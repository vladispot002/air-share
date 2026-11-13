import { describe, it, expect } from 'vitest';
import {
  normalizeRoomCode,
  isValidRoomCode,
  tryNormalizeRoomCode,
} from '../../src/core/session/RoomCodeNormalizer';

describe('TC-ROOM-NORMALIZATION: Crockford Base32 Room Code Normalizer', () => {
  it('preserves standard formatted room code unchanged', () => {
    expect(normalizeRoomCode('DROP-K8M4-P9X2')).toBe('DROP-K8M4-P9X2');
    expect(isValidRoomCode('DROP-K8M4-P9X2')).toBe(true);
  });

  it('normalizes lowercase raw 8-character code with DROP- prefix', () => {
    expect(normalizeRoomCode('k8m4p9x2')).toBe('DROP-K8M4-P9X2');
  });

  it('strips arbitrary whitespace and delimiters', () => {
    expect(normalizeRoomCode(' drop - k8m4 - p9x2 ')).toBe('DROP-K8M4-P9X2');
    expect(normalizeRoomCode('DROP\tK8M4\nP9X2\r')).toBe('DROP-K8M4-P9X2');
  });

  it('corrects optical confusions (O -> 0, I -> 1, L -> 1)', () => {
    expect(normalizeRoomCode('DROP-K8M4-P9O2')).toBe('DROP-K8M4-P902');
    expect(normalizeRoomCode('DROP-K8M4-P9I2')).toBe('DROP-K8M4-P912');
    expect(normalizeRoomCode('DROP-K8M4-P9L2')).toBe('DROP-K8M4-P912');
    expect(normalizeRoomCode('oil2oil2')).toBe('DROP-0112-0112');
  });

  it('rejects letter "U" (excluded from Crockford alphabet)', () => {
    expect(() => normalizeRoomCode('DROP-K8M4-P9U2')).toThrow(/Invalid character 'U'/);
    expect(isValidRoomCode('DROP-K8M4-P9U2')).toBe(false);
  });

  it('rejects invalid code lengths', () => {
    expect(() => normalizeRoomCode('DROP-SHORT')).toThrow(/expected 8 characters/);
    expect(() => normalizeRoomCode('DROP-123456789')).toThrow(/expected 8 characters/);
    expect(() => normalizeRoomCode('')).toThrow(/cannot be empty/);
  });

  it('tryNormalizeRoomCode returns safe result object', () => {
    const success = tryNormalizeRoomCode('k8m4p9x2');
    expect(success.success).toBe(true);
    expect(success.code).toBe('DROP-K8M4-P9X2');

    const failure = tryNormalizeRoomCode('invalid-u');
    expect(failure.success).toBe(false);
    expect(failure.error).toBeDefined();
  });

  it('normalizes full URLs with hash fragments and pairing links', () => {
    expect(normalizeRoomCode('https://landrop-server.duckdns.org/#DROP-K8M4-P9X2')).toBe('DROP-K8M4-P9X2');
    expect(normalizeRoomCode('http://localhost:5173/#k8m4p9x2')).toBe('DROP-K8M4-P9X2');
    expect(normalizeRoomCode('https://domain.com/pair#K8M4P9X2')).toBe('DROP-K8M4-P9X2');
    expect(isValidRoomCode('https://landrop-server.duckdns.org/#DROP-K8M4-P9X2')).toBe(true);
  });
});
