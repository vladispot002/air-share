// Zero-dependency ISO/IEC 18004 QR Code Matrix Generator in TypeScript
// Parity with C++ landrop::ui::QrCodeGenerator

export enum QrEccLevel {
  Low = 0,
  Medium = 1,
  Quartile = 2,
  High = 3,
}

// Galois Field GF(256) tables with primitive polynomial 0x11D
const GF_EXP = new Uint8Array(256);
const GF_LOG = new Uint8Array(256);

(() => {
  let val = 1;
  for (let i = 0; i < 255; i++) {
    GF_EXP[i] = val;
    GF_LOG[val] = i;
    let next = val << 1;
    if (next & 0x100) next ^= 0x11d;
    val = next & 0xff;
  }
  GF_EXP[255] = GF_EXP[0];
  GF_LOG[0] = 0;
})();

function gfMul(x: number, y: number): number {
  if (x === 0 || y === 0) return 0;
  return GF_EXP[(GF_LOG[x] + GF_LOG[y]) % 255];
}

function rsComputeGenerator(degree: number): Uint8Array {
  const g = new Uint8Array(degree + 1);
  g[degree] = 1;

  for (let i = 0; i < degree; i++) {
    const factor = GF_EXP[i];
    for (let j = 0; j < g.length - 1; j++) {
      g[j] = gfMul(g[j], factor) ^ g[j + 1];
    }
    g[g.length - 1] = gfMul(g[g.length - 1], factor);
  }
  return g;
}

function rsComputeRemainder(data: Uint8Array, degree: number): Uint8Array {
  const gen = rsComputeGenerator(degree);
  const rem = new Uint8Array(degree);

  for (let i = 0; i < data.length; i++) {
    const factor = data[i] ^ rem[0];
    for (let j = 0; j < degree - 1; j++) {
      rem[j] = rem[j + 1] ^ gfMul(gen[j], factor);
    }
    rem[degree - 1] = gfMul(gen[degree - 1], factor);
  }
  return rem;
}

interface VersionInfo {
  totalCodewords: number;
  ecCodewordsPerBlock: number;
  numBlocksGroup1: number;
  dataCodewordsGroup1: number;
  numBlocksGroup2: number;
  dataCodewordsGroup2: number;
}

// ISO 18004 Table 9 for ECC Level Medium (Versions 1..10)
const VERSION_INFO_M: VersionInfo[] = [
  { totalCodewords: 26, ecCodewordsPerBlock: 10, numBlocksGroup1: 1, dataCodewordsGroup1: 16, numBlocksGroup2: 0, dataCodewordsGroup2: 0 },
  { totalCodewords: 44, ecCodewordsPerBlock: 16, numBlocksGroup1: 1, dataCodewordsGroup1: 28, numBlocksGroup2: 0, dataCodewordsGroup2: 0 },
  { totalCodewords: 70, ecCodewordsPerBlock: 26, numBlocksGroup1: 1, dataCodewordsGroup1: 44, numBlocksGroup2: 0, dataCodewordsGroup2: 0 },
  { totalCodewords: 100, ecCodewordsPerBlock: 18, numBlocksGroup1: 2, dataCodewordsGroup1: 32, numBlocksGroup2: 0, dataCodewordsGroup2: 0 },
  { totalCodewords: 134, ecCodewordsPerBlock: 24, numBlocksGroup1: 2, dataCodewordsGroup1: 43, numBlocksGroup2: 0, dataCodewordsGroup2: 0 },
  { totalCodewords: 172, ecCodewordsPerBlock: 16, numBlocksGroup1: 4, dataCodewordsGroup1: 27, numBlocksGroup2: 0, dataCodewordsGroup2: 0 },
  { totalCodewords: 196, ecCodewordsPerBlock: 18, numBlocksGroup1: 4, dataCodewordsGroup1: 31, numBlocksGroup2: 0, dataCodewordsGroup2: 0 },
  { totalCodewords: 242, ecCodewordsPerBlock: 22, numBlocksGroup1: 2, dataCodewordsGroup1: 38, numBlocksGroup2: 2, dataCodewordsGroup2: 39 },
  { totalCodewords: 292, ecCodewordsPerBlock: 22, numBlocksGroup1: 3, dataCodewordsGroup1: 36, numBlocksGroup2: 2, dataCodewordsGroup2: 37 },
  { totalCodewords: 346, ecCodewordsPerBlock: 26, numBlocksGroup1: 4, dataCodewordsGroup1: 43, numBlocksGroup2: 1, dataCodewordsGroup2: 44 },
];

const ALIGN_POS: number[][] = [
  [],
  [],
  [6, 18],
  [6, 22],
  [6, 26],
  [6, 30],
  [6, 34],
  [6, 22, 38],
  [6, 24, 42],
  [6, 26, 46],
  [6, 28, 50],
];

// ISO/IEC 18004 Table 13: 18-bit Version Information (BCH(18, 6)) for Versions 7..10
const VERSION_INFO_BITS = [
  0, 0, 0, 0, 0, 0, 0,
  0x07c94, // Version 7
  0x085bc, // Version 8
  0x09a99, // Version 9
  0x0a4d3, // Version 10
];

class BitBuffer {
  bits: boolean[] = [];

  appendBits(val: number, len: number): void {
    for (let i = len - 1; i >= 0; i--) {
      this.bits.push(((val >>> i) & 1) !== 0);
    }
  }

  toBytes(): Uint8Array {
    const bytes = new Uint8Array(Math.ceil(this.bits.length / 8));
    for (let i = 0; i < this.bits.length; i++) {
      if (this.bits[i]) {
        bytes[Math.floor(i / 8)] |= 1 << (7 - (i % 8));
      }
    }
    return bytes;
  }
}

export class QrCode {
  readonly version: number;
  readonly size: number;
  readonly ecc: QrEccLevel;
  private modules: boolean[][];

  private constructor(version: number, ecc: QrEccLevel, dataCodewords: Uint8Array) {
    this.version = version;
    this.size = version * 4 + 17;
    this.ecc = ecc;
    this.modules = Array.from({ length: this.size }, () => new Array(this.size).fill(false));
    const isFunction = Array.from({ length: this.size }, () => new Array(this.size).fill(false));

    const setFunction = (x: number, y: number, val: boolean) => {
      if (x >= 0 && x < this.size && y >= 0 && y < this.size) {
        this.modules[y][x] = val;
        isFunction[y][x] = true;
      }
    };

    // Draw finder patterns
    const drawFinder = (ox: number, oy: number) => {
      for (let y = -1; y <= 7; y++) {
        for (let x = -1; x <= 7; x++) {
          const px = ox + x;
          const py = oy + y;
          if (px < 0 || px >= this.size || py < 0 || py >= this.size) continue;
          const isBorder = (x === 0 || x === 6 || y === 0 || y === 6);
          const isCenter = (x >= 2 && x <= 4 && y >= 2 && y <= 4);
          const dark = (x >= 0 && x <= 6 && y >= 0 && y <= 6) && (isBorder || isCenter);
          setFunction(px, py, dark);
        }
      }
    };

    drawFinder(0, 0);
    drawFinder(this.size - 7, 0);
    drawFinder(0, this.size - 7);

    // Timing patterns (ISO/IEC 18004:2015 Section 6.3.5: strictly between separators from 8 to size - 9)
    for (let i = 8; i <= this.size - 9; i++) {
      setFunction(i, 6, i % 2 === 0);
      setFunction(6, i, i % 2 === 0);
    }

    // Alignment patterns
    const align = ALIGN_POS[version] || [];
    for (let i = 0; i < align.length; i++) {
      for (let j = 0; j < align.length; j++) {
        const cx = align[i];
        const cy = align[j];
        if ((cx <= 8 && cy <= 8) || (cx >= this.size - 9 && cy <= 8) || (cx <= 8 && cy >= this.size - 9)) {
          continue;
        }
        for (let dy = -2; dy <= 2; dy++) {
          for (let dx = -2; dx <= 2; dx++) {
            const dark = Math.max(Math.abs(dx), Math.abs(dy)) !== 1;
            setFunction(cx + dx, cy + dy, dark);
          }
        }
      }
    }

    // Reserve format bits (skipping row 6 and col 6 which belong to timing patterns)
    for (let i = 0; i <= 8; i++) {
      if (i !== 6) {
        setFunction(8, i, false);
        setFunction(i, 8, false);
      }
    }
    // Column 8 format bits reservation: rows (this.size - 7) through (this.size - 1)
    // Note: row (this.size - 8) is the Dark Module, NOT format information
    for (let i = this.size - 7; i < this.size; i++) {
      setFunction(8, i, false);
    }
    // Row 8 format bits reservation: columns (this.size - 8) through (this.size - 1)
    for (let i = this.size - 8; i < this.size; i++) {
      setFunction(i, 8, false);
    }

    // Dark module (ISO/IEC 18004:2015 Section 6.3.6: module at (8, size - 8) is ALWAYS dark)
    setFunction(8, 4 * version + 9, true);

    // Version Information for Versions 7..10 (ISO/IEC 18004:2015 Section 6.5.2)
    if (version >= 7) {
      const vBits = VERSION_INFO_BITS[version];
      for (let k = 0; k < 18; k++) {
        const bit = ((vBits >>> k) & 1) !== 0;
        // Bottom-left block: 6 cols x 3 rows at (0..5, size - 11 .. size - 9)
        setFunction(Math.floor(k / 3), this.size - 11 + (k % 3), bit);
        // Top-right block: 3 cols x 6 rows at (size - 11 .. size - 9, 0..5)
        setFunction(this.size - 11 + (k % 3), Math.floor(k / 3), bit);
      }
    }

    // Interleave blocks
    const vi = VERSION_INFO_M[version - 1];
    const numBlocks = vi.numBlocksGroup1 + vi.numBlocksGroup2;
    const dataBlocks: Uint8Array[] = [];
    const ecBlocks: Uint8Array[] = [];

    let offset = 0;
    for (let b = 0; b < numBlocks; b++) {
      const dcCount = b < vi.numBlocksGroup1 ? vi.dataCodewordsGroup1 : vi.dataCodewordsGroup2;
      const blockData = dataCodewords.slice(offset, offset + dcCount);
      offset += dcCount;
      dataBlocks.push(blockData);
      ecBlocks.push(rsComputeRemainder(blockData, vi.ecCodewordsPerBlock));
    }

    const finalSequence: number[] = [];
    const maxDataCodewords = Math.max(vi.dataCodewordsGroup1, vi.dataCodewordsGroup2);
    for (let i = 0; i < maxDataCodewords; i++) {
      for (let b = 0; b < numBlocks; b++) {
        if (i < dataBlocks[b].length) {
          finalSequence.push(dataBlocks[b][i]);
        }
      }
    }
    for (let i = 0; i < vi.ecCodewordsPerBlock; i++) {
      for (let b = 0; b < numBlocks; b++) {
        finalSequence.push(ecBlocks[b][i]);
      }
    }

    // Matrix placement
    const finalBits = new BitBuffer();
    for (const byte of finalSequence) {
      finalBits.appendBits(byte, 8);
    }
    const REMAINDER_BITS = [0, 0, 7, 7, 7, 7, 7, 0, 0, 0, 0];
    finalBits.appendBits(0, REMAINDER_BITS[version] || 0);

    let bitIdx = 0;
    for (let right = this.size - 1; right > 0; right -= 2) {
      if (right === 6) right--;

      for (let vert = 0; vert < this.size; vert++) {
        for (let j = 0; j < 2; j++) {
          const x = right - j;
          const upwards = ((right + 1) & 2) === 0;
          const y = upwards ? this.size - 1 - vert : vert;

          if (!isFunction[y][x]) {
            let val = false;
            if (bitIdx < finalBits.bits.length) {
              val = finalBits.bits[bitIdx++];
            }
            this.modules[y][x] = val;
          }
        }
      }
    }

    // Apply mask 0
    for (let y = 0; y < this.size; y++) {
      for (let x = 0; x < this.size; x++) {
        if (isFunction[y][x]) continue;
        if ((x + y) % 2 === 0) {
          this.modules[y][x] = !this.modules[y][x];
        }
      }
    }

    // Format info bits for Medium ECC, Mask 0 (0x5412)
    const formatBits = 0x5412;
    for (let i = 0; i <= 5; i++) setFunction(8, i, ((formatBits >>> (14 - i)) & 1) !== 0);
    setFunction(8, 7, ((formatBits >>> 8) & 1) !== 0);
    setFunction(8, 8, ((formatBits >>> 7) & 1) !== 0);
    setFunction(7, 8, ((formatBits >>> 6) & 1) !== 0);
    for (let i = 9; i <= 14; i++) setFunction(14 - i, 8, ((formatBits >>> (14 - i)) & 1) !== 0);

    for (let i = 0; i <= 7; i++) setFunction(this.size - 1 - i, 8, ((formatBits >>> i) & 1) !== 0);
    for (let i = 8; i <= 14; i++) setFunction(8, this.size - 15 + i, ((formatBits >>> i) & 1) !== 0);
  }

  static encodeText(text: string, ecc: QrEccLevel = QrEccLevel.Medium): QrCode {
    const data = new TextEncoder().encode(text);
    return QrCode.encodeBinary(data, ecc);
  }

  static encodeBinary(data: Uint8Array, ecc: QrEccLevel = QrEccLevel.Medium): QrCode {
    let version = 1;
    for (; version <= 10; version++) {
      const vi = VERSION_INFO_M[version - 1];
      const totalData = vi.numBlocksGroup1 * vi.dataCodewordsGroup1 + vi.numBlocksGroup2 * vi.dataCodewordsGroup2;
      const charCountBits = version <= 9 ? 8 : 16;
      const neededBits = 4 + charCountBits + data.length * 8;
      if (Math.ceil(neededBits / 8) <= totalData) {
        break;
      }
    }

    if (version > 10) {
      throw new Error('Data exceeds maximum QR capacity for Versions 1-10 (max 213 bytes for Medium ECC)');
    }

    const vi = VERSION_INFO_M[version - 1];
    const totalDataCodewords = vi.numBlocksGroup1 * vi.dataCodewordsGroup1 + vi.numBlocksGroup2 * vi.dataCodewordsGroup2;

    const bb = new BitBuffer();
    // 8-bit Byte mode indicator: 0100
    bb.appendBits(0x04, 4);

    const charCountBits = version <= 9 ? 8 : 16;
    bb.appendBits(data.length, charCountBits);

    for (let i = 0; i < data.length; i++) {
      bb.appendBits(data[i], 8);
    }

    const remainingBits = totalDataCodewords * 8 - bb.bits.length;
    const termCount = Math.min(4, Math.max(0, remainingBits));
    bb.appendBits(0, termCount);

    if (bb.bits.length % 8 !== 0) {
      bb.appendBits(0, 8 - (bb.bits.length % 8));
    }

    const codewords = Array.from(bb.toBytes());
    const PAD = [0xec, 0x11];
    let padIdx = 0;
    while (codewords.length < totalDataCodewords) {
      codewords.push(PAD[padIdx]);
      padIdx ^= 1;
    }

    return new QrCode(version, ecc, new Uint8Array(codewords));
  }

  getModule(x: number, y: number): boolean {
    if (x >= 0 && x < this.size && y >= 0 && y < this.size) {
      return this.modules[y][x];
    }
    return false;
  }

  toSvgString(border = 4, fgHex = '#000000', bgHex = '#ffffff'): string {
    const total = this.size + 2 * border;
    let path = '';
    for (let y = 0; y < this.size; y++) {
      for (let x = 0; x < this.size; x++) {
        if (this.modules[y][x]) {
          path += `M${x + border},${y + border}h1v1h-1z `;
        }
      }
    }
    return `<svg xmlns="http://www.w3.org/2000/svg" version="1.1" viewBox="0 0 ${total} ${total}" stroke="none"><rect width="100%" height="100%" fill="${bgHex}"/><path fill="${fgHex}" d="${path}"/></svg>`;
  }

  renderToCanvas(canvas: HTMLCanvasElement, moduleSize = 8, border = 4): void {
    const totalModules = this.size + 2 * border;
    canvas.width = totalModules * moduleSize;
    canvas.height = totalModules * moduleSize;

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    ctx.fillStyle = '#000000';
    for (let y = 0; y < this.size; y++) {
      for (let x = 0; x < this.size; x++) {
        if (this.modules[y][x]) {
          ctx.fillRect((x + border) * moduleSize, (y + border) * moduleSize, moduleSize, moduleSize);
        }
      }
    }
  }
}
