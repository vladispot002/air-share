#!/usr/bin/env node
/**
 * ==============================================================================
 * LAN-Drop: Production Coturn TURN Relay Diagnostic Probe
 * ==============================================================================
 * Purpose:
 *   Validates that the production Coturn TURN server is reachable, authenticates
 *   ephemeral HMAC credentials issued by the signaling service, and allocates
 *   valid relay candidates bound to the production public IP (92.5.49.103).
 *
 * Safety & Privacy:
 *   - Never prints room tokens or plaintext TURN credentials to stdout/stderr.
 *   - Strictly enforces per-step timeouts to prevent event-loop deadlocks.
 *   - Automatically terminates child processes and cleans up temporary directories.
 * ==============================================================================
 */

import { spawn } from 'child_process';
import http from 'http';
import fs from 'fs';
import path from 'path';

const SIGNALING_WS_URL = process.env.SIGNALING_WS_URL || 'wss://landrop-server.duckdns.org/ws/signaling';
const TURN_CONFIG_URL = process.env.TURN_CONFIG_URL || 'https://landrop-server.duckdns.org/api/v1/turn-config';
const EXPECTED_PUBLIC_IP = process.env.EXPECTED_PUBLIC_IP || '92.5.49.103';
const CDP_PORT = parseInt(process.env.CDP_PORT || '9348', 10);
const USER_DATA_DIR = path.join('/tmp', `landrop_turn_diag_${Date.now()}_${process.pid}`);

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

async function run() {
  console.log('================================================================');
  console.log('   LAN-Drop Production Coturn TURN Relay Diagnostic Probe       ');
  console.log(`   Signaling: ${SIGNALING_WS_URL}`);
  console.log(`   Expected Public IP: ${EXPECTED_PUBLIC_IP}`);
  console.log('================================================================\n');

  let ws = null;
  let cdpWs = null;
  let braveProc = null;
  let exitCode = 0;

  try {
    // -------------------------------------------------------------------------
    // STEP 1: Connect WebSocket to /ws/signaling
    // -------------------------------------------------------------------------
    console.log('[STEP 1] Connecting WebSocket to /ws/signaling...');
    const tStartWs = Date.now();
    ws = new WebSocket(SIGNALING_WS_URL);

    await withTimeout(new Promise((resolve, reject) => {
      ws.onopen = () => {
        console.log(`  ✓ WebSocket connected in ${Date.now() - tStartWs}ms`);
        resolve();
      };
      ws.onerror = (err) => reject(new Error(`WebSocket connection error: ${err.message || 'connection failed'}`));
    }), 5000, 'WebSocket Connection');

    // -------------------------------------------------------------------------
    // STEP 2 & 3: Request Room Creation via CREATE_ROOM action
    // -------------------------------------------------------------------------
    console.log('[STEP 2 & 3] Requesting room creation via CREATE_ROOM action...');
    const clientId = `probe-diag-${Date.now().toString(36)}`;
    const createPayload = {
      version: '2.0',
      action: 'CREATE_ROOM',
      client_id: clientId,
      timestamp: Math.floor(Date.now() / 1000)
    };

    let roomToken = null;
    let roomCode = null;

    await withTimeout(new Promise((resolve, reject) => {
      ws.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data);
          console.log(`  <- Signaling event received: action='${msg.action || 'UNKNOWN'}'`);

          if (msg.action === 'ERROR') {
            const errCode = msg.payload?.code || 'UNKNOWN_ERROR';
            const errMsg = msg.payload?.message || JSON.stringify(msg.payload);
            reject(new Error(`Signaling server returned ERROR: [${errCode}] ${errMsg}`));
            return;
          }

          if (msg.action === 'ROOM_CREATED') {
            roomCode = msg.payload?.room_code;
            roomToken = msg.payload?.room_token;
            if (!roomCode || !roomToken) {
              reject(new Error('ROOM_CREATED payload missing room_code or room_token'));
              return;
            }
            console.log(`  ✓ Room created: code='${roomCode}', token=[REDACTED (${roomToken.length} chars)]`);
            resolve();
          }
        } catch (e) {
          reject(new Error(`Failed to parse signaling message: ${e.message}`));
        }
      };

      ws.onerror = (err) => reject(new Error(`WebSocket error during room creation: ${err.message || 'unknown'}`));
      ws.send(JSON.stringify(createPayload));
    }), 5000, 'Room Creation & Token Acquisition');

    // -------------------------------------------------------------------------
    // STEP 4 & 5: Query /api/v1/turn-config with Authorization token
    // -------------------------------------------------------------------------
    console.log('[STEP 4 & 5] Fetching ICE server configuration from /api/v1/turn-config...');
    const turnConfigUrl = `${TURN_CONFIG_URL}?client_id=${encodeURIComponent(clientId)}`;
    const turnRes = await withTimeout(
      fetch(turnConfigUrl, {
        method: 'GET',
        headers: { 'Authorization': `Bearer ${roomToken}` }
      }),
      5000,
      'Fetch /api/v1/turn-config'
    );

    console.log(`  <- HTTP Status: ${turnRes.status} ${turnRes.statusText}`);
    if (!turnRes.ok) {
      const errBody = await turnRes.text();
      throw new Error(`Failed to fetch TURN config (HTTP ${turnRes.status}): ${errBody}`);
    }

    const configData = await turnRes.json();

    // -------------------------------------------------------------------------
    // STEP 6: Validate structure of ice_servers
    // -------------------------------------------------------------------------
    console.log('[STEP 6] Validating ICE server configuration schema...');
    const iceServers = configData.ice_servers;
    if (!Array.isArray(iceServers) || iceServers.length === 0) {
      throw new Error(`Invalid ice_servers configuration returned: ${JSON.stringify(configData)}`);
    }

    console.log(`  ✓ Received ${iceServers.length} ICE server groups. TTL: ${configData.ttl_seconds}s`);
    let foundTurnRelay = false;
    for (let i = 0; i < iceServers.length; i++) {
      const entry = iceServers[i];
      const urls = Array.isArray(entry.urls) ? entry.urls : [entry.urls];
      const hasTurn = urls.some(u => typeof u === 'string' && u.startsWith('turn:'));
      if (hasTurn) {
        foundTurnRelay = true;
        const maskedUsername = entry.username ? `${entry.username.substring(0, 10)}...` : '<missing>';
        const maskedCred = entry.credential ? '[REDACTED_HMAC_CREDENTIAL]' : '<missing>';
        console.log(`    [Server ${i + 1}] URLs: ${JSON.stringify(urls)}`);
        console.log(`                 Username: ${maskedUsername}, Credential: ${maskedCred}`);
      } else {
        console.log(`    [Server ${i + 1}] STUN URLs: ${JSON.stringify(urls)}`);
      }
    }

    if (!foundTurnRelay) {
      throw new Error('No valid TURN relay server found in ice_servers configuration!');
    }

    // -------------------------------------------------------------------------
    // STEP 7: Launch Brave headless and connect to CDP
    // -------------------------------------------------------------------------
    console.log('\n[STEP 7] Launching headless browser to probe TURN candidate allocation...');
    fs.mkdirSync(USER_DATA_DIR, { recursive: true });

    braveProc = spawn('brave-browser', [
      '--headless=new',
      `--remote-debugging-port=${CDP_PORT}`,
      `--user-data-dir=${USER_DATA_DIR}`,
      '--no-sandbox',
      '--disable-gpu',
      '--disable-dev-shm-usage',
      '--mute-audio',
      'about:blank'
    ], { stdio: 'ignore' });

    braveProc.on('error', (err) => {
      console.error('  ✗ Failed to start brave-browser process:', err.message);
    });

    console.log(`  ✓ Browser process launched (PID: ${braveProc.pid}). Polling CDP on port ${CDP_PORT}...`);

    let cdpPages = null;
    await withTimeout((async () => {
      for (let attempt = 0; attempt < 40; attempt++) {
        try {
          cdpPages = await fetchHttpJson(`http://127.0.0.1:${CDP_PORT}/json`, 500);
          if (Array.isArray(cdpPages) && cdpPages.length > 0 && cdpPages[0].webSocketDebuggerUrl) {
            return;
          }
        } catch {
          // Retry
        }
        await new Promise(r => setTimeout(r, 200));
      }
      throw new Error(`CDP target not available on port ${CDP_PORT} after 8000ms`);
    })(), 9000, 'CDP Discovery');

    const wsDebuggerUrl = cdpPages[0].webSocketDebuggerUrl;
    console.log('  ✓ CDP endpoint discovered. Connecting CDP WebSocket...');

    cdpWs = new WebSocket(wsDebuggerUrl);
    await withTimeout(new Promise((resolve, reject) => {
      cdpWs.onopen = () => resolve();
      cdpWs.onerror = (err) => reject(new Error(`CDP WebSocket error: ${err.message}`));
    }), 4000, 'CDP WebSocket Connect');

    console.log('  ✓ Connected to Chrome DevTools Protocol session.');

    let cdpId = 1;
    const cdpSend = (method, params = {}) => {
      return new Promise((resolve, reject) => {
        const id = cdpId++;
        const handler = (evt) => {
          try {
            const data = JSON.parse(evt.data);
            if (data.id === id) {
              cdpWs.removeEventListener('message', handler);
              if (data.error) {
                reject(new Error(`CDP Error [${method}]: ${data.error.message || JSON.stringify(data.error)}`));
              } else {
                resolve(data.result);
              }
            }
          } catch {}
        };
        cdpWs.addEventListener('message', handler);
        cdpWs.send(JSON.stringify({ id, method, params }));
      });
    };

    await cdpSend('Runtime.enable');

    // -------------------------------------------------------------------------
    // STEP 8 & 9: Execute WebRTC relay allocation and wait for candidates
    // -------------------------------------------------------------------------
    console.log('[STEP 8 & 9] Initiating RTCPeerConnection with iceTransportPolicy: "relay"...');

    const probeCode = `
      (async () => {
        return new Promise(async (resolve, reject) => {
          try {
            const gatheredCandidates = [];
            let isResolved = false;

            const safeResolve = (reason) => {
              if (isResolved) return;
              isResolved = true;
              resolve({
                candidates: gatheredCandidates,
                gatheringState: pc.iceGatheringState,
                completionReason: reason
              });
            };

            const pc = new RTCPeerConnection({
              iceServers: ${JSON.stringify(iceServers)},
              iceTransportPolicy: "relay"
            });

            pc.onicecandidate = (event) => {
              if (event.candidate && event.candidate.candidate) {
                gatheredCandidates.push(event.candidate.candidate);
              } else if (!event.candidate) {
                // End of candidates marker (null candidate)
                safeResolve("null_candidate_marker");
              }
            };

            pc.onicegatheringstatechange = () => {
              if (pc.iceGatheringState === "complete") {
                safeResolve("gathering_state_complete");
              }
            };

            pc.createDataChannel("turn-probe-channel");
            const offer = await pc.createOffer();
            await pc.setLocalDescription(offer);

            // Safety timeout inside browser runtime (6000ms)
            setTimeout(() => {
              safeResolve("internal_timeout");
            }, 6000);
          } catch (e) {
            reject({ message: e.message || String(e), stack: e.stack });
          }
        });
      })()
    `;

    const evalResult = await withTimeout(
      cdpSend('Runtime.evaluate', {
        expression: probeCode,
        awaitPromise: true,
        returnByValue: true
      }),
      9000,
      'WebRTC ICE Gathering'
    );

    if (evalResult.exceptionDetails) {
      const exMsg = evalResult.exceptionDetails.exception?.description || evalResult.exceptionDetails.text;
      throw new Error(`In-browser evaluation failed: ${exMsg}`);
    }

    const { candidates, gatheringState, completionReason } = evalResult.result.value || {
      candidates: [],
      gatheringState: 'unknown',
      completionReason: 'none'
    };

    console.log(`\n  ✓ ICE Gathering resolved: state='${gatheringState}', reason='${completionReason}'`);
    console.log(`  ✓ Total candidates collected under relay-only policy: ${candidates.length}`);

    // -------------------------------------------------------------------------
    // STEP 10: Verify gathered candidates
    // -------------------------------------------------------------------------
    console.log('\n================================================================');
    console.log('              CANDIDATE ALLOCATION VERIFICATION                 ');
    console.log('================================================================');

    if (candidates.length === 0) {
      throw new Error('ZERO relay candidates gathered! Coturn server was unreachable or rejected credentials.');
    }

    let verifiedRelayCount = 0;
    for (let i = 0; i < candidates.length; i++) {
      const cand = candidates[i];
      console.log(`[Candidate #${i + 1}] ${cand}`);

      const isRelay = cand.includes('typ relay');
      const hasPublicIp = cand.includes(EXPECTED_PUBLIC_IP);

      if (isRelay) {
        if (hasPublicIp) {
          console.log(`   -> VERIFIED: Valid relay candidate bound to public IP ${EXPECTED_PUBLIC_IP}`);
          verifiedRelayCount++;
        } else {
          console.warn(`   -> WARNING: Relay candidate has unexpected address (not ${EXPECTED_PUBLIC_IP})`);
        }
      } else {
        console.warn('   -> WARNING: Non-relay candidate received under relay-only policy!');
      }
    }

    console.log('\n================================================================');
    if (verifiedRelayCount > 0) {
      console.log(`✓ SUCCESS: Gathered ${verifiedRelayCount} verified TURN relay candidate(s) on ${EXPECTED_PUBLIC_IP}!`);
      console.log('  NOTE: This confirms Coturn relay allocation & public IP advertisement.');
      console.log('  End-to-end data relay requires bidirectional transfer between two independent endpoints.');
      console.log('================================================================\n');
      exitCode = 0;
    } else {
      throw new Error(`Failed to find any verified relay candidates with public IP ${EXPECTED_PUBLIC_IP}`);
    }

  } catch (error) {
    console.error('\n❌ DIAGNOSTIC PROBE FAILURE:', error.message);
    exitCode = 1;
  } finally {
    console.log('[CLEANUP] Closing connections and terminating processes...');
    if (ws) {
      try { ws.close(); } catch {}
    }
    if (cdpWs) {
      try { cdpWs.close(); } catch {}
    }
    if (braveProc) {
      try {
        braveProc.kill('SIGKILL');
      } catch {}
    }
    if (fs.existsSync(USER_DATA_DIR)) {
      try {
        fs.rmSync(USER_DATA_DIR, { recursive: true, force: true });
      } catch {}
    }
    console.log('[CLEANUP] Done.');
    process.exit(exitCode);
  }
}

run();
