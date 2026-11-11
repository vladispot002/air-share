import {
  FileHeaderMessage,
  ChunkDataMessage,
  TransferAckMessage,
  ResumeRequestMessage,
  BatchManifestMessage,
  MessageType,
  AckStatus,
  FileHeaderFlags,
  buildFrame,
  FrameReassembler,
} from '../protocol/WireProtocolV2';
import { computeCrc32 } from '../protocol/Crc32';
import { waitForBufferedAmountLow } from '../transport/WebRtcCoordinator';
import { computeFileSha256, computePartialFileSha256 } from '../crypto/Sha256';

export interface BatchSendItem {
  file: File | Blob | { name: string; size: number; slice(start?: number, end?: number): Blob; arrayBuffer?(): Promise<ArrayBuffer> };
  relativePath?: string;
  name?: string;
}

export interface FileStreamSenderOptions {
  chunkSize?: number; // default: 64 KB
  enableResumption?: boolean; // default: true
  negotiationTimeoutMs?: number; // default: 1000 ms
  onProgress?: (bytesSent: number, totalBytes: number) => void;
  onHashProgress?: (bytesHashed: number, totalBytes: number) => void;
  onBatchProgress?: (currentFileIndex: number, totalFiles: number, bytesSent: number, totalBytes: number, currentFileName: string) => void;
}

export class FileStreamSender {
  private chunkSize: number;
  private enableResumption: boolean;
  private negotiationTimeoutMs: number;
  private onProgress?: (bytesSent: number, totalBytes: number) => void;
  private onHashProgress?: (bytesHashed: number, totalBytes: number) => void;
  private onBatchProgress?: (currentFileIndex: number, totalFiles: number, bytesSent: number, totalBytes: number, currentFileName: string) => void;

  constructor(
    private channel: RTCDataChannel,
    options: FileStreamSenderOptions = {}
  ) {
    this.chunkSize = options.chunkSize || 64 * 1024;
    this.enableResumption = options.enableResumption !== false;
    this.negotiationTimeoutMs = options.negotiationTimeoutMs ?? 1000;
    this.onProgress = options.onProgress;
    this.onHashProgress = options.onHashProgress;
    this.onBatchProgress = options.onBatchProgress;
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

    // 1. Send FileHeaderMessage with resumption flag
    const headerMsg = new FileHeaderMessage();
    headerMsg.fileSize = BigInt(file.size);
    headerMsg.chunkSize = this.chunkSize;
    headerMsg.flags = this.enableResumption ? FileHeaderFlags.RESUME_SUPPORTED : FileHeaderFlags.NONE;
    headerMsg.fileName = 'name' in file ? file.name : 'unnamed.bin';
    headerMsg.sha256Hex = sha256Hex;

    const headerFrame = buildFrame(MessageType.FileHeader, headerMsg.serialize());
    (this.channel as any).send(headerFrame);

    let offset = 0;
    let chunkIndex = 0;

    // Resumption negotiation (Milestone 3D)
    if (this.enableResumption && file.size > 0) {
      const negotiation = await new Promise<{ resumeOffset?: bigint; rejected?: boolean }>((res) => {
        let timer: any = null;
        const reassembler = new FrameReassembler();

        const onNegotiationMessage = async (event: MessageEvent) => {
          try {
            const raw = new Uint8Array(event.data as ArrayBuffer);
            const frames = reassembler.push(raw);
            for (const frame of frames) {
              if (frame.header.msgType === MessageType.ResumeRequest) {
                if (timer) clearTimeout(timer);
                this.channel.removeEventListener('message', onNegotiationMessage);
                const resumeReq = ResumeRequestMessage.deserialize(frame.payload);

                // ADR-017 Chunk alignment and bounds validation
                if (
                  resumeReq.fileHash.toLowerCase() !== sha256Hex.toLowerCase() ||
                  resumeReq.bytesReceivedSoFar <= 0n ||
                  resumeReq.bytesReceivedSoFar >= BigInt(file.size) ||
                  resumeReq.bytesReceivedSoFar % BigInt(this.chunkSize) !== 0n
                ) {
                  const ack = new TransferAckMessage();
                  ack.status = AckStatus.ResumeRejected;
                  ack.receivedBytes = 0n;
                  ack.message = 'Resume rejected: invalid bounds or misaligned offset';
                  (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
                  return res({ rejected: true });
                }

                try {
                  const localPartialSha = await computePartialFileSha256(
                    file as Blob,
                    Number(resumeReq.bytesReceivedSoFar)
                  );
                  if (localPartialSha.toLowerCase() === resumeReq.partialSha256.toLowerCase()) {
                    const ack = new TransferAckMessage();
                    ack.status = AckStatus.ResumeAccepted;
                    ack.receivedBytes = resumeReq.bytesReceivedSoFar;
                    ack.message = 'Resume accepted';
                    (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
                    return res({ resumeOffset: resumeReq.bytesReceivedSoFar });
                  }
                } catch {
                  // Partial hash computation failure
                }

                // If mismatch or hash failure, reject resume and restart from 0
                const ack = new TransferAckMessage();
                ack.status = AckStatus.ResumeRejected;
                ack.receivedBytes = 0n;
                ack.message = 'Resume rejected: hash or offset mismatch';
                (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
                return res({ rejected: true });
              } else if (frame.header.msgType === MessageType.TransferCancel) {
                if (timer) clearTimeout(timer);
                this.channel.removeEventListener('message', onNegotiationMessage);
                return res({ rejected: true });
              }
            }
          } catch {
            if (timer) clearTimeout(timer);
            this.channel.removeEventListener('message', onNegotiationMessage);
            res({});
          }
        };

        timer = setTimeout(() => {
          this.channel.removeEventListener('message', onNegotiationMessage);
          res({});
        }, this.negotiationTimeoutMs);

        this.channel.addEventListener('message', onNegotiationMessage);
      });

      if (negotiation.resumeOffset !== undefined) {
        offset = Number(negotiation.resumeOffset);
        chunkIndex = Math.floor(offset / this.chunkSize);
      }
    }

    // 2. Stream ChunkDataMessages slice-by-slice with backpressure (DEF-01)
    const totalBytes = file.size;

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
    return this.waitForAck();
  }

  async sendBatch(
    items: BatchSendItem[],
    batchId?: string
  ): Promise<TransferAckMessage[]> {
    if (this.channel.readyState !== 'open') {
      throw new Error(`Cannot send batch: DataChannel state is ${this.channel.readyState}`);
    }
    if (!items || items.length === 0) {
      throw new Error('Batch file list is empty');
    }

    const effectiveBatchId = batchId || `batch-${Date.now()}`;
    let totalBatchBytes = 0n;
    const preparedFiles: {
      item: BatchSendItem;
      relativePath: string;
      size: bigint;
      sha256Hex: string;
    }[] = [];

    for (let i = 0; i < items.length; i++) {
      const it = items[i];
      const relPath = it.relativePath || ('name' in it.file ? (it.file as any).name : (it.name || `file_${i + 1}`));
      const sz = BigInt(it.file.size);
      totalBatchBytes += sz;

      const sha256Hex = await computeFileSha256(
        it.file as Blob,
        1024 * 1024,
        this.onHashProgress
      );

      preparedFiles.push({
        item: it,
        relativePath: relPath,
        size: sz,
        sha256Hex,
      });
    }

    // 1. Send BatchManifestMessage
    const manifest = new BatchManifestMessage();
    manifest.batchId = effectiveBatchId;
    manifest.totalFiles = preparedFiles.length;
    manifest.totalBytes = totalBatchBytes;
    manifest.files = preparedFiles.map((pf, idx) => ({
      fileIndex: idx,
      fileSize: pf.size,
      relativePath: pf.relativePath,
      sha256Hex: pf.sha256Hex,
    }));

    const manifestFrame = buildFrame(MessageType.BatchManifest, manifest.serialize());
    (this.channel as any).send(manifestFrame);

    // Wait for batch acceptance ACK
    await this.waitForAck((ack) => {
      if (ack.status !== AckStatus.Success) {
        throw new Error(`Batch rejected by receiver: ${ack.message}`);
      }
    });

    const acks: TransferAckMessage[] = [];
    let sentBatchBytes = 0;

    // Stream each file
    for (let i = 0; i < preparedFiles.length; i++) {
      const pf = preparedFiles[i];
      const file = pf.item.file;

      const headerMsg = new FileHeaderMessage();
      headerMsg.fileSize = pf.size;
      headerMsg.chunkSize = this.chunkSize;
      headerMsg.flags = FileHeaderFlags.NONE;
      headerMsg.fileName = pf.relativePath;
      headerMsg.sha256Hex = pf.sha256Hex;

      const headerFrame = buildFrame(MessageType.FileHeader, headerMsg.serialize());
      (this.channel as any).send(headerFrame);

      let offset = 0;
      let chunkIndex = 0;

      while (offset < file.size) {
        const nextChunkEnd = Math.min(offset + this.chunkSize, file.size);
        const slice = file.slice(offset, nextChunkEnd);
        const chunkBuf = new Uint8Array(await slice.arrayBuffer());

        const chunkMsg = new ChunkDataMessage();
        chunkMsg.offset = BigInt(offset);
        chunkMsg.chunkIndex = chunkIndex++;
        chunkMsg.chunkCrc32 = computeCrc32(chunkBuf);
        chunkMsg.data = chunkBuf;

        await waitForBufferedAmountLow(this.channel);
        (this.channel as any).send(buildFrame(MessageType.ChunkData, chunkMsg.serialize()));

        offset += chunkBuf.length;
        sentBatchBytes += chunkBuf.length;

        if (this.onProgress) {
          this.onProgress(sentBatchBytes, Number(totalBatchBytes));
        }
        if (this.onBatchProgress) {
          this.onBatchProgress(i + 1, preparedFiles.length, sentBatchBytes, Number(totalBatchBytes), pf.relativePath);
        }
      }

      // Wait for file ACK
      const fileAck = await this.waitForAck();
      acks.push(fileAck);
    }

    return acks;
  }

  private waitForAck(validator?: (ack: TransferAckMessage) => void): Promise<TransferAckMessage> {
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
              const ack = TransferAckMessage.deserialize(f.payload);
              if (ack.status === AckStatus.ResumeAccepted || ack.status === AckStatus.ResumeRejected) {
                continue;
              }
              settled = true;
              cleanup();
              try {
                if (validator) validator(ack);
                if (ack.status === AckStatus.Success) {
                  resolve(ack);
                } else {
                  reject(new Error(`Transfer failed with status ${ack.status}: ${ack.message}`));
                }
              } catch (err) {
                reject(err);
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
