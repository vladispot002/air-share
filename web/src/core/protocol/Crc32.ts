/**
 * Standard IEEE 802.3 CRC-32 (polynomial 0xEDB88320)
 * Matching C++ landrop::protocol::v2::computeCrc32
 */
const CRC_POLYNOMIAL = 0xedb88320;

// Precomputed 256-entry lookup table
const crc32Table = (() => {
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let j = 0; j < 8; j++) {
      c = (c & 1) ? (CRC_POLYNOMIAL ^ (c >>> 1)) : (c >>> 1);
    }
    table[i] = c >>> 0;
  }
  return table;
})();

/**
 * Computes IEEE 802.3 CRC-32 checksum of data.
 * @param data Uint8Array bytes or UTF-8 string
 * @returns 32-bit unsigned integer (0 .. 0xFFFFFFFF)
 */
export function computeCrc32(data: Uint8Array | string): number {
  const bytes = typeof data === 'string' ? new TextEncoder().encode(data) : data;
  if (!bytes || bytes.length === 0) {
    return 0;
  }

  let crc = 0xffffffff;
  for (let i = 0; i < bytes.length; i++) {
    crc = (crc32Table[(crc ^ bytes[i]) & 0xff] ^ (crc >>> 8)) >>> 0;
  }

  return (crc ^ 0xffffffff) >>> 0;
}
