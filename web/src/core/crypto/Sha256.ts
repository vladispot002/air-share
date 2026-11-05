// FIPS 180-4 standard SHA-256 implementation
// Zero external dependencies, constant memory streaming

const K = new Uint32Array([
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
]);

function rotr(x: number, n: number): number {
  return (x >>> n) | (x << (32 - n));
}

export class Sha256Stream {
  private h0 = 0x6a09e667;
  private h1 = 0xbb67ae85;
  private h2 = 0x3c6ef372;
  private h3 = 0xa54ff53a;
  private h4 = 0x510e527f;
  private h5 = 0x9b05688c;
  private h6 = 0x1f83d9ab;
  private h7 = 0x5be0cd19;

  private buffer = new Uint8Array(64);
  private bufferLength = 0;
  private totalBytes = 0n;
  private w = new Uint32Array(64);

  update(chunk: Uint8Array | string): this {
    const bytes = typeof chunk === 'string' ? new TextEncoder().encode(chunk) : chunk;
    this.totalBytes += BigInt(bytes.length);

    let offset = 0;
    while (offset < bytes.length) {
      if (this.bufferLength === 0 && bytes.length - offset >= 64) {
        // Fast path: direct process from input slice without intermediate buffer
        this.processBlock(bytes, offset);
        offset += 64;
      } else {
        const toCopy = Math.min(64 - this.bufferLength, bytes.length - offset);
        this.buffer.set(bytes.subarray(offset, offset + toCopy), this.bufferLength);
        this.bufferLength += toCopy;
        offset += toCopy;

        if (this.bufferLength === 64) {
          this.processBlock(this.buffer, 0);
          this.bufferLength = 0;
        }
      }
    }
    return this;
  }

  private processBlock(data: Uint8Array, offset: number): void {
    const w = this.w;
    const view = new DataView(data.buffer, data.byteOffset + offset, 64);

    for (let i = 0; i < 16; i++) {
      w[i] = view.getUint32(i * 4, false);
    }
    for (let i = 16; i < 64; i++) {
      const s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >>> 3);
      const s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >>> 10);
      w[i] = (w[i - 16] + s0 + w[i - 7] + s1) >>> 0;
    }

    let a = this.h0;
    let b = this.h1;
    let c = this.h2;
    let d = this.h3;
    let e = this.h4;
    let f = this.h5;
    let g = this.h6;
    let h = this.h7;

    for (let i = 0; i < 64; i++) {
      const s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const ch = (e & f) ^ (~e & g);
      const temp1 = (h + s1 + ch + K[i] + w[i]) >>> 0;
      const s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const maj = (a & b) ^ (a & c) ^ (b & c);
      const temp2 = (s0 + maj) >>> 0;

      h = g;
      g = f;
      f = e;
      e = (d + temp1) >>> 0;
      d = c;
      c = b;
      b = a;
      a = (temp1 + temp2) >>> 0;
    }

    this.h0 = (this.h0 + a) >>> 0;
    this.h1 = (this.h1 + b) >>> 0;
    this.h2 = (this.h2 + c) >>> 0;
    this.h3 = (this.h3 + d) >>> 0;
    this.h4 = (this.h4 + e) >>> 0;
    this.h5 = (this.h5 + f) >>> 0;
    this.h6 = (this.h6 + g) >>> 0;
    this.h7 = (this.h7 + h) >>> 0;
  }

  digest(): Uint8Array {
    // Save state in case multiple digests called or finished
    const totalBits = this.totalBytes * 8n;
    const finalBuffer = new Uint8Array(64);
    finalBuffer.set(this.buffer.subarray(0, this.bufferLength), 0);
    finalBuffer[this.bufferLength] = 0x80;

    if (this.bufferLength >= 56) {
      // Pad remaining with zeros and process block
      finalBuffer.fill(0, this.bufferLength + 1);
      this.processBlock(finalBuffer, 0);
      finalBuffer.fill(0);
    } else {
      finalBuffer.fill(0, this.bufferLength + 1);
    }

    const view = new DataView(finalBuffer.buffer, finalBuffer.byteOffset, 64);
    view.setBigUint64(56, totalBits, false);
    this.processBlock(finalBuffer, 0);

    const out = new Uint8Array(32);
    const outView = new DataView(out.buffer, out.byteOffset, 32);
    outView.setUint32(0, this.h0, false);
    outView.setUint32(4, this.h1, false);
    outView.setUint32(8, this.h2, false);
    outView.setUint32(12, this.h3, false);
    outView.setUint32(16, this.h4, false);
    outView.setUint32(20, this.h5, false);
    outView.setUint32(24, this.h6, false);
    outView.setUint32(28, this.h7, false);

    return out;
  }

  digestHex(): string {
    const bytes = this.digest();
    let hex = '';
    for (let i = 0; i < bytes.length; i++) {
      hex += bytes[i].toString(16).padStart(2, '0');
    }
    return hex;
  }
}

/**
 * Streaming file hasher that reads slice-by-slice without loading the whole file into RAM.
 */
export async function computeFileSha256(
  file: Blob,
  chunkSize = 1024 * 1024,
  onProgress?: (bytesHashed: number, totalBytes: number) => void
): Promise<string> {
  const hasher = new Sha256Stream();
  let offset = 0;
  const total = file.size;

  if (total === 0) {
    return hasher.digestHex();
  }

  while (offset < total) {
    const end = Math.min(offset + chunkSize, total);
    const slice = file.slice(offset, end);
    const buf = await slice.arrayBuffer();
    hasher.update(new Uint8Array(buf));
    offset = end;
    onProgress?.(offset, total);
  }

  return hasher.digestHex();
}
