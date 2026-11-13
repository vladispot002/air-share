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
  ReassembledFrame,
} from '../protocol/WireProtocolV2';
import { computeCrc32 } from '../protocol/Crc32';
import { Sha256Stream } from '../crypto/Sha256';
import { IStorageSink, createStorageSink } from './StorageSink';

export interface FileStreamReceiverOptions {
  onProgress?: (bytesReceived: number, totalBytes: number) => void;
  onFileHeader?: (header: FileHeaderMessage) => Promise<boolean> | boolean; // User confirmation
  onBatchManifest?: (manifest: BatchManifestMessage) => Promise<boolean> | boolean;
  onBatchProgress?: (currentFileIndex: number, totalFiles: number, bytesReceived: number, totalBytes: number, currentFileName: string) => void;
  preferOpfs?: boolean;
  resumeContext?: {
    fileHash: string;
    bytesReceivedSoFar: number;
    partialSha256: string;
    partialChunks?: Uint8Array[];
  };
}

export interface ReceivedFileResult {
  fileName: string;
  size: number;
  blob?: Blob;
  file?: File;
  sha256Hex: string;
}

export interface ReceivedTransferResult {
  isBatch: boolean;
  batchId?: string;
  totalBytes: number;
  files: ReceivedFileResult[];
}

// Windows reserved device names (case-insensitive, with or without extensions)
const WIN32_RESERVED_NAMES = new Set([
  'CON', 'PRN', 'AUX', 'NUL',
  'COM1', 'COM2', 'COM3', 'COM4', 'COM5', 'COM6', 'COM7', 'COM8', 'COM9',
  'LPT1', 'LPT2', 'LPT3', 'LPT4', 'LPT5', 'LPT6', 'LPT7', 'LPT8', 'LPT9',
]);

// Unicode bidirectional override and zero-width characters (RTLO, LRE, RLE, PDF, etc.)
const BIDI_AND_ZERO_WIDTH_REGEX = /[\u202A-\u202E\u2066-\u2069\u200B\u200E\u200F]/;

/**
 * Validates and sanitizes received filenames against path traversal, Win32 devices,
 * control characters, and Unicode spoofing attacks (parity with C++ isSafeFileName).
 */
export function sanitizeFileName(name: string): string {
  if (!name || name.length === 0 || name.length > 255) {
    throw new Error(`Invalid filename length: ${name ? name.length : 0}`);
  }

  // Strictly forbid any directory separators or traversal sequences
  if (name.includes('/') || name.includes('\\') || name.includes('..')) {
    throw new Error(`Unsafe directory traversal sequence in filename: "${name}"`);
  }

  const baseName = name.trim();
  if (baseName.length === 0) {
    throw new Error('Filename cannot be empty or whitespace only');
  }

  // Check leading/trailing dots or spaces
  if (baseName.startsWith('.') || baseName.endsWith('.') || baseName.startsWith(' ') || baseName.endsWith(' ')) {
    throw new Error(`Unsafe filename with leading/trailing dot or space: "${baseName}"`);
  }

  // Check forbidden characters: control characters, :, <, >, ", |, ?, *
  for (let i = 0; i < baseName.length; i++) {
    const code = baseName.charCodeAt(i);
    if (code < 32 || code === 127 ||
        baseName[i] === ':' || baseName[i] === '<' || baseName[i] === '>' ||
        baseName[i] === '"' || baseName[i] === '|' || baseName[i] === '?' || baseName[i] === '*') {
      throw new Error(`Unsafe character in filename: "${baseName}" at index ${i}`);
    }
  }

  // Unicode RTLO / zero-width spoofing check
  if (BIDI_AND_ZERO_WIDTH_REGEX.test(baseName)) {
    throw new Error(`Unsafe Unicode bidi/spoofing characters in filename: "${baseName}"`);
  }

  // Windows reserved device name check (stem before first dot)
  const firstDot = baseName.indexOf('.');
  const stem = (firstDot === -1 ? baseName : baseName.substring(0, firstDot)).toUpperCase();
  if (WIN32_RESERVED_NAMES.has(stem)) {
    throw new Error(`Unsafe Windows reserved device filename: "${baseName}"`);
  }

  return baseName;
}

/**
 * Validates and sanitizes nested relative paths for batch and folder transfers (parity with C++ isSafeRelativePath).
 */
export function sanitizeRelativePath(relPath: string): string {
  if (!relPath || relPath.length === 0 || relPath.length > 1024) {
    throw new Error(`Invalid relative path length: ${relPath ? relPath.length : 0}`);
  }
  if (relPath.startsWith('/') || relPath.startsWith('\\') || relPath.endsWith('/') || relPath.endsWith('\\')) {
    throw new Error(`Unsafe relative path with leading or trailing slash: "${relPath}"`);
  }
  const normalized = relPath.replace(/\\/g, '/');
  if (normalized.includes('//')) {
    throw new Error(`Unsafe relative path containing double slash: "${relPath}"`);
  }
  const segments = normalized.split('/');
  for (const seg of segments) {
    if (seg === '.' || seg === '..') {
      throw new Error(`Unsafe directory traversal segment "${seg}" in path: "${relPath}"`);
    }
    sanitizeFileName(seg);
  }
  return normalized;
}

export class FileStreamReceiver {
  private options: FileStreamReceiverOptions;
  private onProgress?: (bytesReceived: number, totalBytes: number) => void;
  private onFileHeader?: (header: FileHeaderMessage) => Promise<boolean> | boolean;
  private onBatchManifest?: (manifest: BatchManifestMessage) => Promise<boolean> | boolean;
  private onBatchProgress?: (currentFileIndex: number, totalFiles: number, bytesReceived: number, totalBytes: number, currentFileName: string) => void;

  constructor(
    private channel: RTCDataChannel,
    options: FileStreamReceiverOptions = {}
  ) {
    this.options = options;
    this.onProgress = options.onProgress;
    this.onFileHeader = options.onFileHeader;
    this.onBatchManifest = options.onBatchManifest;
    this.onBatchProgress = options.onBatchProgress;
  }

  async receiveFile(): Promise<ReceivedFileResult> {
    const result = await this.receiveTransfer();
    if (result.files.length === 0) {
      throw new Error('No files received in transfer');
    }
    return result.files[0];
  }

  receiveTransfer(): Promise<ReceivedTransferResult> {
    return new Promise((resolve, reject) => {
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
          reject(new Error('RTCDataChannel closed during transfer receive'));
        }
      };

      const onError = () => {
        if (!settled) {
          settled = true;
          cleanup();
          reject(new Error('RTCDataChannel error during transfer receive'));
        }
      };

      let isBatchTransfer = false;
      let batchManifest: BatchManifestMessage | null = null;
      let currentFileIndex = 0;
      const receivedFiles: ReceivedFileResult[] = [];
      let totalBatchBytesReceived = 0;

      // Single file state
      let singleHeader: FileHeaderMessage | null = null;
      let singleSafeName: string = '';
      let singleReceivedBytes = 0;
      let singleExpectedOffset = 0n;
      let singleExpectedChunkIndex = 0;
      let singleHasher = new Sha256Stream();
      const singleChunks: Uint8Array[] = [];
      const pendingChunkFrames: ReassembledFrame[] = [];
      let resumeTimer: any = null;

      enum SinglePhase {
        AwaitingHeader,
        AwaitingResumeAck,
        AwaitingUserConfirm,
        StreamingChunks,
      }
      let singlePhase = SinglePhase.AwaitingHeader;

      // Batch file state
      enum BatchPhase {
        AwaitingFileHeader,
        StreamingFileChunks,
      }
      let batchPhase = BatchPhase.AwaitingFileHeader;
      let currentBatchHeader: FileHeaderMessage | null = null;
      let currentBatchSafeName: string = '';
      let currentBatchReceivedBytes = 0;
      let currentBatchExpectedOffset = 0n;
      let currentBatchExpectedChunkIndex = 0;
      let currentBatchHasher = new Sha256Stream();
      let currentBatchSink: IStorageSink | null = null;

      const processSingleChunk = (frame: ReassembledFrame) => {
        if (!singleHeader) {
          throw new Error('Received ChunkData without prior FileHeader');
        }
        const chunk = ChunkDataMessage.deserialize(frame.payload);

        if (chunk.chunkIndex !== singleExpectedChunkIndex || chunk.offset !== singleExpectedOffset) {
          const ack = new TransferAckMessage();
          ack.status = AckStatus.GenericError;
          ack.receivedBytes = singleExpectedOffset;
          ack.message = `Chunk sequence mismatch: expected index ${singleExpectedChunkIndex} at offset ${singleExpectedOffset}, got index ${chunk.chunkIndex} at offset ${chunk.offset}`;
          (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
          settled = true;
          cleanup();
          reject(new Error(ack.message));
          return;
        }

        if (chunk.chunkCrc32 !== 0) {
          const actualCrc = computeCrc32(chunk.data);
          if (actualCrc !== chunk.chunkCrc32) {
            const ack = new TransferAckMessage();
            ack.status = AckStatus.HashMismatch;
            ack.receivedBytes = singleExpectedOffset;
            ack.message = `Chunk CRC-32 mismatch on chunk ${chunk.chunkIndex}`;
            (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
            settled = true;
            cleanup();
            reject(new Error(ack.message));
            return;
          }
        }

        singleHasher.update(chunk.data);
        singleChunks.push(chunk.data);
        singleReceivedBytes += chunk.data.length;
        singleExpectedChunkIndex++;
        singleExpectedOffset += BigInt(chunk.data.length);

        if (this.onProgress) {
          this.onProgress(singleReceivedBytes, Number(singleHeader.fileSize));
        }

        if (singleReceivedBytes >= Number(singleHeader.fileSize)) {
          completeSingleTransfer();
        }
      };

      const completeSingleTransfer = () => {
        if (!singleHeader || settled) return;
        settled = true;
        cleanup();

        const actualSha = singleHasher.digestHex();
        if (singleHeader.sha256Hex && actualSha.toLowerCase() !== singleHeader.sha256Hex.toLowerCase()) {
          const ack = new TransferAckMessage();
          ack.status = AckStatus.HashMismatch;
          ack.receivedBytes = BigInt(singleReceivedBytes);
          ack.message = `SHA-256 mismatch: expected ${singleHeader.sha256Hex}, got ${actualSha}`;
          (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
          reject(new Error(ack.message));
          return;
        }

        const ack = new TransferAckMessage();
        ack.status = AckStatus.Success;
        ack.receivedBytes = BigInt(singleReceivedBytes);
        ack.message = 'File verified and saved successfully';
        (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));

        const blob = new Blob(singleChunks as BlobPart[]);
        resolve({
          isBatch: false,
          totalBytes: singleReceivedBytes,
          files: [
            {
              fileName: singleSafeName,
              size: singleReceivedBytes,
              blob,
              sha256Hex: actualSha,
            },
          ],
        });
      };

      const proceedSingleAfterResume = async (resumed: boolean, acceptedOffset?: bigint) => {
        if (settled) return;
        if (resumed && acceptedOffset !== undefined) {
          const offsetNum = Number(acceptedOffset);
          singleReceivedBytes = offsetNum;
          singleExpectedOffset = acceptedOffset;
          singleExpectedChunkIndex = Math.floor(offsetNum / singleHeader!.chunkSize);
          if (this.options.resumeContext?.partialChunks) {
            for (const ch of this.options.resumeContext.partialChunks) {
              singleHasher.update(ch);
              singleChunks.push(ch);
            }
          }
        } else {
          singleReceivedBytes = 0;
          singleExpectedOffset = 0n;
          singleExpectedChunkIndex = 0;
        }

        if (this.onFileHeader) {
          singlePhase = SinglePhase.AwaitingUserConfirm;
          try {
            const accepted = await this.onFileHeader(singleHeader!);
            if (!accepted) {
              const ack = new TransferAckMessage();
              ack.status = AckStatus.Rejected;
              ack.receivedBytes = 0n;
              ack.message = 'Transfer rejected by user';
              (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
              settled = true;
              cleanup();
              pendingChunkFrames.length = 0;
              return reject(new Error('Transfer rejected by user'));
            }
          } catch (err) {
            settled = true;
            cleanup();
            pendingChunkFrames.length = 0;
            return reject(err);
          }
        }

        if (singleHeader!.fileSize === 0n) {
          completeSingleTransfer();
          return;
        }

        singlePhase = SinglePhase.StreamingChunks;
        while (pendingChunkFrames.length > 0) {
          const queued = pendingChunkFrames.shift()!;
          processSingleChunk(queued);
          if (settled) return;
        }
      };

      const processBatchChunk = async (frame: ReassembledFrame) => {
        if (!currentBatchHeader || !batchManifest) return;
        const chunk = ChunkDataMessage.deserialize(frame.payload);

        if (chunk.chunkIndex !== currentBatchExpectedChunkIndex || chunk.offset !== currentBatchExpectedOffset) {
          const ack = new TransferAckMessage();
          ack.status = AckStatus.GenericError;
          ack.receivedBytes = currentBatchExpectedOffset;
          ack.message = `Chunk sequence mismatch in batch file ${currentFileIndex}: expected index ${currentBatchExpectedChunkIndex}, got ${chunk.chunkIndex}`;
          (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
          settled = true;
          cleanup();
          reject(new Error(ack.message));
          return;
        }

        if (chunk.chunkCrc32 !== 0) {
          const actualCrc = computeCrc32(chunk.data);
          if (actualCrc !== chunk.chunkCrc32) {
            const ack = new TransferAckMessage();
            ack.status = AckStatus.HashMismatch;
            ack.receivedBytes = currentBatchExpectedOffset;
            ack.message = `Chunk CRC-32 mismatch on batch file ${currentFileIndex}`;
            (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
            settled = true;
            cleanup();
            reject(new Error(ack.message));
            return;
          }
        }

        currentBatchHasher.update(chunk.data);
        if (currentBatchSink) {
          await currentBatchSink.write(chunk.data);
        }
        currentBatchReceivedBytes += chunk.data.length;
        totalBatchBytesReceived += chunk.data.length;
        currentBatchExpectedChunkIndex++;
        currentBatchExpectedOffset += BigInt(chunk.data.length);

        if (this.onProgress) {
          this.onProgress(totalBatchBytesReceived, Number(batchManifest.totalBytes));
        }
        if (this.onBatchProgress) {
          this.onBatchProgress(
            currentFileIndex + 1,
            batchManifest.totalFiles,
            totalBatchBytesReceived,
            Number(batchManifest.totalBytes),
            currentBatchSafeName
          );
        }

        if (currentBatchReceivedBytes >= Number(currentBatchHeader.fileSize)) {
          await completeCurrentBatchFile();
        }
      };

      const completeCurrentBatchFile = async () => {
        if (!currentBatchHeader || !batchManifest) return;

        const actualSha = currentBatchHasher.digestHex();
        if (currentBatchHeader.sha256Hex && actualSha.toLowerCase() !== currentBatchHeader.sha256Hex.toLowerCase()) {
          const ack = new TransferAckMessage();
          ack.status = AckStatus.HashMismatch;
          ack.receivedBytes = BigInt(currentBatchReceivedBytes);
          ack.message = `SHA-256 mismatch on batch file ${currentBatchSafeName}`;
          (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
          settled = true;
          cleanup();
          reject(new Error(ack.message));
          return;
        }

        let savedBlobOrFile: Blob | File | undefined;
        if (currentBatchSink) {
          savedBlobOrFile = await currentBatchSink.finalize();
        }

        const ack = new TransferAckMessage();
        ack.status = AckStatus.Success;
        ack.receivedBytes = BigInt(currentBatchReceivedBytes);
        ack.message = `File ${currentBatchSafeName} verified`;
        (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));

        const result: ReceivedFileResult = {
          fileName: currentBatchSafeName,
          size: currentBatchReceivedBytes,
          sha256Hex: actualSha,
        };
        if (savedBlobOrFile instanceof File) {
          result.file = savedBlobOrFile;
        } else if (savedBlobOrFile instanceof Blob) {
          result.blob = savedBlobOrFile;
        }
        receivedFiles.push(result);

        currentFileIndex++;
        if (currentFileIndex >= batchManifest.totalFiles) {
          settled = true;
          cleanup();
          resolve({
            isBatch: true,
            batchId: batchManifest.batchId,
            totalBytes: totalBatchBytesReceived,
            files: receivedFiles,
          });
        } else {
          batchPhase = BatchPhase.AwaitingFileHeader;
          currentBatchHeader = null;
          currentBatchSink = null;
        }
      };

      const onMessage = async (event: MessageEvent) => {
        try {
          const raw = new Uint8Array(event.data as ArrayBuffer);
          const frames = reassembler.push(raw);

          for (const frame of frames) {
            if (frame.header.msgType === MessageType.TransferCancel) {
              if (resumeTimer) clearTimeout(resumeTimer);
              settled = true;
              cleanup();
              reject(new Error('Transfer cancelled by sender'));
              return;
            }

            // BATCH MANIFEST
            if (frame.header.msgType === MessageType.BatchManifest) {
              isBatchTransfer = true;
              batchManifest = BatchManifestMessage.deserialize(frame.payload);

              for (const f of batchManifest.files) {
                try {
                  sanitizeRelativePath(f.relativePath);
                } catch (err) {
                  const ack = new TransferAckMessage();
                  ack.status = AckStatus.PathUnsafe;
                  ack.receivedBytes = 0n;
                  ack.message = `Unsafe relative path in batch: ${(err as Error).message}`;
                  (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
                  settled = true;
                  cleanup();
                  return reject(err);
                }
              }

              if (this.onBatchManifest) {
                const accepted = await this.onBatchManifest(batchManifest);
                if (!accepted) {
                  const ack = new TransferAckMessage();
                  ack.status = AckStatus.Rejected;
                  ack.receivedBytes = 0n;
                  ack.message = 'Batch transfer rejected by user';
                  (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
                  settled = true;
                  cleanup();
                  return reject(new Error('Batch transfer rejected by user'));
                }
              }

              const ack = new TransferAckMessage();
              ack.status = AckStatus.Success;
              ack.receivedBytes = 0n;
              ack.message = 'Batch manifest accepted';
              (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
              batchPhase = BatchPhase.AwaitingFileHeader;
              continue;
            }

            // FILE HEADER
            if (frame.header.msgType === MessageType.FileHeader) {
              if (isBatchTransfer) {
                if (batchPhase !== BatchPhase.AwaitingFileHeader) continue;
                currentBatchHeader = FileHeaderMessage.deserialize(frame.payload);
                currentBatchSafeName = sanitizeRelativePath(currentBatchHeader.fileName);
                currentBatchReceivedBytes = 0;
                currentBatchExpectedOffset = 0n;
                currentBatchExpectedChunkIndex = 0;
                currentBatchHasher = new Sha256Stream();
                currentBatchSink = await createStorageSink(currentBatchSafeName, this.options.preferOpfs);

                if (currentBatchHeader.fileSize === 0n) {
                  await completeCurrentBatchFile();
                } else {
                  batchPhase = BatchPhase.StreamingFileChunks;
                }
                continue;
              } else {
                if (singlePhase !== SinglePhase.AwaitingHeader) continue;
                singleHeader = FileHeaderMessage.deserialize(frame.payload);
                try {
                  singleSafeName = sanitizeFileName(singleHeader.fileName);
                } catch (err) {
                  const ack = new TransferAckMessage();
                  ack.status = AckStatus.PathUnsafe;
                  ack.receivedBytes = 0n;
                  ack.message = `Unsafe path rejected: ${(err as Error).message}`;
                  (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
                  settled = true;
                  cleanup();
                  return reject(err);
                }

                const canResume =
                  Boolean(this.options.resumeContext) &&
                  (singleHeader.flags & FileHeaderFlags.RESUME_SUPPORTED) !== 0 &&
                  this.options.resumeContext!.fileHash.toLowerCase() === singleHeader.sha256Hex.toLowerCase() &&
                  this.options.resumeContext!.bytesReceivedSoFar > 0 &&
                  this.options.resumeContext!.bytesReceivedSoFar < Number(singleHeader.fileSize);

                if (canResume) {
                  const alignedOffset =
                    Math.floor(this.options.resumeContext!.bytesReceivedSoFar / singleHeader.chunkSize) * singleHeader.chunkSize;

                  if (alignedOffset > 0) {
                    const resumeReq = new ResumeRequestMessage();
                    resumeReq.fileHash = singleHeader.sha256Hex;
                    resumeReq.bytesReceivedSoFar = BigInt(alignedOffset);
                    resumeReq.partialSha256 = this.options.resumeContext!.partialSha256;
                    (this.channel as any).send(buildFrame(MessageType.ResumeRequest, resumeReq.serialize()));

                    singlePhase = SinglePhase.AwaitingResumeAck;
                    resumeTimer = setTimeout(() => {
                      if (singlePhase === SinglePhase.AwaitingResumeAck) {
                        resumeTimer = null;
                        proceedSingleAfterResume(false);
                      }
                    }, 15000);
                    continue;
                  }
                }

                await proceedSingleAfterResume(false);
                continue;
              }
            }

            // TRANSFER ACK (for resumption negotiation on single file)
            if (frame.header.msgType === MessageType.TransferAck) {
              if (!isBatchTransfer && singlePhase === SinglePhase.AwaitingResumeAck) {
                if (resumeTimer) {
                  clearTimeout(resumeTimer);
                  resumeTimer = null;
                }
                const ack = TransferAckMessage.deserialize(frame.payload);
                if (ack.status === AckStatus.ResumeAccepted) {
                  const alignedOffset =
                    Math.floor(this.options.resumeContext!.bytesReceivedSoFar / singleHeader!.chunkSize) * singleHeader!.chunkSize;
                  let validResume = ack.receivedBytes === BigInt(alignedOffset);
                  if (validResume && this.options.resumeContext?.partialChunks) {
                    let totalPartialBytes = 0;
                    for (const c of this.options.resumeContext.partialChunks) totalPartialBytes += c.length;
                    if (totalPartialBytes !== alignedOffset) {
                      validResume = false;
                    }
                  }
                  await proceedSingleAfterResume(validResume, validResume ? ack.receivedBytes : undefined);
                } else {
                  await proceedSingleAfterResume(false);
                }
              }
              continue;
            }

            // CHUNK DATA
            if (frame.header.msgType === MessageType.ChunkData) {
              if (isBatchTransfer) {
                if (batchPhase === BatchPhase.StreamingFileChunks) {
                  await processBatchChunk(frame);
                }
              } else {
                if (
                  singlePhase === SinglePhase.AwaitingResumeAck ||
                  singlePhase === SinglePhase.AwaitingUserConfirm
                ) {
                  pendingChunkFrames.push(frame);
                } else if (singlePhase === SinglePhase.StreamingChunks) {
                  processSingleChunk(frame);
                }
              }
              continue;
            }
          }
        } catch (err) {
          if (!settled) {
            if (resumeTimer) clearTimeout(resumeTimer);
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
