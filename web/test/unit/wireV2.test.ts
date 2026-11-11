import { describe, it, expect } from 'vitest';
import {
  FrameHeader,
  HandshakeMessage,
  FileHeaderMessage,
  ChunkDataMessage,
  TransferAckMessage,
  TransferCancelMessage,
  ResumeRequestMessage,
  BatchManifestMessage,
  MessageType,
  AckStatus,
  buildFrame,
  FrameReassembler,
  isProtocolV2,
  FRAME_HEADER_SIZE,
} from '../../src/core/protocol/WireProtocolV2';
import { computeCrc32 } from '../../src/core/protocol/Crc32';

describe('Wire Protocol v2 Framing & Messages', () => {
  it('TC-WP2-02: FrameHeader Big-Endian serialization and deserialization', () => {
    const hdr = new FrameHeader();
    hdr.version = 2;
    hdr.msgType = MessageType.FileHeader;
    hdr.payloadLength = 4096;
    hdr.crc32OrSeq = 0xaabbccdd;

    const raw = hdr.serialize();
    expect(raw.length).toBe(FRAME_HEADER_SIZE);

    const parsed = FrameHeader.deserialize(raw);
    expect(parsed.version).toBe(2);
    expect(parsed.msgType).toBe(MessageType.FileHeader);
    expect(parsed.payloadLength).toBe(4096);
    expect(parsed.crc32OrSeq).toBe(0xaabbccdd);
  });

  it('TC-WP2-03: rejects corrupted magic header', () => {
    const hdr = new FrameHeader();
    const raw = hdr.serialize();
    raw[0] = 0x58; // 'X' instead of 'L'
    expect(() => FrameHeader.deserialize(raw)).toThrow(/Invalid Protocol v2 magic/);
  });

  it('TC-WP2-04: rejects unsupported version', () => {
    const hdr = new FrameHeader();
    const raw = hdr.serialize();
    new DataView(raw.buffer).setUint16(4, 99, false); // version 99
    expect(() => FrameHeader.deserialize(raw)).toThrow(/Unsupported protocol version: 99/);
  });

  it('TC-WP2-05: rejects oversized payload (> 16MB)', () => {
    const hdr = new FrameHeader();
    const raw = hdr.serialize();
    new DataView(raw.buffer).setUint32(8, 32 * 1024 * 1024, false); // 32 MB
    expect(() => FrameHeader.deserialize(raw)).toThrow(/Payload exceeds maximum allowed frame size/);
  });

  it('TC-WP2-06: HandshakeMessage serialization roundtrip', () => {
    const hs = new HandshakeMessage();
    hs.capabilitiesFlags = 0x07;
    hs.deviceId = 'uuid-client-web-123';
    hs.deviceName = 'Chrome Linux';
    hs.clientVersion = '2.0.0-web';

    const bytes = hs.serialize();
    const parsed = HandshakeMessage.deserialize(bytes);

    expect(parsed.capabilitiesFlags).toBe(0x07);
    expect(parsed.deviceId).toBe('uuid-client-web-123');
    expect(parsed.deviceName).toBe('Chrome Linux');
    expect(parsed.clientVersion).toBe('2.0.0-web');
  });

  it('TC-WP2-07: FileHeaderMessage serialization roundtrip with 64-char SHA256', () => {
    const fh = new FileHeaderMessage();
    fh.fileSize = 104857600n; // 100 MB
    fh.chunkSize = 65536;
    fh.flags = 0x01;
    fh.fileName = 'archive.tar.gz';
    fh.sha256Hex = 'abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789';

    const bytes = fh.serialize();
    const parsed = FileHeaderMessage.deserialize(bytes);

    expect(parsed.fileSize).toBe(104857600n);
    expect(parsed.chunkSize).toBe(65536);
    expect(parsed.flags).toBe(0x01);
    expect(parsed.fileName).toBe('archive.tar.gz');
    expect(parsed.sha256Hex).toBe(fh.sha256Hex);
  });

  it('TC-WP2-08: ChunkDataMessage serialization and chunk CRC32 verification', () => {
    const chunk = new ChunkDataMessage();
    chunk.offset = 131072n;
    chunk.chunkIndex = 2;
    chunk.data = new Uint8Array([0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80]);

    const bytes = chunk.serialize();
    const parsed = ChunkDataMessage.deserialize(bytes);

    expect(parsed.offset).toBe(131072n);
    expect(parsed.chunkIndex).toBe(2);
    expect(parsed.data).toEqual(chunk.data);
    expect(parsed.chunkCrc32).toBe(computeCrc32(chunk.data));

    // Tampered payload detection
    bytes[bytes.length - 1] ^= 0xff;
    expect(() => ChunkDataMessage.deserialize(bytes)).toThrow(/Chunk CRC-32 checksum mismatch/);
  });

  it('TC-WP2-09: TransferAckMessage & TransferCancelMessage roundtrip', () => {
    const ack = new TransferAckMessage();
    ack.status = AckStatus.Success;
    ack.receivedBytes = 104857600n;
    ack.message = 'Transfer complete';

    const ackBytes = ack.serialize();
    const parsedAck = TransferAckMessage.deserialize(ackBytes);
    expect(parsedAck.status).toBe(AckStatus.Success);
    expect(parsedAck.receivedBytes).toBe(104857600n);
    expect(parsedAck.message).toBe('Transfer complete');

    const cancel = new TransferCancelMessage();
    cancel.errorCode = 403;
    cancel.reason = 'Quota exceeded';

    const cancelBytes = cancel.serialize();
    const parsedCancel = TransferCancelMessage.deserialize(cancelBytes);
    expect(parsedCancel.errorCode).toBe(403);
    expect(parsedCancel.reason).toBe('Quota exceeded');
  });

  it('TC-WP2-10: FrameReassembler handles arbitrary fragmentation and concatenated frames', () => {
    const reassembler = new FrameReassembler();
    const payload1 = new Uint8Array([1, 2, 3, 4, 5]);
    const payload2 = new Uint8Array([10, 20, 30, 40]);

    const frame1 = buildFrame(MessageType.Ping, payload1);
    const frame2 = buildFrame(MessageType.Pong, payload2);

    const combined = new Uint8Array(frame1.length + frame2.length);
    combined.set(frame1, 0);
    combined.set(frame2, frame1.length);

    // Feed in small 3-byte slices to test fragmentation handling
    const resultFrames = [];
    for (let i = 0; i < combined.length; i += 3) {
      const slice = combined.subarray(i, Math.min(i + 3, combined.length));
      const res = reassembler.push(slice);
      resultFrames.push(...res);
    }

    expect(resultFrames.length).toBe(2);
    expect(resultFrames[0].header.msgType).toBe(MessageType.Ping);
    expect(resultFrames[0].payload).toEqual(payload1);

    expect(resultFrames[1].header.msgType).toBe(MessageType.Pong);
    expect(resultFrames[1].payload).toEqual(payload2);
  });

  it('TC-WP2-11: verifies isProtocolV2 magic byte detection', () => {
    expect(isProtocolV2(new Uint8Array([0x4c, 0x4e, 0x44, 0x50]))).toBe(true);
    expect(isProtocolV2(new Uint8Array([14, 0, 0, 0]))).toBe(false);
    expect(isProtocolV2(new Uint8Array([0, 0]))).toBe(false);
  });

  it('TC-WP2-12: ResumeRequestMessage serialization roundtrip and fixed 136 bytes size', () => {
    const req = new ResumeRequestMessage();
    req.fileHash = 'abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789';
    req.bytesReceivedSoFar = 1048576n; // 1 MB
    req.partialSha256 = '11223344556677889900aabbccddeeff11223344556677889900aabbccddeeff';

    const bytes = req.serialize();
    expect(bytes.length).toBe(136);

    const parsed = ResumeRequestMessage.deserialize(bytes);
    expect(parsed.fileHash).toBe(req.fileHash);
    expect(parsed.bytesReceivedSoFar).toBe(1048576n);
    expect(parsed.partialSha256).toBe(req.partialSha256);
  });

  it('TC-WP2-13: ResumeRequestMessage underflow rejection (< 136 bytes)', () => {
    expect(() => ResumeRequestMessage.deserialize(new Uint8Array(135))).toThrow(/Buffer underflow/);
    expect(() => ResumeRequestMessage.deserialize(new Uint8Array(0))).toThrow(/Buffer underflow/);
    expect(() => ResumeRequestMessage.deserialize(null as any)).toThrow(/Buffer underflow/);
  });

  it('TC-WP2-14: ResumeRequestMessage preserves 64-bit offsets > 4GB', () => {
    const req = new ResumeRequestMessage();
    req.fileHash = 'a'.repeat(64);
    req.bytesReceivedSoFar = 10737418240n; // 10 GB
    req.partialSha256 = 'b'.repeat(64);

    const bytes = req.serialize();
    expect(bytes.length).toBe(136);

    const parsed = ResumeRequestMessage.deserialize(bytes);
    expect(parsed.bytesReceivedSoFar).toBe(10737418240n);
  });

  it('TC-WP2-15: BatchManifestMessage serialization and directory hierarchy roundtrip', () => {
    const manifest = new BatchManifestMessage();
    manifest.batchId = 'batch-9901';
    manifest.totalFiles = 3;
    manifest.totalBytes = 25000000n;
    manifest.files = [
      {
        fileIndex: 0,
        fileSize: 1000000n,
        relativePath: 'documents/report.pdf',
        sha256Hex: '1'.repeat(64),
      },
      {
        fileIndex: 1,
        fileSize: 20000000n,
        relativePath: 'media/video.mp4',
        sha256Hex: '2'.repeat(64),
      },
      {
        fileIndex: 2,
        fileSize: 4000000n,
        relativePath: 'README.md',
        sha256Hex: '3'.repeat(64),
      },
    ];

    const bytes = manifest.serialize();
    const parsed = BatchManifestMessage.deserialize(bytes);

    expect(parsed.batchId).toBe('batch-9901');
    expect(parsed.totalFiles).toBe(3);
    expect(parsed.totalBytes).toBe(25000000n);
    expect(parsed.files.length).toBe(3);
    expect(parsed.files[0].relativePath).toBe('documents/report.pdf');
    expect(parsed.files[1].relativePath).toBe('media/video.mp4');
    expect(parsed.files[2].relativePath).toBe('README.md');
  });

  it('TC-WP2-16: BatchManifestMessage rejects unsafe relative paths', () => {
    const manifest = new BatchManifestMessage();
    manifest.batchId = 'batch-unsafe';
    manifest.totalFiles = 1;
    manifest.totalBytes = 100n;
    manifest.files = [
      {
        fileIndex: 0,
        fileSize: 100n,
        relativePath: '../../etc/passwd',
        sha256Hex: '0'.repeat(64),
      },
    ];

    const bytes = manifest.serialize();
    expect(() => BatchManifestMessage.deserialize(bytes)).toThrow(/Unsafe relative path/);
  });
});
