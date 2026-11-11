import { describe, it, expect, beforeAll, afterAll } from 'vitest';
import { spawn, ChildProcess } from 'child_process';
import path from 'path';
import { fileURLToPath } from 'url';
import { SignalingClient, ErrorPayload } from '../../src/core/transport/SignalingClient';
import { fetchTurnConfig } from '../../src/core/transport/WebRtcCoordinator';
import { isValidRoomCode } from '../../src/core/session/RoomCodeNormalizer';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const ROOT_DIR = path.resolve(__dirname, '../../..');
const SIGNALING_BIN = process.env.SIGNALING_BIN || path.join(ROOT_DIR, 'backend/signaling/bin/signaling-server');
const WEB_DIST = path.join(ROOT_DIR, 'web/dist');

const TEST_PORT = 18095;
const SERVER_URL = `http://127.0.0.1:${TEST_PORT}`;
const WS_URL = `ws://127.0.0.1:${TEST_PORT}/ws/signaling`;

describe('Signaling & WebRTC Integration Suite (Live Go Daemon)', () => {
  let serverProcess: ChildProcess;

  beforeAll(async () => {
    // Spawn Go signaling server on dedicated test port
    serverProcess = spawn(
      SIGNALING_BIN,
      ['-port', String(TEST_PORT), '-web-dir', WEB_DIST],
      { stdio: 'ignore' }
    );

    // Poll /healthz until ready (up to 5 seconds)
    const startTime = Date.now();
    let ready = false;
    while (Date.now() - startTime < 5000) {
      try {
        const res = await fetch(`${SERVER_URL}/healthz`);
        if (res.ok) {
          const body = (await res.json()) as { status: string };
          if (body.status === 'ok') {
            ready = true;
            break;
          }
        }
      } catch {
        // Retry
      }
      await new Promise((r) => setTimeout(r, 100));
    }

    if (!ready) {
      if (serverProcess.pid) serverProcess.kill('SIGTERM');
      throw new Error(`Signaling server failed to become ready on ${SERVER_URL}/healthz`);
    }
  });

  afterAll(() => {
    if (serverProcess && serverProcess.pid) {
      serverProcess.kill('SIGTERM');
    }
  });

  it('TC-SIG-WEB-01: Ephemeral room creation produces valid Crockford Base32 room code and token', async () => {
    const client = new SignalingClient('test-initiator');
    await client.connect(WS_URL);

    const room = await client.createRoom();
    expect(room.room_code).toBeDefined();
    expect(room.room_code.startsWith('DROP-')).toBe(true);
    expect(isValidRoomCode(room.room_code)).toBe(true);
    expect(room.room_token).toBeDefined();
    expect(room.room_token.length).toBeGreaterThan(10);
    expect(room.role).toBe('initiator');

    client.close();
  });

  it('TC-SIG-WEB-02: Peer join and token-authorized TURN config access', async () => {
    const host = new SignalingClient('test-host-turn');
    const joiner = new SignalingClient('test-joiner-turn');

    await host.connect(WS_URL);
    await joiner.connect(WS_URL);

    const room = await host.createRoom();

    const joinerPromise = new Promise<{ peer_id: string; role: string }>((resolve) => {
      host.on('peer_joined', (p) => resolve(p));
    });

    const joinResult = await joiner.joinRoom(room.room_code);
    expect(joinResult.room_code).toBe(room.room_code);
    expect(joinResult.peer_id).toBe(host.id);
    expect(joinResult.role).toBe('joiner');

    const hostEvent = await joinerPromise;
    expect(hostEvent.peer_id).toBe(joiner.id);

    // Fetch TURN credentials using room token
    const turnConfig = await fetchTurnConfig(SERVER_URL, room.room_token, joiner.id);
    expect(turnConfig.iceServers).toBeDefined();
    expect(turnConfig.iceServers?.length).toBeGreaterThan(0);


    // Verify unauthenticated request fails
    await expect(fetchTurnConfig(SERVER_URL, 'invalid-token-xyz')).rejects.toThrow(/HTTP 401/);

    host.close();
    joiner.close();
  });

  it('TC-SIG-WEB-03: Glare prevention rejects illegal SDP offer from joiner', async () => {
    const host = new SignalingClient('test-host-glare');
    const joiner = new SignalingClient('test-joiner-glare');

    await host.connect(WS_URL);
    await joiner.connect(WS_URL);

    const room = await host.createRoom();
    await joiner.joinRoom(room.room_code);

    // Joiner illegally transmits SDP_OFFER (glare violation)
    const errorPromise = new Promise<ErrorPayload>((resolve) => {
      joiner.on('error', (err) => resolve(err));
    });

    joiner.sendSdpOffer('v=0\r\no=joiner 123 123 IN IP4 127.0.0.1');

    const err = await errorPromise;
    expect(err.code).toBe('ERR_GLARE_VIOLATION');

    host.close();
    joiner.close();
  });

  it('TC-SIG-WEB-04: Full SDP Offer/Answer and ICE candidate relay exchange', async () => {
    const host = new SignalingClient('test-host-sdp');
    const joiner = new SignalingClient('test-joiner-sdp');

    await host.connect(WS_URL);
    await joiner.connect(WS_URL);

    const room = await host.createRoom();
    await joiner.joinRoom(room.room_code);

    const offerPromise = new Promise<string>((resolve) => {
      joiner.on('sdp_offer', (p) => resolve(p.sdp));
    });
    const answerPromise = new Promise<string>((resolve) => {
      host.on('sdp_answer', (p) => resolve(p.sdp));
    });
    const icePromise = new Promise<string>((resolve) => {
      joiner.on('ice_candidate', (p) => resolve(p.candidate));
    });

    host.sendSdpOffer('v=0\r\no=host 111 222 IN IP4 127.0.0.1');
    const receivedOffer = await offerPromise;
    expect(receivedOffer).toContain('o=host');

    joiner.sendSdpAnswer('v=0\r\no=joiner 333 444 IN IP4 127.0.0.1');
    const receivedAnswer = await answerPromise;
    expect(receivedAnswer).toContain('o=joiner');

    host.sendIceCandidate({ candidate: 'candidate:1 1 UDP 2122260223 127.0.0.1 50000 typ host' });
    const receivedIce = await icePromise;
    expect(receivedIce).toContain('candidate:1');

    host.close();
    joiner.close();
  });

  it('TC-SIG-WEB-05: Static web distribution is served on root /', async () => {
    const res = await fetch(`${SERVER_URL}/`);
    expect(res.status).toBe(200);
    const html = await res.text();
    expect(html).toContain('LAN-Drop');
    expect(html).toContain('Web v2');
  });
});
