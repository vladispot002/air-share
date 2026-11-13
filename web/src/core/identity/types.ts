// Web Client Identity, Device, Contact & Transfer Types

export type DevicePresence = "online" | "offline";
export type DeviceStatus = "active" | "revoked";

export interface Device {
  device_id: string;
  user_id: string;
  device_name: string;
  device_type: "desktop" | "laptop" | "mobile" | "tablet" | "browser";
  platform: "linux" | "windows" | "macos" | "android" | "ios" | "web";
  app_version?: string;
  registered_at?: string;
  last_seen_at?: string;
  status: DeviceStatus;
  presence: DevicePresence;
}

export interface UserAccount {
  user_id: string;
  landrop_id: string;
  display_name: string;
  avatar_url?: string;
  email?: string;
  email_verified?: boolean;
  created_at?: string;
  status?: string;
}

export interface GuestSession {
  guest_id: string;
  created_at: number;
  is_guest: boolean;
}

export type ContactState = "NONE" | "REQUEST_SENT" | "REQUEST_RECEIVED" | "ACCEPTED" | "BLOCKED";

export interface ContactEntry {
  user_id: string;
  target_user_id: string;
  landrop_id: string;
  display_name: string;
  avatar_url?: string;
  state: ContactState;
  is_favorite: boolean;
  updated_at?: string;
}

export interface RecentUserEntry {
  user_id: string;
  target_user_id: string;
  landrop_id: string;
  display_name: string;
  avatar_url?: string;
  last_interaction: string;
  last_transfer_status: string;
  available_devices?: number;
}

export type DeliveryMode = "SPECIFIC_DEVICE" | "ANY_AVAILABLE_DEVICE";
export type TransferState =
  | "CREATED"
  | "OFFERED"
  | "AWAITING_APPROVAL"
  | "ACCEPTED"
  | "REJECTED"
  | "CONNECTING"
  | "TRANSFERRING"
  | "PAUSED"
  | "COMPLETED"
  | "FAILED"
  | "CANCELLED"
  | "EXPIRED";

export interface TransferFileItem {
  file_name: string;
  file_size: number;
  sha256_hex?: string;
}

export interface TransferRequest {
  request_id: string;
  sender_user_id?: string;
  sender_landrop_id?: string;
  sender_device_id?: string;
  sender_device_name?: string;
  recipient_user_id: string;
  target_device_id?: string;
  delivery_mode: DeliveryMode;
  files: TransferFileItem[];
  total_bytes: number;
  state: TransferState;
  claimed_by_device_id?: string;
  room_code?: string;
  created_at?: string;
  expires_at?: string;
}

export interface DevicePairingSession {
  session_id: string;
  user_id: string;
  secret: string;
  expires_at: string;
  used: boolean;
}

export interface TransferHistoryRecord {
  transfer_id: string;
  user_id: string;
  direction: "upload" | "download";
  peer_user_id?: string;
  peer_landrop_id?: string;
  peer_device_name?: string;
  files: TransferFileItem[];
  total_bytes: number;
  status: TransferState;
  transport_type: string;
  started_at: string;
  completed_at: string;
}
