import { describe, it, expect, vi } from 'vitest';
import { WebRtcCoordinator, waitForBufferedAmountLow, fetchTurnConfig } from '../../src/core/transport/WebRtcCoordinator';
import { SignalingClient } from '../../src/core/transport/SignalingClient';

describe('WebRtcCoordinator & Transport Robustness Suite (DEF-03, DEF-04, DEF-07, DEF-12)', () => {
  it('TC-COOR-01: cleans up all signaling listeners on close() (DEF-04)', () => {
    const signaling = new SignalingClient('ws://127.0.0.1:8080/ws');
    const coordinator = new WebRtcCoordinator(signaling);

    // Fake setup
    coordinator['onIceCandidateHandler'] = vi.fn() as any;
    coordinator['onSdpAnswerHandler'] = vi.fn() as any;
    coordinator['onSdpOfferHandler'] = vi.fn() as any;
    coordinator['onErrorHandler'] = vi.fn() as any;
    coordinator['onPeerLeftHandler'] = vi.fn() as any;

    signaling.on('ice_candidate', coordinator['onIceCandidateHandler'] as any);
    signaling.on('sdp_answer', coordinator['onSdpAnswerHandler'] as any);
    signaling.on('sdp_offer', coordinator['onSdpOfferHandler'] as any);
    signaling.on('error', coordinator['onErrorHandler'] as any);
    signaling.on('peer_left', coordinator['onPeerLeftHandler'] as any);

    expect(signaling['listeners']['ice_candidate']?.length).toBe(1);
    expect(signaling['listeners']['sdp_answer']?.length).toBe(1);

    coordinator.close();

    expect(signaling['listeners']['ice_candidate']?.length || 0).toBe(0);
    expect(signaling['listeners']['sdp_answer']?.length || 0).toBe(0);
    expect(signaling['listeners']['sdp_offer']?.length || 0).toBe(0);
    expect(signaling['listeners']['error']?.length || 0).toBe(0);
    expect(signaling['listeners']['peer_left']?.length || 0).toBe(0);
  });

  it('TC-COOR-02: waitForBufferedAmountLow rejects when channel closes (DEF-07)', async () => {
    const listeners: Record<string, ((e: any) => void)[]> = {};
    const mockChannel = {
      bufferedAmount: 2 * 1024 * 1024,
      bufferedAmountLowThreshold: 0,
      addEventListener(type: string, fn: any) {
        if (!listeners[type]) listeners[type] = [];
        listeners[type].push(fn);
      },
      removeEventListener(type: string, fn: any) {
        if (listeners[type]) listeners[type] = listeners[type].filter(f => f !== fn);
      },
    };

    const waitPromise = waitForBufferedAmountLow(mockChannel as any, 1024 * 1024, 1000);

    // Simulate channel close
    setTimeout(() => {
      for (const h of listeners['close'] || []) h({});
    }, 10);

    await expect(waitPromise).rejects.toThrow(/closed while waiting/);
  });

  it('TC-COOR-03: waitForBufferedAmountLow times out if stalled (DEF-07)', async () => {
    const mockChannel = {
      bufferedAmount: 2 * 1024 * 1024,
      bufferedAmountLowThreshold: 0,
      addEventListener() {},
      removeEventListener() {},
    };

    await expect(waitForBufferedAmountLow(mockChannel as any, 1024 * 1024, 25)).rejects.toThrow(/Timeout/);
  });

  it('TC-COOR-04: fetchTurnConfig uses Authorization header without token in URL query (DEF-12)', async () => {
    let capturedUrl = '';
    let capturedHeaders: any = {};

    globalThis.fetch = vi.fn().mockImplementation(async (url: string, opts: any) => {
      capturedUrl = url;
      capturedHeaders = opts.headers;
      return {
        ok: true,
        json: async () => ({ ice_servers: [{ urls: 'turn:relay.example.com' }], ttl_seconds: 600 }),
      };
    }) as any;

    await fetchTurnConfig('http://localhost:8080', 'secret-room-token-xyz', 'client-123');

    // Token must NOT be in the query params
    expect(capturedUrl).not.toContain('token=secret-room-token-xyz');
    expect(capturedUrl).toContain('client_id=client-123');
    // Token MUST be in the Authorization header
    expect(capturedHeaders['Authorization']).toBe('Bearer secret-room-token-xyz');
  });
});
