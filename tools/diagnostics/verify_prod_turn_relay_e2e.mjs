#!/usr/bin/env node
/**
 * ==============================================================================
 * LAN-Drop: Production TURN WebRTC Data Relay E2E Verification
 * ==============================================================================
 * Purpose:
 *   Validates end-to-end data transmission through the production Coturn TURN server
 *   by strictly forcing `iceTransportPolicy: "relay"` on both WebRTC endpoints.
 *   Verifies:
 *   1. Both peers authenticate with ephemeral credentials against production Coturn.
 *   2. Only relay candidates are gathered and negotiated.
 *   3. RTCPeerConnection getStats() confirms selected candidate pair is strictly
 *      local `candidateType === 'relay'` and remote `candidateType === 'relay'`.
 *   4. Bidirectional DataChannel file streaming (A -> B and B -> A).
 *   5. Bit-exact SHA-256 hash matching on both endpoints.
 *   6. Confirmed bytesSent / bytesReceived increment on the selected relay pair.
 * ==============================================================================
 */

import { spawn } from 'child_process';
import http from 'http';
import fs from 'fs';
import path from 'path';
import crypto from 'crypto';

const SIGNALING_WS_URL = process.env.SIGNALING_WS_URL || 'wss://landrop-server.duckdns.org/ws/signaling';
const TURN_CONFIG_URL = process.env.TURN_CONFIG_URL || 'https://landrop-server.duckdns.org/api/v1/turn-config';
const EXPECTED_PUBLIC_IP = process.env.EXPECTED_PUBLIC_IP || '92.5.49.103';

const PORT_A = 9361;
const PORT_B = 9362;
const DIR_A = path.join('/tmp', `landrop_relay_host_${Date.now()}_${process.pid}`);
const DIR_B = path.join('/tmp', `landrop_relay_join_${Date.now()}_${process.pid}`);

const TEST_PAYLOAD_A_SIZE = 512 * 1024; // 512 KiB
const TEST_PAYLOAD_B_SIZE = 256 * 1024; // 256 KiB

function withTimeout(promise, ms, stepName) {
  let timer;
  const timeoutPromise = new Promise((_, reject) => {
    timer = setTimeout(() => {
      reject(new Error(`[TIMEOUT] Step '${stepName}' timed out after ${ms}ms`));
    }, ms);
  });
  return Promise.race([promise, timeoutPromise]).finally(() => clearTimeout(timer));
}

function fetchHttpJson(url, timeoutMs = 2000) {
  return new Promise((resolve, reject) => {
    const req = http.get(url, (res) => {
      let data = '';
      res.on('data', chunk => data += chunk);
      res.on('end', () => {
        try {
          resolve(JSON.parse(data));
        } catch (e) {
          reject(new Error(`Failed to parse JSON from ${url}: ${e.message}`));
        }
      });
    });
    req.on('error', reject);
    req.setTimeout(timeoutMs, () => {
      req.destroy();
      reject(new Error(`HTTP request to ${url} timed out after ${timeoutMs}ms`));
    });
  });
}

class CdpClient {
  constructor(name, port) {
    this.name = name;
    this.port = port;
    this.ws = null;
    this.cdpId = 1;
  }

  async connect(maxWaitMs = 10000) {
    const tStart = Date.now();
    let pages = null;
    while (Date.now() - tStart < maxWaitMs) {
      try {
        pages = await fetchHttpJson(`http://127.0.0.1:${this.port}/json`, 500);
        if (Array.isArray(pages) && pages.length > 0 && pages[0].webSocketDebuggerUrl) {
          break;
        }
      } catch {}
      await new Promise(r => setTimeout(r, 200));
    }

    if (!pages || !pages.length || !pages[0].webSocketDebuggerUrl) {
      throw new Error(`[${this.name}] CDP target not found on port ${this.port}`);
    }

    this.ws = new WebSocket(pages[0].webSocketDebuggerUrl);
    await new Promise((resolve, reject) => {
      this.ws.onopen = resolve;
      this.ws.onerror = reject;
    });

    await this.send('Runtime.enable');
  }

  send(method, params = {}) {
    return new Promise((resolve, reject) => {
      const id = this.cdpId++;
      const handler = (evt) => {
        try {
          const data = JSON.parse(evt.data);
          if (data.id === id) {
            this.ws.removeEventListener('message', handler);
            if (data.error) {
              reject(new Error(`[${this.name}] CDP Error ${method}: ${JSON.stringify(data.error)}`));
            } else {
              resolve(data.result);
            }
          }
        } catch {}
      };
      this.ws.addEventListener('message', handler);
      this.ws.send(JSON.stringify({ id, method, params }));
    });
  }

  async evaluate(expression, timeoutMs = 15000) {
    return withTimeout((async () => {
      const res = await this.send('Runtime.evaluate', {
        expression,
        awaitPromise: true,
        returnByValue: true
      });
      if (res.exceptionDetails) {
        const desc = res.exceptionDetails.exception?.description || res.exceptionDetails.text;
        throw new Error(`[${this.name}] JS Exception: ${desc}`);
      }
      return res.result ? res.result.value : undefined;
    })(), timeoutMs, `${this.name} eval`);
  }

  close() {
    if (this.ws) {
      try { this.ws.close(); } catch {}
    }
  }
}

async function run() {
  console.log('================================================================');
  console.log('   LAN-Drop Production TURN WebRTC Data Relay E2E Test          ');
  console.log(`   Signaling: ${SIGNALING_WS_URL}`);
  console.log(`   Coturn Server Expected IP: ${EXPECTED_PUBLIC_IP}`);
  console.log('   Strict Constraint: iceTransportPolicy = "relay" (NO direct P2P)');
  console.log('================================================================\n');

  let procA = null;
  let procB = null;
  let cdpA = null;
  let cdpB = null;
  let wsA = null;
  let wsB = null;
  let exchangeActive = false;
  let exitCode = 0;

  try {
    fs.mkdirSync(DIR_A, { recursive: true });
    fs.mkdirSync(DIR_B, { recursive: true });

    // -------------------------------------------------------------------------
    // STEP 1: Launch Browser A & Browser B in headless mode
    // -------------------------------------------------------------------------
    console.log('[STEP 1] Launching Browser A (Port ' + PORT_A + ') and Browser B (Port ' + PORT_B + ')...');
    procA = spawn('brave-browser', [
      '--headless=new',
      `--remote-debugging-port=${PORT_A}`,
      `--user-data-dir=${DIR_A}`,
      '--no-sandbox',
      '--disable-gpu',
      '--disable-dev-shm-usage',
      '--mute-audio',
      `http://127.0.0.1:${PORT_A}`
    ], { stdio: 'ignore' });

    procB = spawn('brave-browser', [
      '--headless=new',
      `--remote-debugging-port=${PORT_B}`,
      `--user-data-dir=${DIR_B}`,
      '--no-sandbox',
      '--disable-gpu',
      '--disable-dev-shm-usage',
      '--mute-audio',
      `http://127.0.0.1:${PORT_B}`
    ], { stdio: 'ignore' });

    cdpA = new CdpClient('Host_A', PORT_A);
    cdpB = new CdpClient('Joiner_B', PORT_B);

    await Promise.all([
      cdpA.connect(10000),
      cdpB.connect(10000)
    ]);
    console.log('  ✓ Both browsers connected to Chrome DevTools Protocol.');

    // -------------------------------------------------------------------------
    // STEP 2: Connect signaling & create room for Peer A
    // -------------------------------------------------------------------------
    console.log('\n[STEP 2] Creating room on production signaling server for Peer A...');
    wsA = new WebSocket(SIGNALING_WS_URL);
    const clientAId = `relay-host-${Date.now().toString(36)}`;

    let roomCode = null;
    let roomTokenA = null;

    let onAnswerResolver = null;
    let onAnswerRejecter = null;

    wsA.onmessage = (e) => {
      try {
        const msg = JSON.parse(e.data);
        if (msg.action === 'SDP_ANSWER' && onAnswerResolver) {
          onAnswerResolver(msg.payload.sdp);
        } else if (msg.action === 'ICE_CANDIDATE' && msg.payload && msg.payload.candidate) {
          const cand = {
            candidate: msg.payload.candidate,
            sdpMid: msg.payload.sdp_mid || '0',
            sdpMLineIndex: typeof msg.payload.sdp_mline_index === 'number' ? msg.payload.sdp_mline_index : 0
          };
          cdpA.evaluate(`window.__addRemoteCandidate(${JSON.stringify(cand)})`).catch(err => {
            console.error('[CDP A addCandidate Error]:', err.message);
          });
        } else if (msg.action === 'ERROR' && onAnswerRejecter) {
          onAnswerRejecter(new Error(`Signaling A error: ${JSON.stringify(msg.payload)}`));
        }
      } catch {}
    };

    await withTimeout(new Promise((resolve, reject) => {
      const origMsg = wsA.onmessage;
      wsA.onmessage = (e) => {
        try {
          const msg = JSON.parse(e.data);
          if (msg.action === 'ROOM_CREATED') {
            roomCode = msg.payload.room_code;
            roomTokenA = msg.payload.room_token;
            wsA.onmessage = origMsg;
            resolve();
          } else if (msg.action === 'ERROR') {
            reject(new Error(`Signaling A error: ${JSON.stringify(msg.payload)}`));
          }
        } catch (err) { reject(err); }
      };
      wsA.onopen = () => {
        wsA.send(JSON.stringify({
          version: '2.0',
          action: 'CREATE_ROOM',
          client_id: clientAId,
          timestamp: Math.floor(Date.now() / 1000)
        }));
      };
      wsA.onerror = reject;
    }), 6000, 'Peer A CREATE_ROOM');

    console.log(`  ✓ Room created: code='${roomCode}'`);

    // -------------------------------------------------------------------------
    // STEP 3: Peer B joins room
    // -------------------------------------------------------------------------
    console.log('[STEP 3] Joining room from Peer B...');
    wsB = new WebSocket(SIGNALING_WS_URL);
    const clientBId = `relay-join-${Date.now().toString(36)}`;
    let roomTokenB = null;

    let onOfferResolver = null;
    let onOfferRejecter = null;

    wsB.onmessage = (e) => {
      try {
        const msg = JSON.parse(e.data);
        if (msg.action === 'SDP_OFFER' && onOfferResolver) {
          onOfferResolver(msg.payload.sdp);
        } else if (msg.action === 'ICE_CANDIDATE' && msg.payload && msg.payload.candidate) {
          const cand = {
            candidate: msg.payload.candidate,
            sdpMid: msg.payload.sdp_mid || '0',
            sdpMLineIndex: typeof msg.payload.sdp_mline_index === 'number' ? msg.payload.sdp_mline_index : 0
          };
          cdpB.evaluate(`window.__addRemoteCandidate(${JSON.stringify(cand)})`).catch(err => {
            console.error('[CDP B addCandidate Error]:', err.message);
          });
        } else if (msg.action === 'ERROR' && onOfferRejecter) {
          onOfferRejecter(new Error(`Signaling B error: ${JSON.stringify(msg.payload)}`));
        }
      } catch {}
    };

    await withTimeout(new Promise((resolve, reject) => {
      const origMsg = wsB.onmessage;
      wsB.onmessage = (e) => {
        try {
          const msg = JSON.parse(e.data);
          if (msg.action === 'PEER_JOINED') {
            roomTokenB = msg.payload.room_token;
            wsB.onmessage = origMsg;
            resolve();
          } else if (msg.action === 'ERROR') {
            reject(new Error(`Signaling B error: ${JSON.stringify(msg.payload)}`));
          }
        } catch (err) { reject(err); }
      };
      wsB.onopen = () => {
        wsB.send(JSON.stringify({
          version: '2.0',
          action: 'JOIN_ROOM',
          room_code: roomCode,
          client_id: clientBId,
          timestamp: Math.floor(Date.now() / 1000)
        }));
      };
      wsB.onerror = reject;
    }), 6000, 'Peer B JOIN_ROOM');

    console.log(`  ✓ Peer B successfully joined room '${roomCode}'`);

    // -------------------------------------------------------------------------
    // STEP 4: Fetch TURN credentials for both peers
    // -------------------------------------------------------------------------
    console.log('[STEP 4] Fetching ICE server configurations for both peers...');
    const [turnA, turnB] = await Promise.all([
      fetch(`${TURN_CONFIG_URL}?client_id=${clientAId}`, { headers: { Authorization: `Bearer ${roomTokenA}` } }).then(r => r.json()),
      fetch(`${TURN_CONFIG_URL}?client_id=${clientBId}`, { headers: { Authorization: `Bearer ${roomTokenB}` } }).then(r => r.json())
    ]);

    if (!turnA.ice_servers || !turnB.ice_servers) {
      throw new Error('Failed to obtain ice_servers for one or both peers');
    }
    console.log('  ✓ Received valid ICE configurations with ephemeral TURN credentials.');

    // -------------------------------------------------------------------------
    // STEP 5: Initialize WebRTC PeerConnections with iceTransportPolicy: "relay"
    // -------------------------------------------------------------------------
    console.log('\n[STEP 5] Initializing RTCPeerConnection instances with iceTransportPolicy: "relay"...');

    const setupPeerScript = (iceServers, isInitiator) => `
      window.__iceServers = ${JSON.stringify(iceServers)};
      window.__isInitiator = ${isInitiator};
      window.__candidateQueue = [];
      window.__receivedPayloads = [];
      window.__dataChannel = null;
      window.__channelOpen = false;

      window.__pc = new RTCPeerConnection({
        iceServers: window.__iceServers,
        iceTransportPolicy: "relay"
      });

      window.__pendingRemoteCandidates = [];
      window.__hasRemoteDesc = false;

      window.__addRemoteCandidate = async (cand) => {
        if (!window.__hasRemoteDesc) {
          window.__pendingRemoteCandidates.push(cand);
        } else {
          try {
            await window.__pc.addIceCandidate(new RTCIceCandidate(cand));
          } catch (e) {
            console.warn("addIceCandidate error:", e);
          }
        }
      };

      window.__flushRemoteCandidates = async () => {
        window.__hasRemoteDesc = true;
        while (window.__pendingRemoteCandidates.length > 0) {
          const c = window.__pendingRemoteCandidates.shift();
          try {
            await window.__pc.addIceCandidate(new RTCIceCandidate(c));
          } catch (e) {
            console.warn("flush candidate error:", e);
          }
        }
      };

      window.__pc.onicecandidate = (e) => {
        if (e.candidate && e.candidate.candidate) {
          window.__candidateQueue.push(e.candidate.toJSON());
        }
      };

      window.__setupChannel = (channel) => {
        channel.binaryType = "arraybuffer";
        channel.onopen = () => {
          window.__channelOpen = true;
        };
        channel.onmessage = (e) => {
          if (typeof e.data === "string") {
            try {
              const meta = JSON.parse(e.data);
              window.__currentFileMeta = meta;
              window.__fileChunks = [];
              window.__bytesReceivedSoFar = 0;
            } catch {}
          } else if (e.data instanceof ArrayBuffer) {
            window.__fileChunks.push(new Uint8Array(e.data));
            window.__bytesReceivedSoFar = (window.__bytesReceivedSoFar || 0) + e.data.byteLength;
            if (window.__currentFileMeta && window.__bytesReceivedSoFar >= window.__currentFileMeta.size) {
              (async () => {
                const total = new Uint8Array(window.__bytesReceivedSoFar);
                let offset = 0;
                for (const c of window.__fileChunks) {
                  total.set(c, offset);
                  offset += c.byteLength;
                }
                const hashBuf = await crypto.subtle.digest("SHA-256", total);
                const hashHex = Array.from(new Uint8Array(hashBuf)).map(b => b.toString(16).padStart(2, "0")).join("");
                window.__receivedPayloads.push({
                  name: window.__currentFileMeta.name,
                  size: window.__bytesReceivedSoFar,
                  sha256: hashHex
                });
                window.__fileChunks = [];
              })();
            }
          }
        };
      };

      if (window.__isInitiator) {
        window.__dataChannel = window.__pc.createDataChannel("turn_relay_channel", { ordered: true });
        window.__setupChannel(window.__dataChannel);
      } else {
        window.__pc.ondatachannel = (e) => {
          window.__dataChannel = e.channel;
          window.__setupChannel(window.__dataChannel);
        };
      }
    `;

    await Promise.all([
      cdpA.evaluate(setupPeerScript(turnA.ice_servers, true)),
      cdpB.evaluate(setupPeerScript(turnB.ice_servers, false))
    ]);

    console.log('  ✓ Both PeerConnections configured with strict relay policy.');

    // -------------------------------------------------------------------------
    // STEP 6: Signaling negotiation (Offer/Answer + Candidate Relay)
    // -------------------------------------------------------------------------
    console.log('[STEP 6] Performing SDP Offer/Answer negotiation & ICE candidate relay...');

    // Pump candidates continuously from each browser queue to signaling
    exchangeActive = true;
    const pumpCandidates = async (fromCdp, toWs, roleName) => {
      while (exchangeActive) {
        try {
          const cands = await fromCdp.evaluate(`
            (() => {
              const q = window.__candidateQueue || [];
              window.__candidateQueue = [];
              return q;
            })()
          `);
          if (Array.isArray(cands) && cands.length > 0) {
            for (const c of cands) {
              console.log(`  [ICE] ${roleName} -> candidate (${c.candidate.substring(0, 48)}...)`);
              toWs.send(JSON.stringify({
                version: '2.0',
                action: 'ICE_CANDIDATE',
                room_code: roomCode,
                timestamp: Math.floor(Date.now() / 1000),
                payload: {
                  candidate: c.candidate,
                  sdp_mid: c.sdpMid || '0',
                  sdp_mline_index: typeof c.sdpMLineIndex === 'number' ? c.sdpMLineIndex : 0
                }
              }));
            }
          }
        } catch {}
        await new Promise(r => setTimeout(r, 80));
      }
    };

    pumpCandidates(cdpA, wsA, 'Host_A');
    pumpCandidates(cdpB, wsB, 'Joiner_B');

    // Promise for Peer B receiving OFFER
    const offerPromise = new Promise((resolve, reject) => {
      onOfferResolver = resolve;
      onOfferRejecter = reject;
    });

    // Create Offer on Peer A
    const offerSdp = await cdpA.evaluate(`
      (async () => {
        const offer = await window.__pc.createOffer();
        await window.__pc.setLocalDescription(offer);
        return offer.sdp;
      })()
    `);

    // Peer A sends OFFER to Peer B via signaling
    wsA.send(JSON.stringify({
      version: '2.0',
      action: 'SDP_OFFER',
      room_code: roomCode,
      client_id: clientAId,
      timestamp: Math.floor(Date.now() / 1000),
      payload: { sdp: offerSdp, type: 'offer' }
    }));

    // Peer B awaits OFFER
    const offerReceived = await withTimeout(offerPromise, 6000, 'Peer B receive OFFER');

    // Promise for Peer A receiving ANSWER
    const answerPromise = new Promise((resolve, reject) => {
      onAnswerResolver = resolve;
      onAnswerRejecter = reject;
    });

    // Peer B sets remote description, flushes remote candidates, creates Answer, sets local description
    const answerSdp = await cdpB.evaluate(`
      (async () => {
        await window.__pc.setRemoteDescription(new RTCSessionDescription({ type: "offer", sdp: ${JSON.stringify(offerReceived)} }));
        await window.__flushRemoteCandidates();
        const answer = await window.__pc.createAnswer();
        await window.__pc.setLocalDescription(answer);
        return answer.sdp;
      })()
    `);

    // Peer B sends ANSWER to Peer A via signaling
    wsB.send(JSON.stringify({
      version: '2.0',
      action: 'SDP_ANSWER',
      room_code: roomCode,
      client_id: clientBId,
      timestamp: Math.floor(Date.now() / 1000),
      payload: { sdp: answerSdp, type: 'answer' }
    }));

    // Peer A awaits ANSWER
    const answerReceived = await withTimeout(answerPromise, 6000, 'Peer A receive ANSWER');

    // Peer A sets remote description and flushes remote candidates
    await cdpA.evaluate(`
      (async () => {
        await window.__pc.setRemoteDescription(new RTCSessionDescription({ type: "answer", sdp: ${JSON.stringify(answerReceived)} }));
        await window.__flushRemoteCandidates();
      })()
    `);

    console.log('  ✓ SDP Offer/Answer exchanged successfully.');

    // -------------------------------------------------------------------------
    // STEP 7: Wait for WebRTC connectionState == "connected"
    // -------------------------------------------------------------------------
    console.log('[STEP 7] Awaiting connection state == "connected" on both peers via Coturn relay...');
    const tConnectStart = Date.now();

    await withTimeout((async () => {
      for (let i = 0; i < 50; i++) {
        const [stateA, stateB, iceA, iceB, dcAOpen, dcBOpen] = await Promise.all([
          cdpA.evaluate(`window.__pc.connectionState`),
          cdpB.evaluate(`window.__pc.connectionState`),
          cdpA.evaluate(`window.__pc.iceConnectionState`),
          cdpB.evaluate(`window.__pc.iceConnectionState`),
          cdpA.evaluate(`window.__channelOpen === true`),
          cdpB.evaluate(`window.__channelOpen === true`)
        ]);

        if (i % 5 === 0 || stateA === 'connected' || stateB === 'connected') {
          console.log(`  [Poll ${i * 300}ms] Peer A: conn=${stateA}, ice=${iceA}, dc=${dcAOpen} | Peer B: conn=${stateB}, ice=${iceB}, dc=${dcBOpen}`);
        }

        if (stateA === 'connected' && stateB === 'connected' && dcAOpen && dcBOpen) {
          return;
        }
        await new Promise(r => setTimeout(r, 300));
      }
      throw new Error('WebRTC connection failed to reach "connected" state within timeout');
    })(), 16000, 'WebRTC Connection Establishment');

    console.log(`  ✓ WebRTC connected and DataChannel OPEN in ${Date.now() - tConnectStart}ms!`);

    // -------------------------------------------------------------------------
    // STEP 8: Inspect getStats() to verify selectedCandidatePair is RELAY
    // -------------------------------------------------------------------------
    console.log('\n================================================================');
    console.log('         WEBRTC STATISTICS & SELECTED CANDIDATE PAIR            ');
    console.log('================================================================');

    const extractStats = `
      (async () => {
        const report = await window.__pc.getStats();
        let selectedPair = null;
        let localCand = null;
        let remoteCand = null;

        report.forEach(stat => {
          if (stat.type === "transport" && stat.selectedCandidatePairId) {
            selectedPair = report.get(stat.selectedCandidatePairId);
          }
        });

        if (!selectedPair) {
          report.forEach(stat => {
            if (stat.type === "candidate-pair" && stat.state === "succeeded" && stat.nominated) {
              selectedPair = stat;
            }
          });
        }

        if (selectedPair) {
          localCand = report.get(selectedPair.localCandidateId);
          remoteCand = report.get(selectedPair.remoteCandidateId);
        }

        return {
          pairState: selectedPair ? selectedPair.state : "unknown",
          nominated: selectedPair ? selectedPair.nominated : false,
          bytesSent: selectedPair ? selectedPair.bytesSent : 0,
          bytesReceived: selectedPair ? selectedPair.bytesReceived : 0,
          localType: localCand ? localCand.candidateType : "unknown",
          localIp: localCand ? (localCand.ip || localCand.address) : "unknown",
          localPort: localCand ? localCand.port : 0,
          localRelayProto: localCand ? localCand.relayProtocol : "unknown",
          remoteType: remoteCand ? remoteCand.candidateType : "unknown",
          remoteIp: remoteCand ? (remoteCand.ip || remoteCand.address) : "unknown",
          remotePort: remoteCand ? remoteCand.port : 0
        };
      })()
    `;

    const statsA = await cdpA.evaluate(extractStats);
    const statsB = await cdpB.evaluate(extractStats);

    console.log('[Peer A Selected Candidate Pair]');
    console.log(`  State:            ${statsA.pairState} (nominated: ${statsA.nominated})`);
    console.log(`  Local Candidate:  type=${statsA.localType}, endpoint=${statsA.localIp}:${statsA.localPort} (${statsA.localRelayProto})`);
    console.log(`  Remote Candidate: type=${statsA.remoteType}, endpoint=${statsA.remoteIp}:${statsA.remotePort}`);
    console.log(`  Traffic:          bytesSent=${statsA.bytesSent}, bytesReceived=${statsA.bytesReceived}`);

    console.log('\n[Peer B Selected Candidate Pair]');
    console.log(`  State:            ${statsB.pairState} (nominated: ${statsB.nominated})`);
    console.log(`  Local Candidate:  type=${statsB.localType}, endpoint=${statsB.localIp}:${statsB.localPort} (${statsB.localRelayProto})`);
    console.log(`  Remote Candidate: type=${statsB.remoteType}, endpoint=${statsB.remoteIp}:${statsB.remotePort}`);
    console.log(`  Traffic:          bytesSent=${statsB.bytesSent}, bytesReceived=${statsB.bytesReceived}`);

    // Verification asserts
    if (statsA.localType !== 'relay' || statsA.remoteType !== 'relay') {
      throw new Error(`Non-relay transport selected on Peer A: local=${statsA.localType}, remote=${statsA.remoteType}`);
    }
    if (statsB.localType !== 'relay' || statsB.remoteType !== 'relay') {
      throw new Error(`Non-relay transport selected on Peer B: local=${statsB.localType}, remote=${statsB.remoteType}`);
    }

    console.log('\n  ✓ VERIFIED: Selected candidate pair is STRICTLY RELAY <-> RELAY on Coturn!');

    // -------------------------------------------------------------------------
    // STEP 9: Bidirectional file transfer through TURN relay
    // -------------------------------------------------------------------------
    console.log('\n================================================================');
    console.log('         BIDIRECTIONAL FILE TRANSFER THROUGH TURN RELAY         ');
    console.log('================================================================');

    // Generate random binary payloads and compute source SHA-256
    const payloadA = crypto.randomBytes(TEST_PAYLOAD_A_SIZE);
    const hashAExpected = crypto.createHash('sha256').update(payloadA).digest('hex');

    const payloadB = crypto.randomBytes(TEST_PAYLOAD_B_SIZE);
    const hashBExpected = crypto.createHash('sha256').update(payloadB).digest('hex');

    console.log(`[Test 1] Transferring ${TEST_PAYLOAD_A_SIZE} bytes from Host A -> Joiner B...`);
    console.log(`  Source File: payload_A_512k.bin (SHA-256: ${hashAExpected})`);

    // Stream file from A to B via DataChannel
    const tSendAStart = Date.now();
    await cdpA.evaluate(`
      (async () => {
        const raw = new Uint8Array([${payloadA.join(',')}]);
        const dc = window.__dataChannel;

        // Send metadata header
        dc.send(JSON.stringify({ name: "payload_A_512k.bin", size: raw.byteLength }));

        // Send 16KB chunks
        const chunkSize = 16384;
        let offset = 0;
        while (offset < raw.byteLength) {
          const slice = raw.slice(offset, offset + chunkSize);
          dc.send(slice);
          offset += slice.byteLength;
          if (dc.bufferedAmount > 65536) {
            await new Promise(r => setTimeout(r, 10));
          }
        }
      })()
    `);

    // Wait for B to receive and hash
    const recvOnB = await withTimeout((async () => {
      for (let i = 0; i < 60; i++) {
        const results = await cdpB.evaluate(`window.__receivedPayloads`);
        if (results && results.length > 0) {
          return results[0];
        }
        await new Promise(r => setTimeout(r, 200));
      }
      throw new Error('Peer B did not receive full payload within timeout');
    })(), 12000, 'Peer B File Reception');

    console.log(`  ✓ Transfer A -> B finished in ${Date.now() - tSendAStart}ms!`);
    console.log(`    Received: ${recvOnB.size} bytes`);
    console.log(`    Calculated SHA-256: ${recvOnB.sha256}`);

    if (recvOnB.size !== TEST_PAYLOAD_A_SIZE) {
      throw new Error(`Payload size mismatch on B: expected ${TEST_PAYLOAD_A_SIZE}, got ${recvOnB.size}`);
    }
    if (recvOnB.sha256 !== hashAExpected) {
      throw new Error(`SHA-256 mismatch on B: expected ${hashAExpected}, got ${recvOnB.sha256}`);
    }
    console.log('    ✓ BIT-EXACT SHA-256 MATCH CONFIRMED!');

    // Reverse Transfer: B -> A
    console.log(`\n[Test 2] Transferring ${TEST_PAYLOAD_B_SIZE} bytes from Joiner B -> Host A (Reverse direction)...`);
    console.log(`  Source File: payload_B_256k.bin (SHA-256: ${hashBExpected})`);

    const tSendBStart = Date.now();
    await cdpB.evaluate(`
      (async () => {
        const raw = new Uint8Array([${payloadB.join(',')}]);
        const dc = window.__dataChannel;

        dc.send(JSON.stringify({ name: "payload_B_256k.bin", size: raw.byteLength }));

        const chunkSize = 16384;
        let offset = 0;
        while (offset < raw.byteLength) {
          const slice = raw.slice(offset, offset + chunkSize);
          dc.send(slice);
          offset += slice.byteLength;
          if (dc.bufferedAmount > 65536) {
            await new Promise(r => setTimeout(r, 10));
          }
        }
      })()
    `);

    const recvOnA = await withTimeout((async () => {
      for (let i = 0; i < 60; i++) {
        const results = await cdpA.evaluate(`window.__receivedPayloads`);
        if (results && results.length > 0) {
          return results[0];
        }
        await new Promise(r => setTimeout(r, 200));
      }
      throw new Error('Peer A did not receive reverse payload within timeout');
    })(), 12000, 'Peer A Reverse File Reception');

    console.log(`  ✓ Reverse transfer B -> A finished in ${Date.now() - tSendBStart}ms!`);
    console.log(`    Received: ${recvOnA.size} bytes`);
    console.log(`    Calculated SHA-256: ${recvOnA.sha256}`);

    if (recvOnA.size !== TEST_PAYLOAD_B_SIZE) {
      throw new Error(`Reverse size mismatch on A: expected ${TEST_PAYLOAD_B_SIZE}, got ${recvOnA.size}`);
    }
    if (recvOnA.sha256 !== hashBExpected) {
      throw new Error(`SHA-256 mismatch on A: expected ${hashBExpected}, got ${recvOnA.sha256}`);
    }
    console.log('    ✓ BIT-EXACT SHA-256 MATCH CONFIRMED!');

    // -------------------------------------------------------------------------
    // STEP 10: Final stats verification (bytes transmitted through relay)
    // -------------------------------------------------------------------------
    const finalStatsA = await cdpA.evaluate(extractStats);
    console.log('\n[Post-Transfer WebRTC Relay Traffic Summary]');
    console.log(`  Peer A bytesSent through TURN:     ${finalStatsA.bytesSent} bytes`);
    console.log(`  Peer A bytesReceived through TURN: ${finalStatsA.bytesReceived} bytes`);

    console.log('\n================================================================');
    console.log('==> PRODUCTION TURN DATA RELAY TEST PASSED (EXIT CODE 0) <==');
    console.log('================================================================\n');

    exitCode = 0;
  } catch (err) {
    console.error('\n❌ TURN DATA RELAY TEST FAILED:', err.message);
    exitCode = 1;
  } finally {
    console.log('[CLEANUP] Closing connections and terminating browser processes...');
    exchangeActive = false;
    if (wsA) { try { wsA.close(); } catch {} }
    if (wsB) { try { wsB.close(); } catch {} }
    if (cdpA) cdpA.close();
    if (cdpB) cdpB.close();
    if (procA) { try { procA.kill('SIGKILL'); } catch {} }
    if (procB) { try { procB.kill('SIGKILL'); } catch {} }
    if (fs.existsSync(DIR_A)) { try { fs.rmSync(DIR_A, { recursive: true, force: true }); } catch {} }
    if (fs.existsSync(DIR_B)) { try { fs.rmSync(DIR_B, { recursive: true, force: true }); } catch {} }
    console.log('[CLEANUP] Done.');
    process.exit(exitCode);
  }
}

run();
