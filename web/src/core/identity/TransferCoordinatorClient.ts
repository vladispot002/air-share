import { TransferRequest, TransferFileItem, DeliveryMode, TransferState, TransferHistoryRecord } from "./types";
import { IdentityClient } from "./IdentityClient";

export class TransferCoordinatorClient {
  private client: IdentityClient;
  private baseUrl: string;

  constructor(client: IdentityClient, baseUrl: string = "") {
    this.client = client;
    this.baseUrl = baseUrl.replace(/\/$/, "");
  }

  private getAuthHeaders(): HeadersInit {
    const s = this.client.getState();
    const headers: Record<string, string> = {
      "Content-Type": "application/json",
    };
    if (s.token) {
      headers["Authorization"] = `Bearer ${s.token}`;
    }
    return headers;
  }

  public async offerTransfer(params: {
    recipientUserId: string;
    files: TransferFileItem[];
    deliveryMode?: DeliveryMode;
    targetDeviceId?: string;
  }): Promise<TransferRequest> {
    const s = this.client.getState();
    const reqBody: Partial<TransferRequest> = {
      recipient_user_id: params.recipientUserId,
      files: params.files,
      delivery_mode: params.deliveryMode || (params.targetDeviceId ? "SPECIFIC_DEVICE" : "ANY_AVAILABLE_DEVICE"),
      target_device_id: params.targetDeviceId,
      sender_device_name: s.device?.device_name || (s.isGuest ? "Guest Web" : "Web User"),
    };

    const res = await fetch(`${this.baseUrl}/api/v1/transfers/requests`, {
      method: "POST",
      headers: this.getAuthHeaders(),
      body: JSON.stringify(reqBody),
    });
    if (!res.ok) {
      const err = await res.json().catch(() => ({ error: "Transfer offer failed" }));
      throw new Error(err.error || `HTTP ${res.status}`);
    }
    return res.json();
  }

  public async claimTransfer(requestId: string, deviceId?: string): Promise<TransferRequest> {
    const s = this.client.getState();
    const devId = deviceId || s.device?.device_id || "";
    const res = await fetch(`${this.baseUrl}/api/v1/transfers/requests/claim`, {
      method: "POST",
      headers: this.getAuthHeaders(),
      body: JSON.stringify({ request_id: requestId, device_id: devId }),
    });
    if (!res.ok) {
      const err = await res.json().catch(() => ({ error: "Transfer claim failed" }));
      throw new Error(err.error || `HTTP ${res.status}`);
    }
    return res.json();
  }

  public async declineTransfer(requestId: string, deviceId?: string): Promise<TransferRequest> {
    const s = this.client.getState();
    const devId = deviceId || s.device?.device_id || "";
    const res = await fetch(`${this.baseUrl}/api/v1/transfers/requests/decline`, {
      method: "POST",
      headers: this.getAuthHeaders(),
      body: JSON.stringify({ request_id: requestId, device_id: devId }),
    });
    if (!res.ok) {
      const err = await res.json().catch(() => ({ error: "Transfer decline failed" }));
      throw new Error(err.error || `HTTP ${res.status}`);
    }
    return res.json();
  }

  public async completeTransfer(requestId: string, state: TransferState = "COMPLETED", transportType = "WebRtcDataChannel"): Promise<TransferRequest> {
    const res = await fetch(`${this.baseUrl}/api/v1/transfers/requests/complete`, {
      method: "POST",
      headers: this.getAuthHeaders(),
      body: JSON.stringify({ request_id: requestId, state, transport_type: transportType }),
    });
    if (!res.ok) {
      throw new Error(`Failed to complete transfer: HTTP ${res.status}`);
    }
    return res.json();
  }

  public async getHistory(limit = 20): Promise<TransferHistoryRecord[]> {
    const s = this.client.getState();
    if (!s.token) return [];

    const res = await fetch(`${this.baseUrl}/api/v1/history?limit=${limit}`, {
      headers: this.getAuthHeaders(),
    });
    if (!res.ok) return [];
    return res.json();
  }
}
