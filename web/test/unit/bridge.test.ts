import { describe, it, expect, vi, beforeEach } from 'vitest';
import { LocalBridgeClient } from '../../src/core/bridge/LocalBridgeClient';

describe('LocalBridgeClient Suite (Workstream E2)', () => {
  let client: LocalBridgeClient;

  beforeEach(() => {
    client = new LocalBridgeClient('http://127.0.0.1:8849');
    vi.restoreAllMocks();
  });

  it('TC-BRG-01: reports bridge available when /bridge/status responds ok', async () => {
    vi.spyOn(globalThis, 'fetch').mockResolvedValue({
      ok: true,
      json: async () => ({
        status: 'ok',
        service: 'LAN-Drop Local Bridge',
        version: '1.0.2',
        port: 8849,
        transferPort: 8848,
      }),
    } as any);

    const available = await client.isBridgeAvailable();
    expect(available).toBe(true);

    const status = await client.getStatus();
    expect(status).not.toBeNull();
    expect(status?.service).toBe('LAN-Drop Local Bridge');
    expect(status?.port).toBe(8849);
    expect(status?.transferPort).toBe(8848);
  });

  it('TC-BRG-02: returns discovered peers from desktop bridge', async () => {
    const mockPeers = [
      { id: 'peer-1', name: 'MacBook Pro', ip: '192.168.1.105', port: 8848, deviceType: 'desktop' },
      { id: 'peer-2', name: 'Pixel 9', ip: '192.168.1.106', port: 8848, deviceType: 'mobile' },
    ];

    vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce({
      ok: true,
      json: async () => mockPeers,
    } as any);

    const peers = await client.getDiscoveredPeers();
    expect(peers.length).toBe(2);
    expect(peers[0].name).toBe('MacBook Pro');
    expect(peers[1].deviceType).toBe('mobile');
  });

  it('TC-BRG-03: handles offline bridge gracefully without throwing', async () => {
    vi.spyOn(globalThis, 'fetch').mockRejectedValue(new Error('Connection refused'));

    const available = await client.isBridgeAvailable();
    expect(available).toBe(false);

    const status = await client.getStatus();
    expect(status).toBeNull();

    const peers = await client.getDiscoveredPeers();
    expect(peers).toEqual([]);
  });

  it('TC-BRG-04: uploads file to local desktop bridge successfully', async () => {
    const mockUploadResult = {
      status: 'ok',
      filename: 'sample.pdf',
      path: '/home/user/Downloads/sample.pdf',
      bytes: 1024,
      sha256: 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855',
    };

    vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce({
      ok: true,
      json: async () => mockUploadResult,
    } as any);

    const blob = new Blob(['sample data'], { type: 'application/octet-stream' });
    const res = await client.uploadFile(blob, 'sample.pdf');
    expect(res.status).toBe('ok');
    expect(res.filename).toBe('sample.pdf');
    expect(res.bytes).toBe(1024);
    expect(res.sha256).toBe('e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855');
  });

  it('TC-BRG-05: downloads file from local desktop bridge as blob', async () => {
    const mockBlob = new Blob(['downloaded content'], { type: 'application/octet-stream' });
    vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce({
      ok: true,
      blob: async () => mockBlob,
    } as any);

    const blob = await client.downloadFile('sample.pdf');
    expect(blob).toBeDefined();
    expect(blob.size).toBeGreaterThan(0);
  });
});
