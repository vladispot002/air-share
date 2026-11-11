import { Device, DevicePairingSession } from "./types";
import { IdentityClient } from "./IdentityClient";

export class DeviceManager {
  private client: IdentityClient;
  private baseUrl: string;

  constructor(client: IdentityClient, baseUrl: string = "") {
    this.client = client;
    this.baseUrl = baseUrl.replace(/\/$/, "");
  }

  private getAuthHeaders(): HeadersInit {
    const s = this.client.getState();
    if (!s.token) {
      throw new Error("Authentication required for device operations");
    }
    return {
      "Content-Type": "application/json",
      Authorization: `Bearer ${s.token}`,
    };
  }

  public async getDevices(): Promise<Device[]> {
    const res = await fetch(`${this.baseUrl}/api/v1/devices`, {
      headers: this.getAuthHeaders(),
    });
    if (!res.ok) {
      throw new Error(`Failed to list devices: HTTP ${res.status}`);
    }
    return res.json();
  }

  public async registerDevice(name: string, platform = "web", appVersion = "2.0.0"): Promise<Device> {
    const res = await fetch(`${this.baseUrl}/api/v1/devices`, {
      method: "POST",
      headers: this.getAuthHeaders(),
      body: JSON.stringify({
        device_name: name,
        device_type: "browser",
        platform,
        app_version: appVersion,
      }),
    });
    if (!res.ok) {
      throw new Error(`Failed to register device: HTTP ${res.status}`);
    }
    return res.json();
  }

  public async revokeDevice(deviceId: string): Promise<void> {
    const res = await fetch(`${this.baseUrl}/api/v1/devices/revoke`, {
      method: "POST",
      headers: this.getAuthHeaders(),
      body: JSON.stringify({ device_id: deviceId }),
    });
    if (!res.ok) {
      throw new Error(`Failed to revoke device: HTTP ${res.status}`);
    }
  }

  public async createPairingSession(): Promise<DevicePairingSession> {
    const res = await fetch(`${this.baseUrl}/api/v1/devices/pairing`, {
      method: "POST",
      headers: this.getAuthHeaders(),
    });
    if (!res.ok) {
      throw new Error(`Failed to create pairing session: HTTP ${res.status}`);
    }
    return res.json();
  }

  public async confirmPairing(
    sessionId: string,
    secret: string,
    deviceName: string,
    platform = "web"
  ): Promise<{ device: Device; auth: any }> {
    const res = await fetch(`${this.baseUrl}/api/v1/devices/pairing/confirm`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        session_id: sessionId,
        secret,
        device_name: deviceName,
        device_type: "browser",
        platform,
      }),
    });
    if (!res.ok) {
      const err = await res.json().catch(() => ({ error: "Pairing failed" }));
      throw new Error(err.error || `HTTP ${res.status}`);
    }
    return res.json();
  }
}
