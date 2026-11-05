import {
  FileHeaderMessage,
  ChunkDataMessage,
  TransferAckMessage,
  MessageType,
  AckStatus,
  buildFrame,
  FrameReassembler,
} from '../protocol/WireProtocolV2';
import { calculateSha256Hex } from './FileStreamSender';

export interface FileStreamReceiverOptions {
  onProgress?: (bytesReceived: number, totalBytes: number) => void;
  onFileHeader?: (header: FileHeaderMessage) => Promise<boolean> | boolean; // User confirmation
}

export interface ReceivedFileResult {
  fileName: string;
  size: number;
  blob?: Blob;
  sha256Hex: string;
}

export function sanitizeFileName(name: string): string {
  // Strip null bytes and control chars
  let clean = name.replace(/[\x00-\x1f\x7f-\x9f]/g, '');

  // Strip path traversal sequences
  clean = clean.replace(/\\/g, '/');
  const segments = clean.split('/');
  const baseName = segments[segments.length - 1];

  if (!baseName || baseName === '.' || baseName === '..' || baseName.includes('..')) {
    throw new Error(`Unsafe filename detected: "${name}"`);
  }

  return baseName.trim();
}

export class FileStreamReceiver {
  private onProgress?: (bytesReceived: number, totalBytes: number) => void;
  private onFileHeader?: (header: FileHeaderMessage) => Promise<boolean> | boolean;

  constructor(
    private channel: RTCDataChannel,
    options: FileStreamReceiverOptions = {}
  ) {
    this.onProgress = options.onProgress;
    this.onFileHeader = options.onFileHeader;
  }

  receiveFile(): Promise<ReceivedFileResult> {
    return new Promise((resolve, reject) => {
      const reassembler = new FrameReassembler();
      let header: FileHeaderMessage | null = null;
      let safeName: string = '';
      let receivedBytes = 0;
      const chunks: Uint8Array[] = [];

      const onMessage = async (event: MessageEvent) => {
        try {
          const raw = new Uint8Array(event.data as ArrayBuffer);
          const frames = reassembler.push(raw);

          for (const frame of frames) {
            if (frame.header.msgType === MessageType.FileHeader) {
              header = FileHeaderMessage.deserialize(frame.payload);

              // 1. Sanitize file name
              try {
                safeName = sanitizeFileName(header.fileName);
              } catch (err) {
                const ack = new TransferAckMessage();
                ack.status = AckStatus.PathUnsafe;
                ack.receivedBytes = 0n;
                ack.message = `Unsafe path rejected: ${(err as Error).message}`;
                (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
                cleanup();
                return reject(err);
              }

              // 2. User confirmation callback
              if (this.onFileHeader) {
                const accepted = await this.onFileHeader(header);
                if (!accepted) {
                  const ack = new TransferAckMessage();
                  ack.status = AckStatus.Rejected;
                  ack.receivedBytes = 0n;
                  ack.message = 'Transfer rejected by user';
                  (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
                  cleanup();
                  return reject(new Error('Transfer rejected by user'));
                }
              }
            } else if (frame.header.msgType === MessageType.ChunkData) {
              if (!header) {
                throw new Error('Received ChunkData without prior FileHeader');
              }

              const chunk = ChunkDataMessage.deserialize(frame.payload);
              chunks.push(chunk.data);
              receivedBytes += chunk.data.length;

              if (this.onProgress) {
                this.onProgress(receivedBytes, Number(header.fileSize));
              }

              // Check if completed
              if (receivedBytes >= Number(header.fileSize)) {
                // Assemble complete buffer
                const total = new Uint8Array(receivedBytes);
                let cur = 0;
                for (const c of chunks) {
                  total.set(c, cur);
                  cur += c.length;
                }

                // Verify SHA-256
                const actualSha = await calculateSha256Hex(total);
                if (actualSha.toLowerCase() !== header.sha256Hex.toLowerCase()) {
                  const ack = new TransferAckMessage();
                  ack.status = AckStatus.HashMismatch;
                  ack.receivedBytes = BigInt(receivedBytes);
                  ack.message = `SHA-256 mismatch: expected ${header.sha256Hex}, got ${actualSha}`;
                  (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
                  cleanup();
                  return reject(new Error(ack.message));
                }

                // Success ACK
                const ack = new TransferAckMessage();
                ack.status = AckStatus.Success;
                ack.receivedBytes = BigInt(receivedBytes);
                ack.message = 'File verified and saved successfully';
                (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));


                cleanup();
                const blob = new Blob([total]);
                resolve({
                  fileName: safeName,
                  size: receivedBytes,
                  blob,
                  sha256Hex: actualSha,
                });
              }
            }
          }
        } catch (err) {
          cleanup();
          reject(err);
        }
      };

      const cleanup = () => {
        this.channel.removeEventListener('message', onMessage);
      };

      this.channel.addEventListener('message', onMessage);
    });
  }
}
