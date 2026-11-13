import { UserAccount, Device, ContactEntry, TransferHistoryRecord } from "./types";

export interface IdentityState {
  isGuest: boolean;
  guestId?: string;
  user?: UserAccount;
  device?: Device;
  token?: string;
}

export type AuthListener = (state: IdentityState) => void;

export class IdentityClient {
  private baseUrl: string;
  private state: IdentityState;
  private listeners: Set<AuthListener> = new Set();

  constructor(baseUrl: string = "") {
    this.baseUrl = baseUrl.replace(/\/$/, "");
    this.state = this.loadInitialState();
  }

  private getStorage() {
    if (typeof localStorage !== "undefined") {
      return localStorage;
    }
    return {
      getItem: (_key: string): string | null => null,
      setItem: (_key: string, _value: string): void => {},
      removeItem: (_key: string): void => {},
    };
  }

  private loadInitialState(): IdentityState {
    const storage = this.getStorage();
    const savedToken = storage.getItem("landrop_auth_token");
    const savedUser = storage.getItem("landrop_user_profile");
    const savedDev = storage.getItem("landrop_device_info");

    if (savedToken && savedUser) {
      try {
        return {
          isGuest: false,
          token: savedToken,
          user: JSON.parse(savedUser),
          device: savedDev ? JSON.parse(savedDev) : undefined,
        };
      } catch {
        // Fall back to guest session
      }
    }

    // Default zero-friction guest session
    let guestId = storage.getItem("landrop_guest_id");
    if (!guestId) {
      guestId = "guest-" + Math.random().toString(36).substring(2, 8);
      storage.setItem("landrop_guest_id", guestId);
    }

    return {
      isGuest: true,
      guestId: guestId,
    };
  }

  public getState(): IdentityState {
    return { ...this.state };
  }

  public onAuthStateChanged(listener: AuthListener): () => void {
    this.listeners.add(listener);
    listener(this.getState());
    return () => this.listeners.delete(listener);
  }

  private notify() {
    const s = this.getState();
    this.listeners.forEach((fn) => fn(s));
  }

  public async fetchProfile(): Promise<UserAccount | null> {
    if (this.state.isGuest || !this.state.token) {
      return null;
    }

    try {
      const res = await fetch(`${this.baseUrl}/api/v1/users/me`, {
        headers: {
          Authorization: `Bearer ${this.state.token}`,
        },
      });
      if (!res.ok) {
        if (res.status === 401) {
          this.logout();
        }
        return null;
      }
      const user: UserAccount = await res.json();
      this.state.user = user;
      this.getStorage().setItem("landrop_user_profile", JSON.stringify(user));
      this.notify();
      return user;
    } catch {
      return null;
    }
  }

  public async loginWithProvider(payload: {
    provider: string;
    provider_user_id?: string;
    email?: string;
    display_name?: string;
    avatar_url?: string;
    device_name?: string;
    platform?: string;
  }): Promise<IdentityState> {
    const isMobile = typeof navigator !== "undefined" && navigator.userAgent.includes("Mobile");
    const res = await fetch(`${this.baseUrl}/api/v1/auth/login`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        ...payload,
        platform: payload.platform || "web",
        device_name: payload.device_name || (isMobile ? "Web Mobile" : "Web Browser"),
      }),
    });

    if (!res.ok) {
      const err = await res.json().catch(() => ({ error: "Login failed" }));
      throw new Error(err.error || `HTTP ${res.status}`);
    }

    const data = await res.json();
    this.state = {
      isGuest: false,
      token: data.token,
      user: data.user,
      device: data.device,
    };

    const storage = this.getStorage();
    storage.setItem("landrop_auth_token", data.token);
    storage.setItem("landrop_user_profile", JSON.stringify(data.user));
    if (data.device) {
      storage.setItem("landrop_device_info", JSON.stringify(data.device));
    }

    this.notify();
    return this.getState();
  }

  public async registerWithPassword(payload: {
    email: string;
    password: string;
    confirm_password?: string;
    display_name?: string;
    device_name?: string;
    platform?: string;
  }): Promise<IdentityState> {
    const isMobile = typeof navigator !== "undefined" && navigator.userAgent.includes("Mobile");
    const res = await fetch(`${this.baseUrl}/api/v1/auth/register`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        email: payload.email,
        password: payload.password,
        confirm_password: payload.confirm_password || payload.password,
        display_name: payload.display_name || payload.email.split("@")[0],
        platform: payload.platform || (isMobile ? "mobile" : "web"),
        device_name: payload.device_name || (isMobile ? "Web Mobile" : "Web Browser"),
        app_version: "2.0.0",
      }),
    });

    if (!res.ok) {
      const err = await res.json().catch(() => ({ error: "Registration failed" }));
      throw new Error(err.error || `HTTP ${res.status}`);
    }

    const data = await res.json();
    this.state = {
      isGuest: false,
      token: data.token,
      user: data.user,
      device: data.device,
    };

    const storage = this.getStorage();
    storage.setItem("landrop_auth_token", data.token);
    storage.setItem("landrop_user_profile", JSON.stringify(data.user));
    if (data.device) {
      storage.setItem("landrop_device_info", JSON.stringify(data.device));
    }

    this.notify();
    return this.getState();
  }

  public async loginWithPassword(payload: {
    email: string;
    password: string;
    device_name?: string;
    platform?: string;
  }): Promise<IdentityState> {
    const isMobile = typeof navigator !== "undefined" && navigator.userAgent.includes("Mobile");
    const res = await fetch(`${this.baseUrl}/api/v1/auth/login`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        email: payload.email,
        password: payload.password,
        platform: payload.platform || (isMobile ? "mobile" : "web"),
        device_name: payload.device_name || (isMobile ? "Web Mobile" : "Web Browser"),
        app_version: "2.0.0",
      }),
    });

    if (!res.ok) {
      const err = await res.json().catch(() => ({ error: "Login failed" }));
      throw new Error(err.error || `HTTP ${res.status}`);
    }

    const data = await res.json();
    this.state = {
      isGuest: false,
      token: data.token,
      user: data.user,
      device: data.device,
    };

    const storage = this.getStorage();
    storage.setItem("landrop_auth_token", data.token);
    storage.setItem("landrop_user_profile", JSON.stringify(data.user));
    if (data.device) {
      storage.setItem("landrop_device_info", JSON.stringify(data.device));
    }

    this.notify();
    return this.getState();
  }

  public logout(): void {
    const storage = this.getStorage();
    storage.removeItem("landrop_auth_token");
    storage.removeItem("landrop_user_profile");
    storage.removeItem("landrop_device_info");

    const guestId = "guest-" + Math.random().toString(36).substring(2, 8);
    storage.setItem("landrop_guest_id", guestId);

    this.state = {
      isGuest: true,
      guestId,
    };
    this.notify();
  }

  public async lookupUser(landropId: string): Promise<UserAccount | null> {
    const norm = landropId.startsWith("@") ? landropId : `@${landropId}`;
    const res = await fetch(`${this.baseUrl}/api/v1/users/lookup?id=${encodeURIComponent(norm)}`);
    if (!res.ok) return null;
    return res.json();
  }

  public async searchUsers(query: string, limit = 10): Promise<UserAccount[]> {
    const res = await fetch(`${this.baseUrl}/api/v1/users/search?q=${encodeURIComponent(query)}&limit=${limit}`);
    if (!res.ok) return [];
    return res.json();
  }

  public async listDevices(): Promise<Device[]> {
    if (this.state.isGuest || !this.state.token) return [];
    try {
      const res = await fetch(`${this.baseUrl}/api/v1/devices`, {
        headers: { Authorization: `Bearer ${this.state.token}` },
      });
      if (!res.ok) return [];
      return await res.json();
    } catch {
      return [];
    }
  }

  public async revokeDevice(deviceId: string): Promise<boolean> {
    if (this.state.isGuest || !this.state.token) return false;
    try {
      const res = await fetch(`${this.baseUrl}/api/v1/devices/revoke`, {
        method: "POST",
        headers: {
          Authorization: `Bearer ${this.state.token}`,
          "Content-Type": "application/json",
        },
        body: JSON.stringify({ device_id: deviceId }),
      });
      return res.ok;
    } catch {
      return false;
    }
  }

  public async listContacts(): Promise<ContactEntry[]> {
    if (this.state.isGuest || !this.state.token) return [];
    try {
      const res = await fetch(`${this.baseUrl}/api/v1/contacts`, {
        headers: { Authorization: `Bearer ${this.state.token}` },
      });
      if (!res.ok) return [];
      return await res.json();
    } catch {
      return [];
    }
  }

  public async getHistory(limit = 20): Promise<TransferHistoryRecord[]> {
    if (this.state.isGuest || !this.state.token) return [];
    try {
      const res = await fetch(`${this.baseUrl}/api/v1/history?limit=${limit}`, {
        headers: { Authorization: `Bearer ${this.state.token}` },
      });
      if (!res.ok) return [];
      return await res.json();
    } catch {
      return [];
    }
  }

  public async recordHistory(record: Partial<TransferHistoryRecord>): Promise<TransferHistoryRecord | null> {
    if (this.state.isGuest || !this.state.token) return null;
    try {
      const res = await fetch(`${this.baseUrl}/api/v1/history`, {
        method: "POST",
        headers: {
          Authorization: `Bearer ${this.state.token}`,
          "Content-Type": "application/json",
        },
        body: JSON.stringify(record),
      });
      if (!res.ok) return null;
      return await res.json();
    } catch {
      return null;
    }
  }

  public async clearHistory(): Promise<boolean> {
    if (this.state.isGuest || !this.state.token) return false;
    try {
      const res = await fetch(`${this.baseUrl}/api/v1/history`, {
        method: "DELETE",
        headers: { Authorization: `Bearer ${this.state.token}` },
      });
      return res.ok;
    } catch {
      return false;
    }
  }

  public bindWebSocket(ws: WebSocket): void {
    if (ws.readyState === WebSocket.OPEN) {
      this.sendBindEnvelope(ws);
    } else {
      ws.addEventListener(
        "open",
        () => {
          this.sendBindEnvelope(ws);
        },
        { once: true }
      );
    }
  }

  private sendBindEnvelope(ws: WebSocket): void {
    if (this.state.isGuest || !this.state.token) return;

    const env = {
      version: "2.0",
      action: "BIND_CLIENT",
      timestamp: Date.now(),
      payload: {
        token: this.state.token,
        user_id: this.state.user?.user_id,
        device_id: this.state.device?.device_id,
      },
    };
    try {
      ws.send(JSON.stringify(env));
    } catch {
      // Socket not writable
    }
  }
}
