import { describe, it, expect } from 'vitest';
import { computeCrc32 } from '../../src/core/protocol/Crc32';

describe('IEEE 802.3 CRC-32 Verification', () => {
  it('TC-WP2-01: matches standard test vector "123456789" -> 0xCBF43926', () => {
    const input = '123456789';
    const expected = 0xcbf43926; // 3421780774 in unsigned 32-bit int
    const result = computeCrc32(input);
    expect(result).toBe(expected);
  });

  it('returns 0 for empty data', () => {
    expect(computeCrc32('')).toBe(0);
    expect(computeCrc32(new Uint8Array(0))).toBe(0);
  });

  it('computes identical checksum for Uint8Array and string', () => {
    const text = 'LAN-Drop WebRTC WireProtocolV2';
    const bytes = new TextEncoder().encode(text);
    expect(computeCrc32(text)).toBe(computeCrc32(bytes));
  });

  it('detects single-byte changes', () => {
    const data = new Uint8Array([0x01, 0x02, 0x03, 0x04]);
    const crc1 = computeCrc32(data);
    data[2] ^= 0xff;
    const crc2 = computeCrc32(data);
    expect(crc1).not.toBe(crc2);
  });
});
