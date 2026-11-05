import {
  FileHeaderMessage,
  ChunkDataMessage,
  TransferAckMessage,
  MessageType,
  AckStatus,
  buildFrame,
  FrameReassembler,
} from '../protocol/WireProtocolV2';
import { waitForBufferedAmountLow } from '../transport/WebRtcCoordinator';

export interface FileStreamSenderOptions {
  chunkSize?: number; // default: 64 KB
  onProgress?: (bytesSent: number, totalBytes: number) => void;
}

export async function calculateSha256Hex(data: ArrayBuffer | Uint8Array): Promise<string> {
  const buffer = data instanceof Uint8Array
    ? (data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength) as ArrayBuffer)
    : data;
  const hashBuffer = await crypto.subtle.digest('SHA-256', buffer);
  const hashArray = Array.from(new Uint8Array(hashBuffer));
  return hashArray.map((b) => b.toString(16).padStart(2, '0')).join('');
}

export class FileStreamSender {
  private chunkSize: number;
  private onProgress?: (bytesSent: number, totalBytes: number) => void;

  constructor(
    private channel: RTCDataChannel,
    options: FileStreamSenderOptions = {}
  ) {
    this.chunkSize = options.chunkSize || 64 * 1024;
    this.onProgress = options.onProgress;
  }

  async sendFile(file: File | { name: string; size: number; arrayBuffer(): Promise<ArrayBuffer> }): Promise<TransferAckMessage> {
    if (this.channel.readyState !== 'open') {
      throw new Error(`Cannot send file: DataChannel state is ${this.channel.readyState}`);
    }

    const fileBuffer = await file.arrayBuffer();
    const fileBytes = new Uint8Array(fileBuffer);
    const sha256Hex = await calculateSha256Hex(fileBytes);

    // 1. Send FileHeaderMessage
    const headerMsg = new FileHeaderMessage();
    headerMsg.fileSize = BigInt(file.size);
    headerMsg.chunkSize = this.chunkSize;
    headerMsg.flags = 0;
    headerMsg.fileName = file.name;
    headerMsg.sha256Hex = sha256Hex;

    const headerFrame = buildFrame(MessageType.FileHeader, headerMsg.serialize());
    (this.channel as any).send(headerFrame);

    // 2. Stream ChunkDataMessages with backpressure
    const totalBytes = file.size;
    let offset = 0;
    let chunkIndex = 0;

    while (offset < totalBytes) {
      const currentChunkSize = Math.min(this.chunkSize, totalBytes - offset);
      const chunkBytes = fileBytes.subarray(offset, offset + currentChunkSize);

      const chunkMsg = new ChunkDataMessage();
      chunkMsg.offset = BigInt(offset);
      chunkMsg.chunkIndex = chunkIndex;
      chunkMsg.chunkCrc32 = 0; // will be auto-calculated in serialize()
      chunkMsg.data = chunkBytes;

      const chunkFrame = buildFrame(MessageType.ChunkData, chunkMsg.serialize());
      (this.channel as any).send(chunkFrame);


      offset += currentChunkSize;
      chunkIndex++;

      if (this.onProgress) {
        this.onProgress(offset, totalBytes);
      }

      // Backpressure check
      if (this.channel.bufferedAmount > 1024 * 1024) {
        await waitForBufferedAmountLow(this.channel, 512 * 1024);
      }
    }

    // 3. Await TransferAckMessage
    return new Promise<TransferAckMessage>((resolve, reject) => {
      const reassembler = new FrameReassembler();

      const onMessage = (event: MessageEvent) => {
        try {
          const raw = new Uint8Array(event.data as ArrayBuffer);
          const frames = reassembler.push(raw);

          for (const f of frames) {
            if (f.header.msgType === MessageType.TransferAck) {
              this.channel.removeEventListener('message', onMessage);
              const ack = TransferAckMessage.deserialize(f.payload);
              if (ack.status === AckStatus.Success) {
                resolve(ack);
              } else {
                reject(new Error(`Transfer failed with status ${ack.status}: ${ack.message}`));
              }
              return;
            } else if (f.header.msgType === MessageType.TransferCancel) {
              this.channel.removeEventListener('message', onMessage);
              reject(new Error('Transfer cancelled by peer'));
              return;
            }
          }
        } catch (err) {
          this.channel.removeEventListener('message', onMessage);
          reject(err);
        }
      };

      this.channel.addEventListener('message', onMessage);
    });
  }
}
