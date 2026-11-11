export interface BridgeStatus {
  status: string;
  service: string;
  version: string;
  port: number;
}

export interface DiscoveredBridgePeer {
  id: string;
  name: string;
  ip: string;
  port: number;
  deviceType: string;
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
}
