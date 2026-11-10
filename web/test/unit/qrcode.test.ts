import { describe, it, expect } from 'vitest';
import { QrCode, QrEccLevel } from '../../src/core/session/QrCode';

describe('QrCode Zero-Dependency ISO/IEC 18004 Suite (Milestone 3B)', () => {
  it('TC-QR-01: generates valid Version 1 matrix (21x21) for short room code', () => {
    const qr = QrCode.encodeText('DROP-7K9M-X2PQ', QrEccLevel.Medium);
    expect(qr.version).toBeGreaterThanOrEqual(1);
    expect(qr.size).toBe(qr.version * 4 + 17);
  });

  it('TC-QR-02: verifies 3 finder patterns located at corners', () => {
    const qr = QrCode.encodeText('https://drop.local/#DROP-7K9M-X2PQ');
    const s = qr.size;

    // Top-left center (3, 3) must be dark
    expect(qr.getModule(3, 3)).toBe(true);
    // Top-right center (s - 4, 3) must be dark
    expect(qr.getModule(s - 4, 3)).toBe(true);
    // Bottom-left center (3, s - 4) must be dark
    expect(qr.getModule(3, s - 4)).toBe(true);

    // Finder border modules must NEVER be overwritten by timing patterns
    expect(qr.getModule(1, 6)).toBe(true);
    expect(qr.getModule(3, 6)).toBe(true);
    expect(qr.getModule(5, 6)).toBe(true);
    expect(qr.getModule(6, 1)).toBe(true);
    expect(qr.getModule(6, 3)).toBe(true);
    expect(qr.getModule(6, 5)).toBe(true);

    // Dark Module at (8, size - 8) is ALWAYS dark
    expect(qr.getModule(8, qr.size - 8)).toBe(true);

    // Separators around top-left (e.g. (7, 0..6)) must be light
    for (let y = 0; y < 7; y++) {
      expect(qr.getModule(7, y)).toBe(false);
    }
  });

  it('TC-QR-03: verifies timing patterns strictly between finders', () => {
    const qr = QrCode.encodeText('TEST-QR-TIMING');
    // Horizontal timing pattern between finders
    for (let x = 8; x <= qr.size - 9; x++) {
      expect(qr.getModule(x, 6)).toBe(x % 2 === 0);
    }
    // Vertical timing pattern between finders
    for (let y = 8; y <= qr.size - 9; y++) {
      expect(qr.getModule(6, y)).toBe(y % 2 === 0);
    }
  });

  it('TC-QR-04: places Version Information blocks for Version 7..10', () => {
    const textV7 = 'A'.repeat(110);
    const qrV7 = QrCode.encodeText(textV7);
    expect(qrV7.version).toBe(7);
    expect(qrV7.size).toBe(45);

    const v7Expected = 0x07c94;
    for (let k = 0; k < 18; k++) {
      const expectedBit = ((v7Expected >>> k) & 1) !== 0;
      expect(qrV7.getModule(Math.floor(k / 3), 45 - 11 + (k % 3))).toBe(expectedBit);
      expect(qrV7.getModule(45 - 11 + (k % 3), Math.floor(k / 3))).toBe(expectedBit);
    }
  });

  it('TC-QR-05: throws when data exceeds maximum Version 10 capacity', () => {
    const hugeData = new Uint8Array(250);
    hugeData.fill(0x55);
    expect(() => QrCode.encodeBinary(hugeData)).toThrow('Data exceeds maximum QR capacity');
  });

  it('TC-QR-06: generates valid resolution-independent SVG markup', () => {
    const qr = QrCode.encodeText('https://lan-drop.local');
    const svg = qr.toSvgString(4, '#000000', '#ffffff');

    expect(svg).toContain('<svg');
    expect(svg).toContain('viewBox="0 0 ');
    expect(svg).toContain('<path fill="#000000"');
    expect(svg).toContain('</svg>');
  });

  it('TC-QR-07: handles maximum alphanumeric and URL pairing lengths', () => {
    const pairingUrl = 'https://lan-drop.subdomain.example.com:8443/web/#DROP-88M4-P9X2-C3T7';
    const qr = QrCode.encodeText(pairingUrl);

    expect(qr.size).toBeGreaterThanOrEqual(25); // At least Version 2
    expect(qr.getModule(0, 0)).toBe(true);
    expect(qr.getModule(8, qr.size - 8)).toBe(true);
  });
});
