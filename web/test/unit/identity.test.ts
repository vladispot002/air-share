import { describe, it, expect, vi, beforeEach } from 'vitest';
import { IdentityClient } from '../../src/core/identity/IdentityClient';
import { ContactManager } from '../../src/core/identity/ContactManager';
import { DeviceManager } from '../../src/core/identity/DeviceManager';
import { TransferCoordinatorClient } from '../../src/core/identity/TransferCoordinatorClient';

// In-memory localStorage mock for node test environment
const mockStorage = (() => {
  let store: Record<string, string> = {};
  return {
    getItem: (key: string) => store[key] || null,
    setItem: (key: string, value: string) => {
      store[key] = value.toString();
    },
    removeItem: (key: string) => {
      delete store[key];
    },
    clear: () => {
      store = {};
    },
  };
})();
Object.defineProperty(globalThis, 'localStorage', {
  value: mockStorage,
  writable: true,
});

describe('Web Identity Ecosystem Suite', () => {
  beforeEach(() => {
    mockStorage.clear();
    vi.restoreAllMocks();
  });

  describe('IdentityClient (Guest-First & Auth Lifecycle)', () => {
    it('TC-ID-01: initializes default guest session immediately with zero friction', () => {
      const client = new IdentityClient('http://localhost:8080');
      const state = client.getState();

      expect(state.isGuest).toBe(true);
      expect(state.guestId).toBeDefined();
      expect(state.guestId?.startsWith('guest-')).toBe(true);
      expect(state.token).toBeUndefined();
      expect(state.user).toBeUndefined();
    });

    it('TC-ID-02: logs in with provider, stores tokens and notifies listeners', async () => {
      const client = new IdentityClient('http://localhost:8080');
      let notifyCount = 0;
      client.onAuthStateChanged((s) => {
        if (!s.isGuest) notifyCount++;
      });

      const mockResponse = {
        token: 'ldtok_test_secret_123',
        user: {
          user_id: 'u-1',
          landrop_id: '@tester',
          display_name: 'Tester User',
          email: 'test@landrop.app',
        },
        device: {
          device_id: 'd-1',
          user_id: 'u-1',
          device_name: 'Chrome on Linux',
          platform: 'web',
          status: 'active',
          presence: 'online',
        },
      };

      vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce({
        ok: true,
        json: async () => mockResponse,
      } as any);

      const state = await client.loginWithProvider({
        provider: 'google',
        email: 'test@landrop.app',
        display_name: 'Tester User',
      });

      expect(state.isGuest).toBe(false);
      expect(state.token).toBe('ldtok_test_secret_123');
      expect(state.user?.landrop_id).toBe('@tester');
      expect(state.device?.device_id).toBe('d-1');
      expect(notifyCount).toBe(1);

      // Verify localStorage persistence
      expect(localStorage.getItem('landrop_auth_token')).toBe('ldtok_test_secret_123');
    });

    it('TC-ID-03: restores session on reload and falls back to guest on logout', async () => {
      localStorage.setItem('landrop_auth_token', 'ldtok_persisted_token');
      localStorage.setItem(
        'landrop_user_profile',
        JSON.stringify({ user_id: 'u-persisted', landrop_id: '@persisted', display_name: 'Persisted' })
      );

      const client = new IdentityClient('http://localhost:8080');
      expect(client.getState().isGuest).toBe(false);
      expect(client.getState().user?.landrop_id).toBe('@persisted');

      client.logout();
      expect(client.getState().isGuest).toBe(true);
      expect(localStorage.getItem('landrop_auth_token')).toBeNull();
    });

    it('TC-ID-04: dispatches BIND_CLIENT message on active WebSocket', () => {
      localStorage.setItem('landrop_auth_token', 'ldtok_ws_test');
      localStorage.setItem(
        'landrop_user_profile',
        JSON.stringify({ user_id: 'u-ws', landrop_id: '@ws_user', display_name: 'WS User' })
      );
      localStorage.setItem(
        'landrop_device_info',
        JSON.stringify({ device_id: 'd-ws', device_name: 'Browser' })
      );

      const client = new IdentityClient('http://localhost:8080');
      const mockWs = {
        readyState: 1, // OPEN
        send: vi.fn(),
        addEventListener: vi.fn(),
      } as unknown as WebSocket;

      client.bindWebSocket(mockWs);
      expect(mockWs.send).toHaveBeenCalledTimes(1);
      const sentPayload = JSON.parse((mockWs.send as any).mock.calls[0][0]);
      expect(sentPayload.action).toBe('BIND_CLIENT');
      expect(sentPayload.payload.token).toBe('ldtok_ws_test');
      expect(sentPayload.payload.device_id).toBe('d-ws');
    });
  });

  describe('ContactManager', () => {
    let client: IdentityClient;
    let contactMgr: ContactManager;

    beforeEach(async () => {
      client = new IdentityClient('http://localhost:8080');
      vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce({
        ok: true,
        json: async () => ({
          token: 'token-123',
          user: { user_id: 'u-alice', landrop_id: '@alice', display_name: 'Alice' },
        }),
      } as any);
      await client.loginWithProvider({ provider: 'local', email: 'alice@example.com' });
      contactMgr = new ContactManager(client, 'http://localhost:8080');
    });

    it('TC-CNT-01: fetches contacts and sends contact invitations', async () => {
      const mockContacts = [
        { user_id: 'u-alice', target_user_id: 'u-bob', landrop_id: '@bob', display_name: 'Bob', state: 'ACCEPTED', is_favorite: true },
      ];

      vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce({
        ok: true,
        json: async () => mockContacts,
      } as any);

      const contacts = await contactMgr.getContacts();
      expect(contacts.length).toBe(1);
      expect(contacts[0].landrop_id).toBe('@bob');

      // Send contact request
      vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce({
        ok: true,
        json: async () => ({
          user_id: 'u-alice',
          target_user_id: 'u-charlie',
          landrop_id: '@charlie',
          state: 'REQUEST_SENT',
        }),
      } as any);

      const sent = await contactMgr.sendContactRequest('@charlie');
      expect(sent.state).toBe('REQUEST_SENT');
    });
  });

  describe('DeviceManager & Pairing', () => {
    let client: IdentityClient;
    let devMgr: DeviceManager;

    beforeEach(async () => {
      client = new IdentityClient('http://localhost:8080');
      vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce({
        ok: true,
        json: async () => ({
          token: 'token-123',
          user: { user_id: 'u-1', landrop_id: '@user', display_name: 'User' },
        }),
      } as any);
      await client.loginWithProvider({ provider: 'local', email: 'user@example.com' });
      devMgr = new DeviceManager(client, 'http://localhost:8080');
    });

    it('TC-DEV-01: creates QR pairing session and confirms pairing', async () => {
      vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce({
        ok: true,
        json: async () => ({
          session_id: 'pair-sess-1',
          user_id: 'u-1',
          secret: 'pair-secret-abc',
          expires_at: new Date(Date.now() + 300000).toISOString(),
          used: false,
        }),
      } as any);

      const pairing = await devMgr.createPairingSession();
      expect(pairing.session_id).toBe('pair-sess-1');
      expect(pairing.secret).toBe('pair-secret-abc');

      vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce({
        ok: true,
        json: async () => ({
          device: { device_id: 'dev-phone-9', device_name: 'Pixel 9' },
          auth: { token: 'token-phone-9' },
        }),
      } as any);

      const res = await devMgr.confirmPairing('pair-sess-1', 'pair-secret-abc', 'Pixel 9', 'android');
      expect(res.device.device_id).toBe('dev-phone-9');
      expect(res.auth.token).toBe('token-phone-9');
    });
  });

  describe('TransferCoordinatorClient (Mode A & Mode B)', () => {
    let client: IdentityClient;
    let coord: TransferCoordinatorClient;

    beforeEach(() => {
      client = new IdentityClient('http://localhost:8080');
      coord = new TransferCoordinatorClient(client, 'http://localhost:8080');
    });

    it('TC-COORD-01: offers transfer in Mode B (Any Available Device)', async () => {
      const mockOffer = {
        request_id: 'xfer-req-101',
        recipient_user_id: 'u-recip',
        delivery_mode: 'ANY_AVAILABLE_DEVICE',
        room_code: 'K7X9QM',
        state: 'AWAITING_APPROVAL',
        files: [{ file_name: 'document.pdf', file_size: 1048576 }],
        total_bytes: 1048576,
      };

      vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce({
        ok: true,
        json: async () => mockOffer,
      } as any);

      const res = await coord.offerTransfer({
        recipientUserId: 'u-recip',
        files: [{ file_name: 'document.pdf', file_size: 1048576 }],
        deliveryMode: 'ANY_AVAILABLE_DEVICE',
      });

      expect(res.request_id).toBe('xfer-req-101');
      expect(res.delivery_mode).toBe('ANY_AVAILABLE_DEVICE');
      expect(res.room_code).toBe('K7X9QM');
    });

    it('TC-COORD-02: atomic claims transfer and completes it', async () => {
      vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce({
        ok: true,
        json: async () => ({
          request_id: 'xfer-req-101',
          claimed_by_device_id: 'dev-phone',
          state: 'ACCEPTED',
        }),
      } as any);

      const claimRes = await coord.claimTransfer('xfer-req-101', 'dev-phone');
      expect(claimRes.state).toBe('ACCEPTED');
      expect(claimRes.claimed_by_device_id).toBe('dev-phone');

      vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce({
        ok: true,
        json: async () => ({
          request_id: 'xfer-req-101',
          state: 'COMPLETED',
        }),
      } as any);

      const compRes = await coord.completeTransfer('xfer-req-101', 'COMPLETED');
      expect(compRes.state).toBe('COMPLETED');
    });
  });
});
