import {
  FileHeaderMessage,
  ChunkDataMessage,
  TransferAckMessage,
  ResumeRequestMessage,
  MessageType,
  AckStatus,
  FileHeaderFlags,
  buildFrame,
  FrameReassembler,
  ReassembledFrame,
} from '../protocol/WireProtocolV2';
import { computeCrc32 } from '../protocol/Crc32';
import { Sha256Stream } from '../crypto/Sha256';

export interface FileStreamReceiverOptions {
  onProgress?: (bytesReceived: number, totalBytes: number) => void;
  onFileHeader?: (header: FileHeaderMessage) => Promise<boolean> | boolean; // User confirmation
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
  sha256Hex: string;
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

export class FileStreamReceiver {
  private options: FileStreamReceiverOptions;
  private onProgress?: (bytesReceived: number, totalBytes: number) => void;
  private onFileHeader?: (header: FileHeaderMessage) => Promise<boolean> | boolean;

  constructor(
    private channel: RTCDataChannel,
    options: FileStreamReceiverOptions = {}
  ) {
    this.options = options;
    this.onProgress = options.onProgress;
    this.onFileHeader = options.onFileHeader;
  }

  receiveFile(): Promise<ReceivedFileResult> {
    return new Promise((resolve, reject) => {
      const reassembler = new FrameReassembler();
      const hasher = new Sha256Stream();
      let header: FileHeaderMessage | null = null;
      let safeName: string = '';
      let receivedBytes = 0;
      let expectedOffset = 0n;
      let expectedChunkIndex = 0;
      const chunks: Uint8Array[] = [];

      let settled = false;
      const pendingChunkFrames: ReassembledFrame[] = [];

      enum ReceiverPhase {
        AwaitingHeader,
        AwaitingResumeAck,
        AwaitingUserConfirm,
        StreamingChunks,
      }

      let phase = ReceiverPhase.AwaitingHeader;
      let resumeTimer: any = null;

      const cleanup = () => {
        if (resumeTimer) clearTimeout(resumeTimer);
        this.channel.removeEventListener('message', onMessage);
        this.channel.removeEventListener('close', onClose);
        this.channel.removeEventListener('error', onError);
      };

      const onClose = () => {
        if (!settled) {
          settled = true;
          cleanup();
          reject(new Error('RTCDataChannel closed during file receive'));
        }
      };

      const onError = () => {
        if (!settled) {
          settled = true;
          cleanup();
          reject(new Error('RTCDataChannel error during file receive'));
        }
      };

      const processChunkFrame = (frame: ReassembledFrame) => {
        if (!header) {
          throw new Error('Received ChunkData without prior FileHeader');
        }

        const chunk = ChunkDataMessage.deserialize(frame.payload);

        // DEF-08: Validate chunk sequence index and byte offset
        if (chunk.chunkIndex !== expectedChunkIndex || chunk.offset !== expectedOffset) {
          const ack = new TransferAckMessage();
          ack.status = AckStatus.GenericError;
          ack.receivedBytes = expectedOffset;
          ack.message = `Chunk sequence mismatch: expected index ${expectedChunkIndex} at offset ${expectedOffset}, got index ${chunk.chunkIndex} at offset ${chunk.offset}`;
          (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
          settled = true;
          cleanup();
          reject(new Error(ack.message));
          return;
        }

        // Validate chunk CRC-32 if present
        if (chunk.chunkCrc32 !== 0) {
          const actualCrc = computeCrc32(chunk.data);
          if (actualCrc !== chunk.chunkCrc32) {
            const ack = new TransferAckMessage();
            ack.status = AckStatus.HashMismatch;
            ack.receivedBytes = expectedOffset;
            ack.message = `Chunk CRC-32 mismatch on chunk ${chunk.chunkIndex}`;
            (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
            settled = true;
            cleanup();
            reject(new Error(ack.message));
            return;
          }
        }

        hasher.update(chunk.data);
        chunks.push(chunk.data);
        receivedBytes += chunk.data.length;
        expectedChunkIndex++;
        expectedOffset += BigInt(chunk.data.length);

        if (this.onProgress) {
          this.onProgress(receivedBytes, Number(header.fileSize));
        }

        // Check completion
        if (receivedBytes >= Number(header.fileSize)) {
          completeTransfer();
        }
      };

      const completeTransfer = () => {
        if (!header || settled) return;
        settled = true;
        cleanup();

        // DEF-01: Verify SHA-256 using streaming hasher (no huge array allocation)
        const actualSha = hasher.digestHex();
        if (header.sha256Hex && actualSha.toLowerCase() !== header.sha256Hex.toLowerCase()) {
          const ack = new TransferAckMessage();
          ack.status = AckStatus.HashMismatch;
          ack.receivedBytes = BigInt(receivedBytes);
          ack.message = `SHA-256 mismatch: expected ${header.sha256Hex}, got ${actualSha}`;
          (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));
          reject(new Error(ack.message));
          return;
        }

        // Success ACK
        const ack = new TransferAckMessage();
        ack.status = AckStatus.Success;
        ack.receivedBytes = BigInt(receivedBytes);
        ack.message = 'File verified and saved successfully';
        (this.channel as any).send(buildFrame(MessageType.TransferAck, ack.serialize()));

        // DEF-01: Construct Blob directly from chunk array without intermediate contiguous buffer
        const blob = new Blob(chunks as BlobPart[]);
        resolve({
          fileName: safeName,
          size: receivedBytes,
          blob,
          sha256Hex: actualSha,
        });
      };

      const proceedAfterResumeResolution = async (resumed: boolean, acceptedOffset?: bigint) => {
        if (settled) return;
        if (resumed && acceptedOffset !== undefined) {
          const offsetNum = Number(acceptedOffset);
          receivedBytes = offsetNum;
          expectedOffset = acceptedOffset;
          expectedChunkIndex = Math.floor(offsetNum / header!.chunkSize);
          if (this.options.resumeContext?.partialChunks) {
            for (const ch of this.options.resumeContext.partialChunks) {
              hasher.update(ch);
              chunks.push(ch);
            }
          }
        } else {
          receivedBytes = 0;
          expectedOffset = 0n;
          expectedChunkIndex = 0;
        }

        // 2. User confirmation callback with DEF-05 race protection
        if (this.onFileHeader) {
          phase = ReceiverPhase.AwaitingUserConfirm;
          try {
            const accepted = await this.onFileHeader(header!);
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

        // DEF-02: Support 0-byte file without deadlock
        if (header!.fileSize === 0n) {
          completeTransfer();
          return;
        }

        phase = ReceiverPhase.StreamingChunks;

        // Drain chunks buffered while confirmation or resumption was pending (DEF-05 & Finding 3)
        while (pendingChunkFrames.length > 0) {
          const queued = pendingChunkFrames.shift()!;
          processChunkFrame(queued);
          if (settled) return;
        }
      };

      const onMessage = async (event: MessageEvent) => {
        try {
          const raw = new Uint8Array(event.data as ArrayBuffer);
          const frames = reassembler.push(raw);

          for (const frame of frames) {
            if (frame.header.msgType === MessageType.FileHeader) {
              if (phase !== ReceiverPhase.AwaitingHeader) {
                continue;
              }
              header = FileHeaderMessage.deserialize(frame.payload);

              // 1. Sanitize file name (DEF-11)
              try {
                safeName = sanitizeFileName(header.fileName);
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

              // Milestone 3D: Resumption Negotiation
              const canResume =
                Boolean(this.options.resumeContext) &&
                (header.flags & FileHeaderFlags.RESUME_SUPPORTED) !== 0 &&
                this.options.resumeContext!.fileHash.toLowerCase() === header.sha256Hex.toLowerCase() &&
                this.options.resumeContext!.bytesReceivedSoFar > 0 &&
                this.options.resumeContext!.bytesReceivedSoFar < Number(header.fileSize);

              if (canResume) {
                // Align to chunkSize boundary (ADR-017)
                const alignedOffset =
                  Math.floor(this.options.resumeContext!.bytesReceivedSoFar / header.chunkSize) * header.chunkSize;

                if (alignedOffset > 0) {
                  const resumeReq = new ResumeRequestMessage();
                  resumeReq.fileHash = header.sha256Hex;
                  resumeReq.bytesReceivedSoFar = BigInt(alignedOffset);
                  resumeReq.partialSha256 = this.options.resumeContext!.partialSha256;
                  (this.channel as any).send(buildFrame(MessageType.ResumeRequest, resumeReq.serialize()));

                  phase = ReceiverPhase.AwaitingResumeAck;
                  resumeTimer = setTimeout(() => {
                    if (phase === ReceiverPhase.AwaitingResumeAck) {
                      resumeTimer = null;
                      proceedAfterResumeResolution(false);
                    }
                  }, 15000);
                  continue;
                }
              }

              // Clean transfer without resumption (No phantom TransferAck!)
              await proceedAfterResumeResolution(false);

            } else if (frame.header.msgType === MessageType.TransferAck) {
              if (phase === ReceiverPhase.AwaitingResumeAck) {
                if (resumeTimer) {
                  clearTimeout(resumeTimer);
                  resumeTimer = null;
                }
                const ack = TransferAckMessage.deserialize(frame.payload);
                if (ack.status === AckStatus.ResumeAccepted) {
                  const alignedOffset =
                    Math.floor(this.options.resumeContext!.bytesReceivedSoFar / header!.chunkSize) * header!.chunkSize;
                  let validResume = ack.receivedBytes === BigInt(alignedOffset);

                  // Validate partial chunks total bytes match alignedOffset if provided
                  if (validResume && this.options.resumeContext?.partialChunks) {
                    let totalPartialBytes = 0;
                    for (const c of this.options.resumeContext.partialChunks) totalPartialBytes += c.length;
                    if (totalPartialBytes !== alignedOffset) {
                      validResume = false;
                    }
                  }

                  if (validResume) {
                    await proceedAfterResumeResolution(true, ack.receivedBytes);
                  } else {
                    await proceedAfterResumeResolution(false);
                  }
                } else {
                  // ResumeRejected or error: fallback to fresh transfer from offset 0
                  await proceedAfterResumeResolution(false);
                }
              }
            } else if (frame.header.msgType === MessageType.ChunkData) {
              if (
                phase === ReceiverPhase.AwaitingResumeAck ||
                phase === ReceiverPhase.AwaitingUserConfirm
              ) {
                // Buffer chunks while resumption or user confirmation is pending (DEF-05 & Finding 3)
                pendingChunkFrames.push(frame);
              } else if (phase === ReceiverPhase.StreamingChunks) {
                processChunkFrame(frame);
              }
            } else if (frame.header.msgType === MessageType.TransferCancel) {
              if (resumeTimer) clearTimeout(resumeTimer);
              settled = true;
              cleanup();
              reject(new Error('Transfer cancelled by sender'));
              return;
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
