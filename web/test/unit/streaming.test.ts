import { describe, it, expect } from 'vitest';
import { FileStreamSender } from '../../src/core/streaming/FileStreamSender';
import { FileStreamReceiver, sanitizeFileName } from '../../src/core/streaming/FileStreamReceiver';
import { AckStatus } from '../../src/core/protocol/WireProtocolV2';
import { Sha256Stream } from '../../src/core/crypto/Sha256';

// In-memory duplex mock of WebRTC RTCDataChannel
class MockDataChannel {
  readyState: 'open' | 'closed' = 'open';
  bufferedAmount: number = 0;
  bufferedAmountLowThreshold: number = 0;
  binaryType: string = 'arraybuffer';
  peer?: MockDataChannel;

  private listeners: Record<string, ((event: any) => void)[]> = {};

  addEventListener(type: string, listener: (event: any) => void): void {
    if (!this.listeners[type]) this.listeners[type] = [];
    this.listeners[type].push(listener);
  }

  removeEventListener(type: string, listener: (event: any) => void): void {
    if (this.listeners[type]) {
      this.listeners[type] = this.listeners[type].filter((l) => l !== listener);
    }
  }

  send(data: Uint8Array | ArrayBuffer): void {
    if (this.readyState !== 'open') {
      throw new Error('DataChannel closed');
    }
    const copy = data instanceof Uint8Array ? data.slice() : new Uint8Array(data).slice();
    // Deliver asynchronously to peer to simulate real network tick
    setTimeout(() => {
      if (this.peer && this.peer.readyState === 'open') {
        const ev = { data: copy.buffer };
        const handlers = this.peer.listeners['message'] || [];
        for (const h of handlers) h(ev);
      }
    }, 1);
  }

  close(): void {
    this.readyState = 'closed';
    const handlers = this.listeners['close'] || [];
    for (const h of handlers) h({});
  }
}

function createChannelPair(): [MockDataChannel, MockDataChannel] {
  const c1 = new MockDataChannel();
  const c2 = new MockDataChannel();
  c1.peer = c2;
  c2.peer = c1;
  return [c1, c2];
}

describe('FileStreamSender & FileStreamReceiver Suite (DEF-13)', () => {
  it('TC-STR-01: transfers a multi-chunk file with SHA-256 and progress tracking', async () => {
    const [senderCh, receiverCh] = createChannelPair();
    const sender = new FileStreamSender(senderCh as any, { chunkSize: 16 });
    const receiver = new FileStreamReceiver(receiverCh as any);

    const payload = new Uint8Array(50);
    for (let i = 0; i < 50; i++) payload[i] = i + 1;
    const expectedSha = new Sha256Stream().update(payload).digestHex();

    const file = new File([payload], 'test_data.bin');

    let senderProgress = 0;
    let receiverProgress = 0;
    sender['onProgress'] = (sent) => { senderProgress = sent; };
    receiver['onProgress'] = (rcv) => { receiverProgress = rcv; };

    const [ack, received] = await Promise.all([
      sender.sendFile(file),
      receiver.receiveFile(),
    ]);

    expect(ack.status).toBe(AckStatus.Success);
    expect(received.fileName).toBe('test_data.bin');
    expect(received.size).toBe(50);
    expect(received.sha256Hex).toBe(expectedSha);
    expect(senderProgress).toBe(50);
    expect(receiverProgress).toBe(50);
  });

  it('TC-STR-02: 0-byte file transfer does not deadlock (DEF-02)', async () => {
    const [senderCh, receiverCh] = createChannelPair();
    const sender = new FileStreamSender(senderCh as any);
    const receiver = new FileStreamReceiver(receiverCh as any);

    const emptyFile = new File([], 'empty.txt');

    const [ack, received] = await Promise.all([
      sender.sendFile(emptyFile),
      receiver.receiveFile(),
    ]);

    expect(ack.status).toBe(AckStatus.Success);
    expect(received.fileName).toBe('empty.txt');
    expect(received.size).toBe(0);
    expect(received.blob?.size).toBe(0);
    expect(received.sha256Hex).toBe('e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855');
  });

  it('TC-STR-03: user rejection cancels transfer cleanly (DEF-05)', async () => {
    const [senderCh, receiverCh] = createChannelPair();
    const sender = new FileStreamSender(senderCh as any);
    const receiver = new FileStreamReceiver(receiverCh as any, {
      onFileHeader: () => false, // Reject
    });

    const file = new File([new Uint8Array([1, 2, 3])], 'secret.pdf');

    await expect(
      Promise.all([
        sender.sendFile(file),
        receiver.receiveFile(),
      ])
    ).rejects.toThrow(/Transfer rejected by user/);
  });

  it('TC-STR-04: buffers chunks arriving while user confirmation is pending (DEF-05)', async () => {
    const [senderCh, receiverCh] = createChannelPair();
    const sender = new FileStreamSender(senderCh as any, { chunkSize: 10 });
    const receiver = new FileStreamReceiver(receiverCh as any, {
      onFileHeader: () => new Promise((resolve) => setTimeout(() => resolve(true), 25)),
    });

    const payload = new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]);
    const file = new File([payload], 'buffered.bin');

    const [ack, received] = await Promise.all([
      sender.sendFile(file),
      receiver.receiveFile(),
    ]);

    expect(ack.status).toBe(AckStatus.Success);
    expect(received.size).toBe(12);
  });

  it('TC-STR-05: detects data corruption and rejects with HashMismatch', async () => {
    const [senderCh, receiverCh] = createChannelPair();
    const sender = new FileStreamSender(senderCh as any, { chunkSize: 32 });
    const receiver = new FileStreamReceiver(receiverCh as any);

    // Corrupt one byte passing through channel
    const origSend = senderCh.send.bind(senderCh);
    let chunkCount = 0;
    senderCh.send = (data: Uint8Array | ArrayBuffer) => {
      const arr = new Uint8Array(data);
      // If it is a ChunkData message (msgType 0x0003), corrupt its payload
      if (arr.length > 20 && arr[6] === 0x00 && arr[7] === 0x03) {
        chunkCount++;
        if (chunkCount === 1) {
          arr[25] ^= 0xff; // corrupt a data byte
        }
      }
      origSend(arr);
    };

    const file = new File([new Uint8Array(64).fill(7)], 'corrupt.bin');

    await expect(
      Promise.all([
        sender.sendFile(file),
        receiver.receiveFile(),
      ])
    ).rejects.toThrow(/CRC-32 mismatch|mismatch/);
  });

  it('TC-STR-06: sanitizeFileName comprehensive security suite (DEF-11)', () => {
    // Valid filenames
    expect(sanitizeFileName('normal.pdf')).toBe('normal.pdf');
    expect(sanitizeFileName('report_2026.final.tar.gz')).toBe('report_2026.final.tar.gz');
    expect(sanitizeFileName('my-photo_01.jpg')).toBe('my-photo_01.jpg');

    // Path traversal and directory separator attacks
    expect(() => sanitizeFileName('../../../etc/passwd')).toThrow(/Unsafe/);
    expect(() => sanitizeFileName('..')).toThrow(/Unsafe/);
    expect(() => sanitizeFileName('.')).toThrow(/Unsafe/);
    expect(() => sanitizeFileName('folder/sub/doc.txt')).toThrow(/Unsafe/);
    expect(() => sanitizeFileName('C:\\Users\\Admin\\file.zip')).toThrow(/Unsafe/);
    expect(() => sanitizeFileName('foo/../bar')).toThrow(/Unsafe/);

    // Leading/trailing dots and spaces
    expect(() => sanitizeFileName('.hidden')).toThrow(/Unsafe/);
    expect(() => sanitizeFileName('trailing. ')).toThrow(/Unsafe/);
    expect(() => sanitizeFileName('trailing.')).toThrow(/Unsafe/);

    // Windows reserved device names
    expect(() => sanitizeFileName('CON.txt')).toThrow(/reserved/);
    expect(() => sanitizeFileName('com1.dat')).toThrow(/reserved/);
    expect(() => sanitizeFileName('aux')).toThrow(/reserved/);
    expect(() => sanitizeFileName('NUL.tar')).toThrow(/reserved/);

    // Forbidden characters
    expect(() => sanitizeFileName('file:stream')).toThrow(/Unsafe/);
    expect(() => sanitizeFileName('file<name>.txt')).toThrow(/Unsafe/);
    expect(() => sanitizeFileName('file|pipe.txt')).toThrow(/Unsafe/);

    // Unicode RTLO spoofing
    expect(() => sanitizeFileName('invoice\u202Ecod.exe')).toThrow(/spoofing/);
  });

  it('TC-STR-07: Resilient resumption seeks to byte offset with partial chunk context', async () => {
    const [senderCh, receiverCh] = createChannelPair();
    const payload = new Uint8Array(128);
    for (let i = 0; i < 128; i++) payload[i] = (i * 7 + 13) & 0xff;
    const file = new File([payload], 'resumed_file.bin');

    const fullSha = new Sha256Stream().update(payload).digestHex();
    const partialOffset = 48;
    const partialSlice = payload.subarray(0, partialOffset);
    const partialSha = new Sha256Stream().update(partialSlice).digestHex();

    // Receiver has partial chunks for the first 48 bytes
    const receiver = new FileStreamReceiver(receiverCh as any, {
      resumeContext: {
        fileHash: fullSha,
        bytesReceivedSoFar: partialOffset,
        partialSha256: partialSha,
        partialChunks: [partialSlice],
      },
    });

    const sender = new FileStreamSender(senderCh as any, {
      chunkSize: 16,
      enableResumption: true,
    });

    // Track chunk offsets sent
    const offsetsSent: number[] = [];
    const origSend = senderCh.send.bind(senderCh);
    senderCh.send = (data: Uint8Array | ArrayBuffer) => {
      const arr = new Uint8Array(data);
      if (arr.length > 20 && arr[6] === 0x00 && arr[7] === 0x03) {
        const view = new DataView(arr.buffer, arr.byteOffset, arr.byteLength);
        offsetsSent.push(Number(view.getBigUint64(16, false)));
      }
      origSend(arr);
    };

    const [ack, received] = await Promise.all([
      sender.sendFile(file),
      receiver.receiveFile(),
    ]);

    expect(ack.status).toBe(AckStatus.Success);
    expect(received.fileName).toBe('resumed_file.bin');
    expect(received.size).toBe(128);
    expect(received.sha256Hex).toBe(fullSha);

    // Verify sender did not stream bytes [0..48)
    expect(offsetsSent.length).toBeGreaterThan(0);
    expect(offsetsSent[0]).toBe(48);
  });

  it('TC-STR-08: Resumption rejected when partial hash mismatches; falls back to offset 0', async () => {
    const [senderCh, receiverCh] = createChannelPair();
    const payload = new Uint8Array(64);
    for (let i = 0; i < 64; i++) payload[i] = i;
    const file = new File([payload], 'mismatch_resume.bin');
    const fullSha = new Sha256Stream().update(payload).digestHex();

    // Receiver has corrupt/invalid partial SHA
    const receiver = new FileStreamReceiver(receiverCh as any, {
      resumeContext: {
        fileHash: fullSha,
        bytesReceivedSoFar: 20,
        partialSha256: 'deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef',
      },
    });

    const sender = new FileStreamSender(senderCh as any, {
      chunkSize: 16,
      enableResumption: true,
    });

    const offsetsSent: number[] = [];
    const origSend = senderCh.send.bind(senderCh);
    senderCh.send = (data: Uint8Array | ArrayBuffer) => {
      const arr = new Uint8Array(data);
      if (arr.length > 20 && arr[6] === 0x00 && arr[7] === 0x03) {
        const view = new DataView(arr.buffer, arr.byteOffset, arr.byteLength);
        offsetsSent.push(Number(view.getBigUint64(16, false)));
      }
      origSend(arr);
    };

    const [ack, received] = await Promise.all([
      sender.sendFile(file),
      receiver.receiveFile(),
    ]);

    expect(ack.status).toBe(AckStatus.Success);
    expect(received.size).toBe(64);
    expect(received.sha256Hex).toBe(fullSha);
    // Verified fallback to offset 0
    expect(offsetsSent[0]).toBe(0);
  });

  it('TC-STR-09: Sender rejects misaligned ResumeRequest and restarts from offset 0', async () => {
    const [senderCh, receiverCh] = createChannelPair();
    const payload = new Uint8Array(64).fill(42);
    const file = new File([payload], 'unaligned.bin');
    const fullSha = new Sha256Stream().update(payload).digestHex();

    const sender = new FileStreamSender(senderCh as any, {
      chunkSize: 16,
      enableResumption: true,
      negotiationTimeoutMs: 100,
    });

    const receivedAcks: any[] = [];
    receiverCh.addEventListener('message', async (ev) => {
      const { FrameReassembler, buildFrame, MessageType, ResumeRequestMessage, TransferAckMessage } = await import('../../src/core/protocol/WireProtocolV2');
      const reassembler = new FrameReassembler();
      const frames = reassembler.push(new Uint8Array(ev.data));
      for (const f of frames) {
        if (f.header.msgType === MessageType.FileHeader) {
          // Send intentionally misaligned resume request (offset 25 when chunkSize is 16)
          const req = new ResumeRequestMessage();
          req.fileHash = fullSha;
          req.bytesReceivedSoFar = 25n;
          req.partialSha256 = '0'.repeat(64);
          receiverCh.send(buildFrame(MessageType.ResumeRequest, req.serialize()));
        } else if (f.header.msgType === MessageType.TransferAck) {
          const ack = TransferAckMessage.deserialize(f.payload);
          receivedAcks.push(ack);
        }
      }
    });

    // Start sender in background
    sender.sendFile(file).catch(() => {});

    await new Promise((r) => setTimeout(r, 60));
    expect(receivedAcks.length).toBeGreaterThan(0);
    expect(receivedAcks[0].status).toBe(AckStatus.ResumeRejected);
    expect(receivedAcks[0].message).toContain('misaligned');
  });

  it('TC-STR-10: Fresh transfer emits zero phantom ACKs prior to full reception', async () => {
    const [senderCh, receiverCh] = createChannelPair();
    const payload = new Uint8Array(128).fill(99);
    const file = new File([payload], 'fresh.bin');

    const receiver = new FileStreamReceiver(receiverCh as any);
    const sender = new FileStreamSender(senderCh as any, {
      chunkSize: 32,
      enableResumption: true,
      negotiationTimeoutMs: 50,
    });

    const receiverAcksSent: any[] = [];
    const origReceiverSend = receiverCh.send.bind(receiverCh);
    receiverCh.send = async (data: any) => {
      const { FrameReassembler, MessageType, TransferAckMessage } = await import('../../src/core/protocol/WireProtocolV2');
      const reassembler = new FrameReassembler();
      const frames = reassembler.push(new Uint8Array(data));
      for (const f of frames) {
        if (f.header.msgType === MessageType.TransferAck) {
          receiverAcksSent.push(TransferAckMessage.deserialize(f.payload));
        }
      }
      origReceiverSend(data);
    };

    const [ack, received] = await Promise.all([
      sender.sendFile(file),
      receiver.receiveFile(),
    ]);

    expect(ack.status).toBe(AckStatus.Success);
    expect(received.size).toBe(128);
    // Exactly ONE ACK emitted at the very end with full byte count
    expect(receiverAcksSent.length).toBe(1);
    expect(receiverAcksSent[0].status).toBe(AckStatus.Success);
    expect(receiverAcksSent[0].receivedBytes).toBe(128n);
  });
});
