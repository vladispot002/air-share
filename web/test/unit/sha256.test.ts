import { describe, it, expect } from 'vitest';
import { Sha256Stream, computeFileSha256 } from '../../src/core/crypto/Sha256';

describe('Sha256Stream FIPS 180-4 Verification', () => {
  it('TC-SHA-01: matches standard empty string vector', () => {
    const hasher = new Sha256Stream();
    expect(hasher.digestHex()).toBe('e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855');
  });

  it('TC-SHA-02: matches NIST vector for "abc"', () => {
    const hasher = new Sha256Stream();
    hasher.update('abc');
    expect(hasher.digestHex()).toBe('ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
  });

  it('TC-SHA-03: matches NIST vector for multi-block 56-byte input', () => {
    const hasher = new Sha256Stream();
    hasher.update('abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq');
    expect(hasher.digestHex()).toBe('248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1');
  });

  it('TC-SHA-04: matches incremental updates vs single update', () => {
    const fullText = 'LAN-Drop WebRTC secure file streaming across internet NATs';
    const h1 = new Sha256Stream().update(fullText).digestHex();

    const h2 = new Sha256Stream();
    for (let i = 0; i < fullText.length; i += 5) {
      h2.update(fullText.slice(i, i + 5));
    }
    expect(h2.digestHex()).toBe(h1);
  });

  it('TC-SHA-05: computeFileSha256 works on simulated Blob/File', async () => {
    const payload = new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
    const blob = new Blob([payload]);
    const hex = await computeFileSha256(blob, 3);
    const expected = new Sha256Stream().update(payload).digestHex();
    expect(hex).toBe(expected);
  });

  it('TC-SHA-06: computeFileSha256 handles 0-byte Blob', async () => {
    const blob = new Blob([]);
    const hex = await computeFileSha256(blob);
    expect(hex).toBe('e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855');
  });

  it('TC-SHA-07: computePartialFileSha256 slice calculation and out-of-bounds rejection', async () => {
    const { computePartialFileSha256 } = await import('../../src/core/crypto/Sha256');
    const payload = new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
    const blob = new Blob([payload]);

    // 0-byte slice returns empty hash
    const zeroHex = await computePartialFileSha256(blob, 0);
    expect(zeroHex).toBe('e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855');

    // 5-byte partial slice
    const partHex = await computePartialFileSha256(blob, 5);
    const expectedPart = new Sha256Stream().update(payload.subarray(0, 5)).digestHex();
    expect(partHex).toBe(expectedPart);

    // Out-of-bounds slice rejection
    await expect(computePartialFileSha256(blob, 20)).rejects.toThrow(/exceeds file size/);
  });
});
