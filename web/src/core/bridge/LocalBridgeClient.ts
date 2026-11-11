export interface BridgeStatus {
  status: string;
  service: string;
  version: string;
  port: number;
  transferPort?: number;
}

export interface DiscoveredBridgePeer {
  id: string;
  name: string;
  ip: string;
  port: number;
  deviceType: string;
}

export interface BridgeUploadResult {
  status: string;
  filename: string;
  path: string;
  bytes: number;
  sha256: string;
}

export class LocalBridgeClient {
  constructor(private bridgeBaseUrl: string = 'http://127.0.0.1:8849') {}

  async isBridgeAvailable(): Promise<boolean> {
    try {
      const resp = await fetch(`${this.bridgeBaseUrl}/bridge/status`, {
        method: 'GET',
        headers: { 'Accept': 'application/json' },
      });
      if (!resp.ok) return false;
      const data: BridgeStatus = await resp.json();
      return data.status === 'ok';
    } catch {
      return false;
    }
  }

  async getStatus(): Promise<BridgeStatus | null> {
    try {
      const resp = await fetch(`${this.bridgeBaseUrl}/bridge/status`);
      if (!resp.ok) return null;
      return await resp.json();
    } catch {
      return null;
    }
  }

  async getDiscoveredPeers(): Promise<DiscoveredBridgePeer[]> {
    try {
      const resp = await fetch(`${this.bridgeBaseUrl}/bridge/peers`);
      if (!resp.ok) return [];
      return await resp.json();
    } catch {
      return [];
    }
  }

  async uploadFile(file: File | Blob, filename?: string): Promise<BridgeUploadResult> {
    const name = filename || (file instanceof File ? file.name : 'upload.bin');
    const resp = await fetch(`${this.bridgeBaseUrl}/bridge/upload?filename=${encodeURIComponent(name)}`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/octet-stream',
        'X-Filename': encodeURIComponent(name),
      },
      body: file,
    });
    if (!resp.ok) {
      throw new Error(`Upload failed with status ${resp.status}`);
    }
    return await resp.json();
  }

  async downloadFile(filename: string): Promise<Blob> {
    const resp = await fetch(`${this.bridgeBaseUrl}/bridge/download?file=${encodeURIComponent(filename)}`);
    if (!resp.ok) {
      throw new Error(`Download failed with status ${resp.status}`);
    }
    return await resp.blob();
  }
}

