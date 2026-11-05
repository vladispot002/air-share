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
import { computeFileSha256 } from '../crypto/Sha256';

export interface FileStreamSenderOptions {
  chunkSize?: number; // default: 64 KB
  onProgress?: (bytesSent: number, totalBytes: number) => void;
  onHashProgress?: (bytesHashed: number, totalBytes: number) => void;
}

export class FileStreamSender {
  private chunkSize: number;
  private onProgress?: (bytesSent: number, totalBytes: number) => void;
  private onHashProgress?: (bytesHashed: number, totalBytes: number) => void;

  constructor(
    private channel: RTCDataChannel,
    options: FileStreamSenderOptions = {}
  ) {
    this.chunkSize = options.chunkSize || 64 * 1024;
    this.onProgress = options.onProgress;
    this.onHashProgress = options.onHashProgress;
  }

  async sendFile(
    file: File | Blob | { name: string; size: number; slice(start?: number, end?: number): Blob; arrayBuffer?(): Promise<ArrayBuffer> }
  ): Promise<TransferAckMessage> {
    if (this.channel.readyState !== 'open') {
      throw new Error(`Cannot send file: DataChannel state is ${this.channel.readyState}`);
    }

    // DEF-01: Incremental slice-by-slice SHA-256 calculation to avoid heap exhaustion
    const sha256Hex = await computeFileSha256(
      file as Blob,
      1024 * 1024,
      this.onHashProgress
    );

    // 1. Send FileHeaderMessage
    const headerMsg = new FileHeaderMessage();
    headerMsg.fileSize = BigInt(file.size);
    headerMsg.chunkSize = this.chunkSize;
    headerMsg.flags = 0;
    headerMsg.fileName = 'name' in file ? file.name : 'unnamed.bin';
    headerMsg.sha256Hex = sha256Hex;

    const headerFrame = buildFrame(MessageType.FileHeader, headerMsg.serialize());
    (this.channel as any).send(headerFrame);

    // 2. Stream ChunkDataMessages slice-by-slice with backpressure (DEF-01)
    const totalBytes = file.size;
    let offset = 0;
    let chunkIndex = 0;

    // DEF-02: For 0-byte files, skip chunk loop and proceed directly to awaiting ACK
    while (offset < totalBytes) {
      const currentChunkSize = Math.min(this.chunkSize, totalBytes - offset);
      const slice = file.slice(offset, offset + currentChunkSize);
      const chunkBuf = await slice.arrayBuffer();
      const chunkBytes = new Uint8Array(chunkBuf);

      const chunkMsg = new ChunkDataMessage();
      chunkMsg.offset = BigInt(offset);
      chunkMsg.chunkIndex = chunkIndex;
      chunkMsg.chunkCrc32 = 0; // auto-computed in serialize()
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
      let settled = false;

      const cleanup = () => {
        this.channel.removeEventListener('message', onMessage);
        this.channel.removeEventListener('close', onClose);
        this.channel.removeEventListener('error', onError);
      };

      const onClose = () => {
        if (!settled) {
          settled = true;
          cleanup();
          reject(new Error('RTCDataChannel closed while awaiting transfer ACK'));
        }
      };

      const onError = () => {
        if (!settled) {
          settled = true;
          cleanup();
          reject(new Error('RTCDataChannel error while awaiting transfer ACK'));
        }
      };

      const onMessage = (event: MessageEvent) => {
        try {
          const raw = new Uint8Array(event.data as ArrayBuffer);
          const frames = reassembler.push(raw);

          for (const f of frames) {
            if (f.header.msgType === MessageType.TransferAck) {
              settled = true;
              cleanup();
              const ack = TransferAckMessage.deserialize(f.payload);
              if (ack.status === AckStatus.Success) {
                resolve(ack);
              } else {
                reject(new Error(`Transfer failed with status ${ack.status}: ${ack.message}`));
              }
              return;
            } else if (f.header.msgType === MessageType.TransferCancel) {
              settled = true;
              cleanup();
              reject(new Error('Transfer cancelled by peer'));
              return;
            }
          }
        } catch (err) {
          if (!settled) {
            settled = true;
            cleanup();
            reject(err);
          }
        }
      };

      this.channel.addEventListener('message', onMessage);
      this.channel.addEventListener('close', onClose);
      this.channel.addEventListener('error', onError);
    });
  }
}
