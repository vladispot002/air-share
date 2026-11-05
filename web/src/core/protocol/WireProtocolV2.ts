import { computeCrc32 } from './Crc32';

export const MAGIC = new Uint8Array([0x4c, 0x4e, 0x44, 0x50]); // 'L', 'N', 'D', 'P'
export const MAGIC_U32 = 0x4c4e4450;
export const PROTOCOL_VERSION = 2;
export const FRAME_HEADER_SIZE = 16;
export const MAX_PAYLOAD_SIZE = 16 * 1024 * 1024; // 16 MB boundary protection
export const MAX_FILENAME_LENGTH = 255;

export enum MessageType {
  Handshake = 0x0001,
  FileHeader = 0x0002,
  ChunkData = 0x0003,
  TransferAck = 0x0004,
  TransferCancel = 0x0005,
  Ping = 0x0006,
  Pong = 0x0007,
  ResumeRequest = 0x0008,
}

export enum AckStatus {
  Success = 0x00,
  HashMismatch = 0x01,
  DiskFull = 0x02,
  Rejected = 0x03,
  PathUnsafe = 0x04,
  GenericError = 0x05,
  ResumeAccepted = 0x06,
  ResumeRejected = 0x07,
}

export const FileHeaderFlags = {
  NONE: 0x0000,
  RESUME_SUPPORTED: 0x0001,
} as const;

export function isProtocolV2(first4Bytes: Uint8Array): boolean {
  if (first4Bytes.length < 4) return false;
  return (
    first4Bytes[0] === MAGIC[0] &&
    first4Bytes[1] === MAGIC[1] &&
    first4Bytes[2] === MAGIC[2] &&
    first4Bytes[3] === MAGIC[3]
  );
}

export class FrameHeader {
  magic: Uint8Array = new Uint8Array([0x4c, 0x4e, 0x44, 0x50]);
  version: number = PROTOCOL_VERSION;
  msgType: MessageType = MessageType.Ping;
  payloadLength: number = 0;
  crc32OrSeq: number = 0;

  serialize(): Uint8Array {
    const buf = new Uint8Array(FRAME_HEADER_SIZE);
    buf.set(this.magic, 0);
    const view = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
    view.setUint16(4, this.version, false);
    view.setUint16(6, this.msgType, false);
    view.setUint32(8, this.payloadLength, false);
    view.setUint32(12, this.crc32OrSeq, false);
    return buf;
  }

  static deserialize(bytes: Uint8Array): FrameHeader {
    if (bytes.length < FRAME_HEADER_SIZE) {
      throw new Error(`Buffer underflow reading FrameHeader: expected ${FRAME_HEADER_SIZE} bytes, got ${bytes.length}`);
    }
    if (!isProtocolV2(bytes.subarray(0, 4))) {
      throw new Error('Invalid Protocol v2 magic header');
    }
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const version = view.getUint16(4, false);
    if (version !== PROTOCOL_VERSION) {
      throw new Error(`Unsupported protocol version: ${version}`);
    }
    const msgType = view.getUint16(6, false) as MessageType;
    const payloadLength = view.getUint32(8, false);
    if (payloadLength > MAX_PAYLOAD_SIZE) {
      throw new Error(`Payload exceeds maximum allowed frame size: ${payloadLength}`);
    }
    const crc32OrSeq = view.getUint32(12, false);

    const hdr = new FrameHeader();
    hdr.magic = bytes.slice(0, 4);
    hdr.version = version;
    hdr.msgType = msgType;
    hdr.payloadLength = payloadLength;
    hdr.crc32OrSeq = crc32OrSeq;
    return hdr;
  }
}

// HandshakeMessage (0x0001)
export class HandshakeMessage {
  capabilitiesFlags: number = 0;
  deviceId: string = '';
  deviceName: string = '';
  clientVersion: string = '';

  serialize(): Uint8Array {
    const enc = new TextEncoder();
    const idBytes = enc.encode(this.deviceId);
    const nameBytes = enc.encode(this.deviceName);
    const verBytes = enc.encode(this.clientVersion);

    const total = 4 + 2 + idBytes.length + 2 + nameBytes.length + 2 + verBytes.length;
    const buf = new Uint8Array(total);
    const view = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);

    let offset = 0;
    view.setUint32(offset, this.capabilitiesFlags, false);
    offset += 4;

    view.setUint16(offset, idBytes.length, false);
    offset += 2;
    buf.set(idBytes, offset);
    offset += idBytes.length;

    view.setUint16(offset, nameBytes.length, false);
    offset += 2;
    buf.set(nameBytes, offset);
    offset += nameBytes.length;

    view.setUint16(offset, verBytes.length, false);
    offset += 2;
    buf.set(verBytes, offset);

    return buf;
  }

  static deserialize(data: Uint8Array): HandshakeMessage {
    if (!data || data.length < 4 + 2 + 2 + 2) {
      throw new Error('Buffer underflow parsing HandshakeMessage');
    }
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const dec = new TextDecoder();
    let offset = 0;

    const capabilitiesFlags = view.getUint32(offset, false);
    offset += 4;

    const readString = (): string => {
      if (offset + 2 > data.length) throw new Error('Truncated string field in HandshakeMessage');
      const len = view.getUint16(offset, false);
      offset += 2;
      if (offset + len > data.length) throw new Error('Truncated string field in HandshakeMessage');
      const str = dec.decode(data.subarray(offset, offset + len));
      offset += len;
      return str;
    };

    const deviceId = readString();
    const deviceName = readString();
    const clientVersion = readString();

    const msg = new HandshakeMessage();
    msg.capabilitiesFlags = capabilitiesFlags;
    msg.deviceId = deviceId;
    msg.deviceName = deviceName;
    msg.clientVersion = clientVersion;
    return msg;
  }
}

// FileHeaderMessage (0x0002)
export class FileHeaderMessage {
  fileSize: bigint = 0n;
  chunkSize: number = 64 * 1024;
  flags: number = 0;
  fileName: string = '';
  sha256Hex: string = ''; // 64-character hex digest

  serialize(): Uint8Array {
    const enc = new TextEncoder();
    const nameBytes = enc.encode(this.fileName);
    if (nameBytes.length === 0 || nameBytes.length > MAX_FILENAME_LENGTH) {
      throw new Error(`Invalid filename length: ${nameBytes.length} (max ${MAX_FILENAME_LENGTH})`);
    }

    const shaBytes = enc.encode(this.sha256Hex.padEnd(64, '0').slice(0, 64));

    const total = 8 + 4 + 2 + 2 + nameBytes.length + 64;
    const buf = new Uint8Array(total);
    const view = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);

    let offset = 0;
    view.setBigUint64(offset, this.fileSize, false);
    offset += 8;

    view.setUint32(offset, this.chunkSize, false);
    offset += 4;

    view.setUint16(offset, this.flags, false);
    offset += 2;

    view.setUint16(offset, nameBytes.length, false);
    offset += 2;
    buf.set(nameBytes, offset);
    offset += nameBytes.length;

    buf.set(shaBytes, offset);

    return buf;
  }

  static deserialize(data: Uint8Array): FileHeaderMessage {
    const minSize = 8 + 4 + 2 + 2 + 64;
    if (!data || data.length < minSize) {
      throw new Error('Buffer underflow parsing FileHeaderMessage');
    }
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const dec = new TextDecoder();
    let offset = 0;

    const fileSize = view.getBigUint64(offset, false);
    offset += 8;

    const chunkSize = view.getUint32(offset, false);
    offset += 4;

    const flags = view.getUint16(offset, false);
    offset += 2;

    const nameLen = view.getUint16(offset, false);
    offset += 2;

    if (nameLen === 0 || nameLen > MAX_FILENAME_LENGTH) {
      throw new Error(`Invalid filename length in FileHeaderMessage: ${nameLen}`);
    }

    if (offset + nameLen + 64 > data.length) {
      throw new Error('Truncated payload in FileHeaderMessage');
    }

    const fileName = dec.decode(data.subarray(offset, offset + nameLen));
    offset += nameLen;

    const sha256Hex = dec.decode(data.subarray(offset, offset + 64));

    const msg = new FileHeaderMessage();
    msg.fileSize = fileSize;
    msg.chunkSize = chunkSize;
    msg.flags = flags;
    msg.fileName = fileName;
    msg.sha256Hex = sha256Hex;
    return msg;
  }
}

// ChunkDataMessage (0x0003)
export class ChunkDataMessage {
  offset: bigint = 0n;
  chunkIndex: number = 0;
  chunkCrc32: number = 0;
  data: Uint8Array = new Uint8Array(0);

  serialize(): Uint8Array {
    const crc = this.chunkCrc32 !== 0 ? this.chunkCrc32 : computeCrc32(this.data);
    const total = 8 + 4 + 4 + this.data.length;
    const buf = new Uint8Array(total);
    const view = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);

    let offset = 0;
    view.setBigUint64(offset, this.offset, false);
    offset += 8;

    view.setUint32(offset, this.chunkIndex, false);
    offset += 4;

    view.setUint32(offset, crc, false);
    offset += 4;

    if (this.data.length > 0) {
      buf.set(this.data, offset);
    }

    return buf;
  }

  static deserialize(data: Uint8Array): ChunkDataMessage {
    const minSize = 8 + 4 + 4;
    if (!data || data.length < minSize) {
      throw new Error('Buffer underflow parsing ChunkDataMessage');
    }
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    let offset = 0;

    const msgOffset = view.getBigUint64(offset, false);
    offset += 8;

    const chunkIndex = view.getUint32(offset, false);
    offset += 4;

    const chunkCrc32 = view.getUint32(offset, false);
    offset += 4;

    const chunkData = data.slice(offset);
    if (chunkData.length > 0 && chunkCrc32 !== 0) {
      const actualCrc = computeCrc32(chunkData);
      if (actualCrc !== chunkCrc32) {
        throw new Error(`Chunk CRC-32 checksum mismatch: expected ${chunkCrc32}, got ${actualCrc}`);
      }
    }

    const msg = new ChunkDataMessage();
    msg.offset = msgOffset;
    msg.chunkIndex = chunkIndex;
    msg.chunkCrc32 = chunkCrc32;
    msg.data = chunkData;
    return msg;
  }
}

// TransferAckMessage (0x0004)
export class TransferAckMessage {
  status: AckStatus = AckStatus.Success;
  receivedBytes: bigint = 0n;
  message: string = '';

  serialize(): Uint8Array {
    const enc = new TextEncoder();
    const msgBytes = enc.encode(this.message);
    const total = 1 + 8 + 2 + msgBytes.length;
    const buf = new Uint8Array(total);
    const view = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);

    let offset = 0;
    view.setUint8(offset, this.status);
    offset += 1;

    view.setBigUint64(offset, this.receivedBytes, false);
    offset += 8;

    view.setUint16(offset, msgBytes.length, false);
    offset += 2;

    if (msgBytes.length > 0) {
      buf.set(msgBytes, offset);
    }

    return buf;
  }

  static deserialize(data: Uint8Array): TransferAckMessage {
    const minSize = 1 + 8 + 2;
    if (!data || data.length < minSize) {
      throw new Error('Buffer underflow parsing TransferAckMessage');
    }
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const dec = new TextDecoder();
    let offset = 0;

    const status = view.getUint8(offset) as AckStatus;
    offset += 1;

    const receivedBytes = view.getBigUint64(offset, false);
    offset += 8;

    const msgLen = view.getUint16(offset, false);
    offset += 2;

    if (offset + msgLen > data.length) {
      throw new Error('Truncated message in TransferAckMessage');
    }

    const message = dec.decode(data.subarray(offset, offset + msgLen));

    const msg = new TransferAckMessage();
    msg.status = status;
    msg.receivedBytes = receivedBytes;
    msg.message = message;
    return msg;
  }
}

// TransferCancelMessage (0x0005)
export class TransferCancelMessage {
  errorCode: number = 0;
  reason: string = '';

  serialize(): Uint8Array {
    const enc = new TextEncoder();
    const reasonBytes = enc.encode(this.reason);
    const total = 4 + 2 + reasonBytes.length;
    const buf = new Uint8Array(total);
    const view = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);

    let offset = 0;
    view.setUint32(offset, this.errorCode, false);
    offset += 4;

    view.setUint16(offset, reasonBytes.length, false);
    offset += 2;

    if (reasonBytes.length > 0) {
      buf.set(reasonBytes, offset);
    }

    return buf;
  }

  static deserialize(data: Uint8Array): TransferCancelMessage {
    const minSize = 4 + 2;
    if (!data || data.length < minSize) {
      throw new Error('Buffer underflow parsing TransferCancelMessage');
    }
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const dec = new TextDecoder();
    let offset = 0;

    const errorCode = view.getUint32(offset, false);
    offset += 4;

    const reasonLen = view.getUint16(offset, false);
    offset += 2;

    if (offset + reasonLen > data.length) {
      throw new Error('Truncated reason in TransferCancelMessage');
    }

    const reason = dec.decode(data.subarray(offset, offset + reasonLen));

    const msg = new TransferCancelMessage();
    msg.errorCode = errorCode;
    msg.reason = reason;
    return msg;
  }
}

// ResumeRequestMessage (0x0008, fixed 136 bytes)
export class ResumeRequestMessage {
  fileHash: string = '';
  bytesReceivedSoFar: bigint = 0n;
  partialSha256: string = '';

  serialize(): Uint8Array {
    const buf = new Uint8Array(136);
    const view = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
    const enc = new TextEncoder();

    buf.set(enc.encode(this.fileHash.padEnd(64, '0').slice(0, 64)), 0);
    view.setBigUint64(64, this.bytesReceivedSoFar, false);
    buf.set(enc.encode(this.partialSha256.padEnd(64, '0').slice(0, 64)), 72);
    return buf;
  }

  static deserialize(data: Uint8Array): ResumeRequestMessage {
    if (!data || data.length < 136) {
      throw new Error(`Buffer underflow parsing ResumeRequestMessage: expected 136 bytes, got ${data ? data.length : 0}`);
    }
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const dec = new TextDecoder();

    const msg = new ResumeRequestMessage();
    msg.fileHash = dec.decode(data.subarray(0, 64));
    msg.bytesReceivedSoFar = view.getBigUint64(64, false);
    msg.partialSha256 = dec.decode(data.subarray(72, 136));
    return msg;
  }
}

export function buildFrame(
  type: MessageType,
  payload: Uint8Array = new Uint8Array(0),
  crcOrSeq: number = 0
): Uint8Array {
  const crc = crcOrSeq !== 0 ? crcOrSeq : (payload.length > 0 ? computeCrc32(payload) : 0);
  const hdr = new FrameHeader();
  hdr.version = PROTOCOL_VERSION;
  hdr.msgType = type;
  hdr.payloadLength = payload.length;
  hdr.crc32OrSeq = crc;

  const frame = new Uint8Array(FRAME_HEADER_SIZE + payload.length);
  frame.set(hdr.serialize(), 0);
  if (payload.length > 0) {
    frame.set(payload, FRAME_HEADER_SIZE);
  }
  return frame;
}

export interface ReassembledFrame {
  header: FrameHeader;
  payload: Uint8Array;
}

export class FrameReassembler {
  private buffer: Uint8Array = new Uint8Array(0);

  /**
   * Pushes incoming raw bytes into the reassembler buffer.
   * Returns an array of zero or more completely parsed and CRC-verified frames.
   */
  push(incoming: Uint8Array): ReassembledFrame[] {
    if (incoming.length === 0) return [];

    if (this.buffer.length === 0) {
      this.buffer = incoming;
    } else {
      const combined = new Uint8Array(this.buffer.length + incoming.length);
      combined.set(this.buffer, 0);
      combined.set(incoming, this.buffer.length);
      this.buffer = combined;
    }

    const frames: ReassembledFrame[] = [];

    while (this.buffer.length >= FRAME_HEADER_SIZE) {
      if (!isProtocolV2(this.buffer.subarray(0, 4))) {
        let resyncIdx = -1;
        for (let i = 1; i <= this.buffer.length - 4; i++) {
          if (isProtocolV2(this.buffer.subarray(i, i + 4))) {
            resyncIdx = i;
            break;
          }
        }
        if (resyncIdx !== -1) {
          this.buffer = this.buffer.slice(resyncIdx);
          continue;
        } else {
          this.buffer = this.buffer.slice(Math.max(0, this.buffer.length - 3));
          break;
        }
      }

      let header: FrameHeader;
      try {
        header = FrameHeader.deserialize(this.buffer.subarray(0, FRAME_HEADER_SIZE));
      } catch {
        // Discard 4 magic bytes to advance and search for the next valid frame
        this.buffer = this.buffer.slice(4);
        continue;
      }

      const frameTotalSize = FRAME_HEADER_SIZE + header.payloadLength;

      if (this.buffer.length < frameTotalSize) {
        break;
      }

      const payload = this.buffer.slice(FRAME_HEADER_SIZE, frameTotalSize);

      if (header.payloadLength > 0 && header.crc32OrSeq !== 0) {
        const actualCrc = computeCrc32(payload);
        if (actualCrc !== header.crc32OrSeq) {
          this.buffer = this.buffer.slice(frameTotalSize);
          throw new Error(
            `Frame payload CRC-32 mismatch for msgType 0x${header.msgType.toString(16)}: expected ${header.crc32OrSeq}, got ${actualCrc}`
          );
        }
      }

      frames.push({ header, payload });
      this.buffer = this.buffer.slice(frameTotalSize);
    }

    return frames;
  }

  reset(): void {
    this.buffer = new Uint8Array(0);
  }

  get bufferedBytes(): number {
    return this.buffer.length;
  }
}
