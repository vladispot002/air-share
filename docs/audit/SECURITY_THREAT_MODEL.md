# LAN-Drop: Security Threat Model & Cryptographic Audit

**Date**: 2026-11-10  
**Target**: LAN-Drop Architecture & Wire Protocol v2 (Branch `audit/production-readiness`)  
**Lead Auditor**: Lead Security Engineer  
**Classification**: Engineering Security Architecture

---

## 1. Threat Modeling Overview & Trust Boundaries

LAN-Drop is an open-source, serverless-first, peer-to-peer file transfer system. It operates across three distinct operational topologies:
1. **Local Area Network (LAN) Direct Transfer**: Peer-to-peer over TCP/TLS without internet connectivity.
2. **Global P2P (WebRTC DataChannel)**: Peer-to-peer over DTLS 1.2/1.3 via libdatachannel or browser WebRTC, negotiated via a central Go WebSocket signaling server and STUN/TURN relays.
3. **Web Client (Browser PWA)**: Ephemeral browser-to-desktop or browser-to-browser WebRTC file streaming.

```
+---------------------------------------------------------------------------------------------------+
|                                      TRUST BOUNDARY ARCHITECTURE                                   |
|                                                                                                   |
|  [ TRUST BOUNDARY 1: Local Device & File System ]                                                 |
|    - App storage directory, unprivileged execution, path sanitization, atomic staging (.part)     |
|                                                                                                   |
|  [ TRUST BOUNDARY 2: Direct Local LAN ]                                                           |
|    - UDP Multicast Beacons (239.255.255.250:52637)                                                |
|    - TCP Direct Connection (Port 8848) with Ephemeral TLS 1.3 & Fingerprint Pinning               |
|                                                                                                   |
|  [ TRUST BOUNDARY 3: Centralized Signaling Server (UNTRUSTED FOR CONTENT) ]                        |
|    - WebSocket JSON-RPC (wss://<host>/ws/signaling)                                               |
|    - REST Ephemeral TURN Credentials (GET /api/v1/turn-config)                                    |
|                                                                                                   |
|  [ TRUST BOUNDARY 4: Peer-to-Peer WebRTC DataChannel (END-TO-END DATA PLANE) ]                    |
|    - DTLS 1.2 / 1.3 Handshake + SCTP Channel Framing                                             |
+---------------------------------------------------------------------------------------------------+
```

### Critical Security Distinction: Transport Encryption vs. Peer Authentication

A fundamental finding of this audit is the distinction between transport-level confidentiality and peer identity:

- **Transport Encryption (DTLS/TLS)**: **ACTIVE**. All DataChannel traffic is encrypted using DTLS. All signaling traffic is encrypted over TLS via Caddy.
- **End-to-End Peer Authentication**: **ABSENT**. DTLS certificates are self-signed and negotiated entirely in-band via SDP fingerprints (`a=fingerprint:sha-256 ...`). The signaling server is implicitly trusted as an unauthenticated key distributor.
- **Consequence**: An untrusted or compromised signaling server can mount an undetectable **Man-in-the-Middle (MITM)** attack by rewriting SDP certificate fingerprints.

---

## 2. Assets, Threat Actors & Attack Surfaces

### 2.1 Protected Assets
1. **User Files**: Confidentiality and integrity of transferred files across public networks.
2. **Local File System**: Prevention of unauthorized file overwrites, deletions, or directory traversal outside designated save folders.
3. **Session Privacy**: Room codes and peer IP addresses (metadata confidentiality).
4. **Signaling & Relay Infrastructure**: Availability and bandwidth of the self-hosted Go signaling daemon and Coturn server.

### 2.2 Threat Actor Profiles
- **Malicious Signaling Server Operator / Compromised Host**: Possesses the ability to intercept, alter, or drop signaling messages (SDP offers/answers, ICE candidates).
- **Network Eavesdropper (Passive / Active ISP / Wi-Fi Attacker)**: Observes or injects packets on local Wi-Fi or transit networks.
- **Adversarial Peer**: Connects to legitimate rooms or sends malformed Wire Protocol v2 frames, path traversal vectors, or oversized payloads.
- **Internet Scanner / DoS Botnet**: Floods public HTTP/WebSocket endpoints with malformed requests or brute-forces room codes.

---

## 3. Vulnerability Findings Matrix

| Finding ID | Severity | CVSS v3.1 | CWE | Vulnerability Title | Status |
| :--- | :--- | :---: | :--- | :--- | :---: |
| **SEC-01** | **CRITICAL** | **9.1** | CWE-300 | Signaling Server MITM via SDP Fingerprint Rewriting | **CONFIRMED** |
| **SEC-02** | **CRITICAL** | **9.1** | CWE-798 | Coturn Shared Secret Hardcoded Fallback via Variable Mismatch | **CONFIRMED** |
| **SEC-03** | **HIGH** | **7.5** | CWE-400 | Global Denial-of-Service via Unconfigured `TrustProxy` | **CONFIRMED** |
| **SEC-04** | **HIGH** | **7.5** | CWE-770 | Unbounded Memory Growth & Mutex Starvation in Rate Limiter | **CONFIRMED** |
| **SEC-05** | **MEDIUM** | **6.5** | CWE-362 | Destruction of Pre-Existing `.part` Files & In-Flight Collision | **CONFIRMED** |
| **SEC-06** | **MEDIUM** | **5.3** | CWE-400 | Coturn UDP Relay Port Starvation (Only 41 Ports Configured) | **CONFIRMED** |
| **SEC-07** | **MEDIUM** | **5.3** | CWE-287 | Unrestricted `/api/v1/turn-config` Harvesting & Lack of Single-Use Tokens | **CONFIRMED** |
| **SEC-08** | **MEDIUM** | **4.3** | CWE-346 | Insecure CSP (`'unsafe-inline'`), Insecure Protocol, Missing CSWSH Guard | **CONFIRMED** |
| **SEC-09** | **LOW** | **3.7** | CWE-918 | Incomplete IPv6 and IPv4-Mapped IPv6 Anti-SSRF Rules in Coturn | **CONFIRMED** |
| **SEC-10** | **LOW** | **3.1** | CWE-20 | Missing Windows Reserved Device Names `CONIN$` and `CONOUT$` | **CONFIRMED** |

---

## 4. Deep-Dive Vulnerability Assessments

### SEC-01: Signaling Server MITM via SDP Fingerprint Rewriting (CVSS: 9.1)
- **Vector**: `CVSS:3.1/AV:N/AC:L/PR:N/UI:N/S:U/C:H/I:H/A:N`
- **Affected Code**:
  - `src/core/WebRtcSessionCoordinator.cpp:184-198`
  - `src/core/SignalingClient.cpp:247-271`
  - `web/src/core/transport/WebRtcCoordinator.ts:237-344`
  - `backend/signaling/internal/server/server.go:437-512`
- **Vulnerability Mechanism**:
  WebRTC DataChannel security relies on DTLS (RFC 8827 / RFC 5763). Because peer certificates are self-signed, authenticity is guaranteed strictly by matching the remote certificate against the `a=fingerprint` attribute exchanged during signaling.
  LAN-Drop performs zero out-of-band validation of this fingerprint. An attacker controlling or proxying the signaling server can intercept `SDP_OFFER`, replace the fingerprint with its own certificate fingerprint, complete DTLS with the Initiator, and create a separate DTLS session with the Joiner. The signaling server then proxies and inspects all file data in plaintext.
- **Dead Code Finding**:
  An `e2ee_public_key` field exists in `SignalingClient.cpp` and `messages.go`, but is completely unused. No Short Authentication String (SAS) or cryptographic verification is implemented.
- **Remediation**:
  1. Embed the Initiator's DTLS certificate fingerprint into the optical QR pairing code (`https://drop.domain/#DROP-XXXX-XXXX:<base64url_fingerprint>`).
  2. Implement Short Authentication String (SAS) verification: Derive a 4-word or 4-digit code using `HKDF-SHA256(fingerprint_A || fingerprint_B)` and display it on both UIs for confirmation.

---

### SEC-02: Coturn Shared Secret Hardcoded Fallback via Variable Mismatch (CVSS: 9.1)
- **Vector**: `CVSS:3.1/AV:N/AC:L/PR:N/UI:N/S:U/C:H/I:H/A:N`
- **Affected Code**:
  - `backend/signaling/cmd/server/main.go:19`
  - `docker-compose.yml:16`
- **Vulnerability Mechanism**:
  In `main.go`:
  ```go
  turnSecret := flag.String("turn-secret", getEnv("TURN_SECRET", "local-test-turn-secret"), "Coturn shared secret")
  ```
  In `docker-compose.yml`:
  ```yaml
  environment:
    - COTURN_STATIC_AUTH_SECRET=${TURN_SECRET}
  ```
  The signaling container receives `COTURN_STATIC_AUTH_SECRET`, but `main.go` only checks `TURN_SECRET`. As a result, the signaling daemon uses `"local-test-turn-secret"` while Coturn uses the generated secret from `.env`.
- **Impact**: All TURN credentials minted by the server are signed with the fallback secret and rejected with 401 Unauthorized by Coturn. WebRTC relay fallback is completely broken in production.
- **Remediation**:
  Change `docker-compose.yml` to set `TURN_SECRET=${TURN_SECRET}`. Update `main.go` to fall back to `COTURN_STATIC_AUTH_SECRET` if `TURN_SECRET` is empty.

---

### SEC-03: Global Denial-of-Service via Unconfigured `TrustProxy` (CVSS: 7.5)
- **Vector**: `CVSS:3.1/AV:N/AC:L/PR:N/UI:N/S:U/C:N/I:N/A:H`
- **Affected Code**:
  - `backend/signaling/cmd/server/main.go:39-49`
  - `backend/signaling/internal/server/server.go:581`
- **Vulnerability Mechanism**:
  `TrustProxy` defaults to `false` and cannot be enabled via flags or environment variables. Behind Caddy, `r.RemoteAddr` resolves to Caddy's internal bridge IP (e.g., `172.18.0.2`).
  When any single user fails 5 room join attempts within 10 minutes, `RecordFailure("172.18.0.2")` triggers a block on Caddy's IP.
- **Impact**: **All users globally are blacklisted** from creating or joining rooms for 10 minutes.
- **Remediation**:
  Add `TRUST_PROXY` environment variable support in `main.go` and set `TRUST_PROXY=true` in `docker-compose.yml`.

---

### SEC-04: Unbounded Memory Growth & Mutex Starvation in Rate Limiter (CVSS: 7.5)
- **Vector**: `CVSS:3.1/AV:N/AC:L/PR:N/UI:N/S:U/C:N/I:N/A:H`
- **Affected Code**:
  - `backend/signaling/internal/ratelimit/limiter.go:84-147`
- **Vulnerability Mechanism**:
  `pruneOldestLocked(now)` only evicts records that are both past `blockedTo` and have zero failures within `now - window`. If an attacker generates requests from 50,000 distinct IP addresses within a 10-minute window, `pruneOldestLocked` removes 0 records. The function then inserts the new IP into `l.records` unconditionally, exceeding `maxCapacity`.
  Additionally, every request triggers an $O(N)$ linear scan of all records while holding `l.mu.Lock()`.
- **Impact**: Process memory exhaustion and severe mutex lock starvation, dropping legitimate WebSocket connections.
- **Remediation**: Implement a bounded LRU eviction cache that strictly caps the map at 50,000 entries.

---

### SEC-05: Destruction of Pre-Existing `.part` Files & Concurrent Collision (CVSS: 6.5)
- **Vector**: `CVSS:3.1/AV:L/AC:L/PR:L/UI:N/S:U/C:N/I:H/A:H`
- **Affected Code**:
  - `src/core/TransferEngine.cpp:597-604`
- **Vulnerability Mechanism**:
  ```cpp
  std::filesystem::path partPath = canonicalBase / (cleanName.string() + ".part");
  std::filesystem::remove(partPath, ec);
  ```
  1. **Data Loss**: If a user has a legitimate file `data.zip.part` in their save directory, an incoming transfer for `data.zip` immediately deletes `data.zip.part`.
  2. **Concurrent Collision**: If two peers send `report.pdf` simultaneously, both threads write to `report.pdf.part` simultaneously, corrupting chunk sequencing.
- **Remediation**:
  Use task-unique staging paths: `canonicalBase / ("." + cleanName.string() + "." + taskId + ".part")`. Atomically rename to `cleanName` only upon full SHA-256 validation.

---

### SEC-06: Coturn UDP Relay Port Starvation (CVSS: 5.3)
- **Vector**: `CVSS:3.1/AV:N/AC:L/PR:N/UI:N/S:U/C:N/I:N/A:L`
- **Affected Code**:
  - `docker-compose.yml:50, 65`
- **Vulnerability Mechanism**:
  `--min-port=49160` and `--max-port=49200` defines a range of only 41 UDP ports. Each active TURN relay allocation claims one port. The server can only support 20–40 concurrent relayed transfers globally. An attacker can exhaust all ports by holding 41 WebRTC connections open.
- **Remediation**: Expand port range to at least 1,000–5,000 ports (e.g. `49160-50160`).

---

### SEC-07: Unrestricted `/api/v1/turn-config` Harvesting (CVSS: 5.3)
- **Vector**: `CVSS:3.1/AV:N/AC:L/PR:N/UI:N/S:U/C:L/I:N/A:N`
- **Affected Code**:
  - `backend/signaling/internal/server/server.go:151-205`
- **Vulnerability Mechanism**:
  The room token is valid for the entire 10-minute lifetime of the room and is not single-use. Furthermore, `/api/v1/turn-config` reflects arbitrary `client_id` query parameters into minted credentials and lacks rate limiting.
- **Remediation**: Invalidate tokens after 2 credential fetches (one for Initiator, one for Joiner). Enforce rate limiting on the endpoint.

---

### SEC-08: Insecure CSP & Missing CSWSH Origin Guard (CVSS: 4.3)
- **Vector**: `CVSS:3.1/AV:N/AC:L/PR:N/UI:R/S:U/C:N/I:L/A:N`
- **Affected Code**:
  - `Caddyfile:14`
  - `backend/signaling/internal/server/server.go:79-81`
- **Vulnerability Mechanism**:
  `Caddyfile` specifies `script-src 'self' 'unsafe-inline'`. Because the Vite SPA uses pre-compiled external bundles, `'unsafe-inline'` is completely unnecessary. `server.go` implements `CheckOrigin: func(r *http.Request) bool { return true }`, exposing the WebSocket to Cross-Site WebSocket Hijacking (CSWSH).
- **Remediation**: Remove `'unsafe-inline'` from CSP. Enforce origin matching in `CheckOrigin`.

---

### SEC-09: Incomplete IPv6 Anti-SSRF Rules in Coturn (CVSS: 3.7)
- **Vector**: `CVSS:3.1/AV:N/AC:H/PR:N/UI:N/S:U/C:L/I:N/A:N`
- **Affected Code**:
  - `docker-compose.yml:57-61`
- **Vulnerability Mechanism**:
  `docker-compose.yml` denies private IPv4 subnets, but omits IPv6 Loopback (`::1`), Unique Local (`fc00::/7`), Link-Local (`fe80::/10`), and IPv4-mapped IPv6 (`::ffff:0:0/96`).
- **Remediation**: Add explicit `--denied-peer-ip` rules for all private IPv6 ranges.

---

### SEC-10: Missing Windows Reserved Names `CONIN$` and `CONOUT$` (CVSS: 3.1)
- **Vector**: `CVSS:3.1/AV:L/AC:L/PR:L/UI:N/S:U/C:N/I:N/A:L`
- **Affected Code**:
  - `include/landrop/utils/Types.hpp:136`
  - `web/src/core/streaming/FileStreamReceiver.ts:35`
- **Vulnerability Mechanism**:
  `CONIN$` and `CONOUT$` are reserved Windows console devices not matched by `upperStem == "CON"`. Attempting to open them on Windows can stall thread execution.
- **Remediation**: Add `CONIN$` and `CONOUT$` to the reserved device name table.

---

## 5. Security Remediation Priority Roadmap

```
+-------------------------------------------------------------------------------+
|                            SECURITY REMEDIATION TIMELINE                      |
|                                                                               |
|  [ Sprint 1: Critical Operational & DoS Fixes ]                               |
|    - SEC-02: Fix TURN_SECRET mismatch in docker-compose.yml                   |
|    - SEC-03: Add TRUST_PROXY flag to signaling server                         |
|    - SEC-04: Implement strict LRU eviction in rate limiter                   |
|    - SEC-05: Task-unique staging paths in TransferEngine (.part)              |
|                                                                               |
|  [ Sprint 2: Cryptographic & Web Hardening ]                                  |
|    - SEC-01: Optical QR DTLS Fingerprint Exchange & SAS Verification         |
|    - SEC-06 & SEC-09: Expand Coturn relay ports & add IPv6 SSRF filters       |
|    - SEC-08: Remove 'unsafe-inline' CSP and add Origin check                  |
|    - SEC-10: Add Windows CONIN$ and CONOUT$ guards                            |
|                                                                               |
|  [ Sprint 3: Token Hardening ]                                                |
|    - SEC-07: Single-use room tokens & /turn-config rate limiting              |
+-------------------------------------------------------------------------------+
```

---

## 6. Security Engineer Sign-Off

LAN-Drop features clean core cryptography (OpenSSL 3 PBKDF2, constant-time comparisons, SHA-256 chunk hashing). However, due to **SEC-02** (100% TURN authentication failure in production) and **SEC-03** (global rate-limit blackout vulnerability), **production security approval is WITHHELD** until Sprint 1 remediations are merged and verified.
