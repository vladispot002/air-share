import { ContactEntry, RecentUserEntry } from "./types";
import { IdentityClient } from "./IdentityClient";

export class ContactManager {
  private client: IdentityClient;
  private baseUrl: string;

  constructor(client: IdentityClient, baseUrl: string = "") {
    this.client = client;
    this.baseUrl = baseUrl.replace(/\/$/, "");
  }

  private getAuthHeaders(): HeadersInit {
    const s = this.client.getState();
    if (!s.token) {
      throw new Error("Authentication required for contact operations");
    }
    return {
      "Content-Type": "application/json",
      Authorization: `Bearer ${s.token}`,
    };
  }

  public async getContacts(): Promise<ContactEntry[]> {
    const res = await fetch(`${this.baseUrl}/api/v1/contacts`, {
      headers: this.getAuthHeaders(),
    });
    if (!res.ok) {
      throw new Error(`Failed to fetch contacts: HTTP ${res.status}`);
    }
    return res.json();
  }

  public async sendContactRequest(targetLandropId: string): Promise<ContactEntry> {
    const res = await fetch(`${this.baseUrl}/api/v1/contacts/request`, {
      method: "POST",
      headers: this.getAuthHeaders(),
      body: JSON.stringify({ target_landrop_id: targetLandropId }),
    });
    if (!res.ok) {
      const err = await res.json().catch(() => ({ error: "Request failed" }));
      throw new Error(err.error || `HTTP ${res.status}`);
    }
    return res.json();
  }

  public async acceptContactRequest(targetUserId: string): Promise<void> {
    const res = await fetch(`${this.baseUrl}/api/v1/contacts/accept`, {
      method: "POST",
      headers: this.getAuthHeaders(),
      body: JSON.stringify({ target_user_id: targetUserId }),
    });
    if (!res.ok) {
      throw new Error(`Failed to accept contact: HTTP ${res.status}`);
    }
  }

  public async declineContactRequest(targetUserId: string): Promise<void> {
    const res = await fetch(`${this.baseUrl}/api/v1/contacts/decline`, {
      method: "POST",
      headers: this.getAuthHeaders(),
      body: JSON.stringify({ target_user_id: targetUserId }),
    });
    if (!res.ok) {
      throw new Error(`Failed to decline contact: HTTP ${res.status}`);
    }
  }

  public async blockUser(targetUserId: string): Promise<void> {
    const res = await fetch(`${this.baseUrl}/api/v1/contacts/block`, {
      method: "POST",
      headers: this.getAuthHeaders(),
      body: JSON.stringify({ target_user_id: targetUserId }),
    });
    if (!res.ok) {
      throw new Error(`Failed to block user: HTTP ${res.status}`);
    }
  }

  public async setFavorite(targetUserId: string, isFavorite: boolean): Promise<void> {
    const res = await fetch(`${this.baseUrl}/api/v1/contacts/favorite`, {
      method: "POST",
      headers: this.getAuthHeaders(),
      body: JSON.stringify({ target_user_id: targetUserId, is_favorite: isFavorite }),
    });
    if (!res.ok) {
      throw new Error(`Failed to toggle favorite: HTTP ${res.status}`);
    }
  }

  public async getRecentUsers(limit = 10): Promise<RecentUserEntry[]> {
    const res = await fetch(`${this.baseUrl}/api/v1/contacts/recents?limit=${limit}`, {
      headers: this.getAuthHeaders(),
    });
    if (!res.ok) {
      return [];
    }
    return res.json();
  }
}
