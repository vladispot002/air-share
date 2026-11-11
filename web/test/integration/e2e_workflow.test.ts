import { describe, it, expect } from 'vitest';
import {
  BatchManifestMessage,
  FileHeaderMessage,
  TransferAckMessage,
  AckStatus,
  MessageType,
  buildFrame,
  FrameHeader,
} from '../../src/core/protocol/WireProtocolV2';
import {
  sanitizeFileName,
  sanitizeRelativePath,
} from '../../src/core/streaming/FileStreamReceiver';

describe('Web Client E2E Workflow & Transfer Integrity Suite', () => {
  describe('Mode B Transfer Event & Auto-Dismissal Workflow', () => {
    it('notifies on transfer.requested and auto-dismisses on transfer.claimed for the same request_id', () => {
      let isModalVisible = false;
      let activeRequestId: string | null = null;
      let statusText = '';

      // Simulate the UI event handler in main.ts
      const handleSignalingEnvelope = (env: { action: string; payload: any }) => {
        if (env.action === 'transfer.requested') {
          activeRequestId = env.payload.request_id;
          isModalVisible = true;
          statusText = `Incoming transfer from ${env.payload.sender_landrop_id || 'Guest'}`;
        } else if (env.action === 'transfer.claimed') {
          if (activeRequestId && activeRequestId === env.payload.request_id) {
            isModalVisible = false;
            activeRequestId = null;
            statusText = `Transfer claimed by another device (${env.payload.claimed_by_device_id})`;
          }
        }
      };

      // 1. Receive transfer.requested for Mode B
      handleSignalingEnvelope({
        action: 'transfer.requested',
        payload: {
          request_id: 'xfer-req-101',
          sender_landrop_id: '@alice',
          sender_device_name: 'Alice MacBook',
          delivery_mode: 'ANY_AVAILABLE_DEVICE',
          files: [{ file_name: 'project_brief.pdf', file_size: 1048576 }],
          total_bytes: 1048576,
        },
      });

      expect(isModalVisible).toBe(true);
      expect(activeRequestId).toBe('xfer-req-101');
      expect(statusText).toContain('Incoming transfer from @alice');

      // 2. Another unrelated transfer claimed should NOT dismiss current modal
      handleSignalingEnvelope({
        action: 'transfer.claimed',
        payload: {
          request_id: 'unrelated-xfer-999',
          claimed_by_device_id: 'dev-other',
        },
      });
      expect(isModalVisible).toBe(true);
      expect(activeRequestId).toBe('xfer-req-101');

      // 3. Current transfer claimed by peer device -> modal auto-dismisses!
      handleSignalingEnvelope({
        action: 'transfer.claimed',
        payload: {
          request_id: 'xfer-req-101',
          claimed_by_device_id: 'dev-bob-phone',
        },
      });
      expect(isModalVisible).toBe(false);
      expect(activeRequestId).toBeNull();
      expect(statusText).toBe('Transfer claimed by another device (dev-bob-phone)');
    });
  });

  describe('Multi-File & Folder Transfer Batch Parsing', () => {
    it('serializes and deserializes BatchManifestMessage with full folder structures', () => {
      const manifest = new BatchManifestMessage();
      manifest.batchId = 'batch_12345678_folder_sync';
      manifest.totalFiles = 3;
      manifest.totalBytes = 2500000n;
      manifest.files = [
        {
          fileIndex: 0,
          fileSize: 500000n,
          relativePath: 'documents/spec.pdf',
          sha256Hex: 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855',
        },
        {
          fileIndex: 1,
          fileSize: 1500000n,
          relativePath: 'images/vacation/sunset.png',
          sha256Hex: 'ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb',
        },
        {
          fileIndex: 2,
          fileSize: 500000n,
          relativePath: 'root_readme.txt',
          sha256Hex: '4e1243bd22c66e76c2ba9eddc1f91394e57f9f835e0766be1b2c4c8cd455b5d1',
        },
      ];

      const serialized = manifest.serialize();
      expect(serialized.length).toBeGreaterThan(0);

      const parsed = BatchManifestMessage.deserialize(serialized);
      expect(parsed.batchId).toBe('batch_12345678_folder_sync');
      expect(parsed.totalFiles).toBe(3);
      expect(parsed.totalBytes).toBe(2500000n);
      expect(parsed.files.length).toBe(3);

      expect(parsed.files[0].relativePath).toBe('documents/spec.pdf');
      expect(parsed.files[1].relativePath).toBe('images/vacation/sunset.png');
      expect(parsed.files[2].relativePath).toBe('root_readme.txt');
    });

    it('sanitizes relative paths and blocks directory traversal attempts', () => {
      // Valid nested paths
      expect(sanitizeRelativePath('folder/subfolder/file.txt')).toBe('folder/subfolder/file.txt');
      expect(sanitizeRelativePath('dir\\sub\\image.png')).toBe('dir/sub/image.png');
      expect(sanitizeRelativePath('single_file.bin')).toBe('single_file.bin');

      // Traversal attacks rejected
      expect(() => sanitizeRelativePath('../secret.env')).toThrow(/Unsafe/);
      expect(() => sanitizeRelativePath('foo/../../bar')).toThrow(/Unsafe/);
      expect(() => sanitizeRelativePath('/etc/passwd')).toThrow(/Unsafe/);
      expect(() => sanitizeRelativePath('\\Windows\\System32')).toThrow(/Unsafe/);
      expect(() => sanitizeRelativePath('C:\\autoexec.bat')).toThrow(/Unsafe/);
      expect(() => sanitizeRelativePath('photos/./hidden.txt')).toThrow(/Unsafe/);
    });

    it('sanitizes single filenames against reserved devices and control characters', () => {
      expect(sanitizeFileName('normal_photo.jpg')).toBe('normal_photo.jpg');

      // Traversal sequences rejected
      expect(() => sanitizeFileName('../secret')).toThrow(/Unsafe/);
      expect(() => sanitizeFileName('foo/bar')).toThrow(/Unsafe/);

      // Windows reserved names rejected
      expect(() => sanitizeFileName('CON.txt')).toThrow(/Unsafe/);
      expect(() => sanitizeFileName('aux.json')).toThrow(/Unsafe/);
      expect(() => sanitizeFileName('NUL')).toThrow(/Unsafe/);

      // Illegal characters rejected
      expect(() => sanitizeFileName('bad:file.txt')).toThrow(/Unsafe/);
      expect(() => sanitizeFileName('wild*card.txt')).toThrow(/Unsafe/);
      expect(() => sanitizeFileName('hello\x00world.txt')).toThrow(/Unsafe/);
    });
  });

  describe('Wire Protocol V2 Framing & End-to-End Packet Generation', () => {
    it('creates framed packets that decode correctly through FrameHeader', () => {
      const headerMsg = new FileHeaderMessage();
      headerMsg.fileName = 'presentation.pdf';
      headerMsg.fileSize = 1048576n;
      headerMsg.chunkSize = 64 * 1024;
      headerMsg.sha256Hex = 'a'.repeat(64);

      const frame = buildFrame(MessageType.FileHeader, headerMsg.serialize(), 1);
      const parsedHdr = FrameHeader.deserialize(frame);

      expect(parsedHdr.version).toBe(2);
      expect(parsedHdr.msgType).toBe(MessageType.FileHeader);
      expect(parsedHdr.crc32OrSeq).toBe(1);

      const payload = frame.subarray(16);
      const parsedMsg = FileHeaderMessage.deserialize(payload);
      expect(parsedMsg.fileName).toBe('presentation.pdf');
      expect(parsedMsg.fileSize).toBe(1048576n);
      expect(parsedMsg.sha256Hex).toBe('a'.repeat(64));
    });

    it('generates TransferAck with correct status codes', () => {
      const ack = new TransferAckMessage();
      ack.status = AckStatus.Success;
      ack.receivedBytes = 1048576n;
      ack.message = 'SHA-256 verified successfully';

      const serialized = ack.serialize();
      const deserialized = TransferAckMessage.deserialize(serialized);

      expect(deserialized.status).toBe(AckStatus.Success);
      expect(deserialized.receivedBytes).toBe(1048576n);
      expect(deserialized.message).toBe('SHA-256 verified successfully');
    });
  });
});
