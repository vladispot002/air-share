# Stage 2: Global Connectivity Foundation — Architecture & Technical Design

**Document ID:** `STAGE_2_GLOBAL_CONNECTIVITY_DESIGN.md`
**Author:** AI Orchestration Team
**Reviewers:** Chief Software Architect, Lead QA Architect (Tester), Security Reviewer
**Review Status:** **APPROVED BY SUBAGENT REVIEW BOARD (Subject to User Gate Approval)**
**Target Milestone:** Stage 2 — Global Connectivity Foundation

---

## 1. Executive Summary & Objective

The objective of **Stage 2** is to build the minimal, robust vertical slice enabling two independent LAN-Drop nodes located on separate networks to discover each other, negotiate an encrypted direct peer-to-peer connection via NAT traversal, and transfer a complete file with end-to-end cryptographic integrity.

```
+-----------------------------------------------------------------------------------------+
|                                    MINIMAL VERTICAL SLICE                               |
|                                                                                         |
|  [Node A (Sender)] <== WebSocket ==> [Go Signaling Service] <== WebSocket ==> [Node B]  |
|          \                                                                    /         |
|           \======= STUN / NAT Hole Punching (or TURN Coturn Relay) ==========           |
|                                         ||                                              |
|                                         \/                                              |
|                      [Direct WebRTC P2P DataChannel]                                    |
|                      - Native DTLS 1.3 / SCTP Transport                                 |
|                      - Wire Protocol v2 Framing                                         |
|                      - Out-of-Band SAS Verification (Anti-MITM)                         |
|                      - Real File Transfer + SHA-256 Verification                         |
+-----------------------------------------------------------------------------------------+
```

### Core Non-Negotiable Tenets:
1. **Transfer-First Principle**: Local LAN transfers must remain completely autonomous and function without an internet connection, account, or external server.
2. **Anonymous Guest Mode**: Global pairing operates on ephemeral, zero-knowledge pairing codes (e.g. `DROP-K8M4-P9X2`) without registration, telemetry, or persistent state.
3. **Zero-Knowledge Signaling**: The signaling service only relays opaque session descriptions (SDP) and ICE candidates; it never possesses encryption keys, file payloads, or plaintext metadata.
4. **Automatic Fallback Hierarchy**: Direct LAN (TCP/TLS) -> Direct WebRTC P2P (STUN) -> Relayed WebRTC (TURN).

---

## 2. Signaling Architecture (Go + WebSocket Router)

### 2.1 Technology Selection Rationale
- **Language**: Go 1.22+.
- **Signaling Daemon**: Lightweight, pure Go WebSocket router (utilizing `nhooyr.io/websocket` or `gorilla/websocket`).
  - *Architectural Note*: The signaling daemon acts strictly as an out-of-band message coordinator and does NOT terminate WebRTC media or SCTP data channels. Stripping WebRTC engines from the server daemon keeps the server binary < 15 MB and memory footprint < 20 MB.
  - `pion/webrtc` is reserved exclusively as a test fixture for headless automated CI mock-peer tests.
- **C++ Client WebSocket**: The C++ client leverages `rtc::WebSocket` already provided natively by `libdatachannel`, avoiding extra third-party networking dependencies.
- **Directory Layout**:
  ```
  backend/
  └── signaling/
      ├── cmd/
      │   └── server/
      │       └── main.go
      ├── internal/
      │   ├── room/
      │   │   ├── manager.go
      │   │   └── room.go
      │   ├── protocol/
      │   │   └── messages.go
      │   ├── ratelimit/
      │   │   └── limiter.go
      │   └── turn/
      │       └── credentials.go
      ├── tests/
      │   └── mock_peer_test.go   <-- (Uses pion/webrtc for mock testing)
      ├── go.mod
      └── go.sum
  ```

### 2.2 Component Diagram
```
                       +---------------------------------------+
                       |           Signaling Service           |
                       |              (Go 1.22+)               |
                       +---------------------------------------+
                       | - HTTP /healthz                       |
                       | - Authenticated /api/v1/turn-config   |
                       | - WebSocket Handler: /ws/signaling    |
                       | - Ephemeral Room Manager (TTL 10m)    |
                       | - IP Rate Limiter (5 attempts/10m)    |
                       +---------------------------------------+
                                    ^               ^
                                   /                 \
                      WebSocket   /                   \   WebSocket
                     (JSON-RPC)  /                     \ (JSON-RPC)
                                v                       v
                    +---------------+               +---------------+
                    | LAN-Drop Peer |               | LAN-Drop Peer |
                    |   (Sender)    |               |  (Receiver)   |
                    +---------------+               +---------------+
```

---

## 3. Signaling Message Protocol (JSON over WebSocket)

All signaling communications occur over a secure WebSocket connection (`wss://` or `ws://127.0.0.1` for local testing) in JSON format.

### 3.1 Common Envelope
```json
{
  "version": "2.0",
  "action": "<ACTION_NAME>",
  "room_code": "DROP-K8M4-P9X2",
  "client_id": "c-9b1deb4d-3b7d-4bad-9bdd-2b0d7b3dcb6d",
  "timestamp": 1730822400,
  "payload": { ... }
}
```

### 3.2 Action Catalog & Glare Resolution

| Action | Sender | Description |
|---|---|---|
| `CREATE_ROOM` | Initiator -> Server | Requests creation of a new ephemeral transfer room. Server returns 8-character Crockford Base32 code. |
| `ROOM_CREATED` | Server -> Initiator | Acknowledges room creation with `room_code`, join token, and TTL. |
| `JOIN_ROOM` | Receiver -> Server | Requests to join an existing room via `room_code`. |
| `PEER_JOINED` | Server -> Both | Broadcast when both peers are present; signals initiator to generate SDP Offer. |
| `SDP_OFFER` | Initiator -> Server -> Peer | Transmits WebRTC SDP offer string. |
| `SDP_ANSWER` | Receiver -> Server -> Peer | Transmits WebRTC SDP answer string. |
| `ICE_CANDIDATE` | Either -> Server -> Peer | Transmits a trickle ICE candidate. |
| `PEER_LEFT` | Server -> Peer | Broadcast immediately when a peer disconnects, preventing hangs. |
| `LEAVE_ROOM` | Either -> Server | Explicitly closes the room and disconnects. |
| `ERROR` | Server -> Peer | Notifies of invalid room code, timeout, room full, or rate-limit (`ERR_RATE_LIMITED`). |

> [!IMPORTANT]
> **Glare Resolution & Role Assignment**:
> To guarantee deterministic WebRTC negotiation and eliminate glare (both peers offering simultaneously), the **Room Initiator (creator) is unconditionally the SDP Offer creator**, and the **Room Joiner is unconditionally the SDP Answer creator**, regardless of which peer is sending files.

### 3.3 Message Payloads

#### `SDP_OFFER` / `SDP_ANSWER` Payload
```json
{
  "type": "offer",
  "sdp": "v=0\r\no=- 4242 2 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\na=group:BUNDLE 0\r\n...",
  "e2ee_public_key": "x25519-base64-ephemeral-pubkey"
}
```

#### `ICE_CANDIDATE` Payload
```json
{
  "candidate": "candidate:842163049 1 udp 1677729535 192.168.1.100 54123 typ host ...",
  "sdp_mid": "0",
  "sdp_mline_index": 0
}
```

---

## 4. Guest Session Lifecycle & Anonymous Transfer

```
Initiator (Room Creator)          Signaling Server                 Receiver (Joiner)
       |                                 |                                 |
       |--- 1. CREATE_ROOM ------------->|                                 |
       |<-- 2. ROOM_CREATED -------------|                                 |
       |    [DROP-K8M4-P9X2]             |                                 |
       |    (Displays code/QR)           |                                 |
       |                                 |<-- 3. JOIN_ROOM [DROP-K8M4-P9X2]|
       |<-- 4. PEER_JOINED --------------|--- 4. PEER_JOINED ------------->|
       |                                 |                                 |
       |--- 5. SDP_OFFER --------------->|--- 5. SDP_OFFER --------------->|
       |<-- 6. SDP_ANSWER ---------------|<-- 6. SDP_ANSWER --------------|
       |                                 |                                 |
       |<== 7. TRICKLE ICE CANDIDATES ==><== 7. TRICKLE ICE CANDIDATES ===>|
       |                                 |                                 |
       +=================== DIRECT P2P ESTABLISHED ========================+
       |                                 |                                 |
       | [Room enters ESTABLISHED state; stays active for ICE restart]     |
       |                                 |                                 |
       +------------------- DATA TRANSFER IN PROGRESS ---------------------+
       |                                 |                                 |
       |--- 8. LEAVE_ROOM -------------->|                                 |
       |    (Room state purged)          |<-- 8. LEAVE_ROOM ---------------|
```

### Key Lifecycle & Security Rules:
- **Room Code Entropy**: Room codes use **8-character Crockford Base32** (e.g. `DROP-K8M4-P9X2`), providing $32^8 \approx 1.1 \times 10^{12}$ combinations (40 bits of entropy) with unambiguous characters (excluding I, L, O, U).
- **Brute-Force Rate Limiting**: The Go signaling service enforces IP-based rate limiting: a maximum of **5 failed `JOIN_ROOM` attempts per 10 minutes per IP**. Subsequent attempts receive `ERR_RATE_LIMITED` (HTTP 429).
- **Maximum 2 Peers per Room**: Strictly point-to-point. Attempts by a third client to join return `ERR_ROOM_FULL`.
- **Standby Room State**: Upon DataChannel connection, the room enters `ESTABLISHED` state and remains available until transfer completes, allowing ICE restart if connectivity degrades.
- **Immediate Disconnect Notification**: If either peer disconnects prematurely, the server pushes `PEER_LEFT` to the remaining peer and purges the room after a 60-second grace period.
- **TTL Expiration**: Unjoined rooms expire in **10 minutes**.
- **Zero Disk Persistence**: Rooms reside strictly in RAM. Zero database writes, zero IP logging, zero payload retention.

---

## 5. SDP / ICE Exchange & STUN/TURN Topology

### 5.1 STUN (Session Traversal Utilities for NAT)
Used for discovering the public IP and UDP port mapping of peers behind full-cone, restricted-cone, or port-restricted NATs.
- Default STUN pool:
  - `stun:stun.l.google.com:19302`
  - `stun:global.stun.twilio.com:3478`

### 5.2 TURN (Traversal Using Relays around NAT)
Used as a deterministic fallback when both peers are behind symmetric NATs (common in cellular networks, enterprise firewalls, and CGNAT).
- Coturn deployment configuration:
  - Standard port: `3478` (UDP/TCP)
  - Secure TLS port: `5349` (TURNS over TLS)
  - Ephemeral time-limited credentials generated via HMAC-SHA1 shared secret:
    ```go
    username := fmt.Sprintf("%d:%s", expiryTimestamp, clientID)
    password := base64(hmac_sha1(turnSecret, username))
    ```
- **Credential Protection**: Access to `/api/v1/turn-config` is strictly gated to clients presenting a valid `room_token` issued upon room creation or join, preventing public credential scraping.

---

## 6. Integration with C++ `ITransportChannel`

`libdatachannel` provides native C++20 asynchronous WebRTC support (`rtc::PeerConnection`, `rtc::DataChannel`, and `rtc::WebSocket`).

```
+-------------------------------------------------------------------------+
|                              TransferEngine                             |
|  - sendFileOverChannel(path, channel, progressCallback)                 |
|  - receiveFileOverChannel(destDir, channel, progressCallback)           |
+-------------------------------------------------------------------------+
                                     |
                                     v
+-------------------------------------------------------------------------+
|                            ITransportChannel                            |
| + sendExact(data, len)                                                  |
| + receiveExact(buf, len, timeout, cancel)                               |
| + getBufferedAmount() -> Flow Control / Backpressure                    |
+-------------------------------------------------------------------------+
                                     ^
                                     |
+-------------------------------------------------------------------------+
|                            WebRtcDataChannel                            |
| ----------------------------------------------------------------------- |
| - Holds std::shared_ptr<rtc::PeerConnection>                            |
| - Holds std::shared_ptr<rtc::DataChannel>                               |
| - onMessage callback pushes bytes to thread-safe receive buffer         |
| - onBufferedAmountLow callback signals condition variable               |
| - sendExact() blocks on condition variable if buffer > 16 MB            |
| - Remote ICE candidates arriving before remote SDP are buffered safely  |
+-------------------------------------------------------------------------+
```

### Backpressure Synchronization & Candidate Queueing:
1. **Non-Failing Backpressure**: In `WebRtcDataChannel::sendExact()`, when `bufferedAmount() > 16 MB`, the sender thread enters `std::condition_variable::wait_for` until `onBufferedAmountLow` fires (low-water mark: 4 MB) or cancellation occurs. Transfers never fail prematurely from buffer saturation.
2. **Trickle ICE Candidate Buffering**: Remote ICE candidates received over WebSocket prior to `setRemoteDescription` completion are placed in a thread-safe FIFO queue and applied sequentially once the description transitions to `HaveRemoteOffer` / `Stable`.
3. **Channel-Agnostic Core Engine**: `TransferEngine` operates against `std::shared_ptr<ITransportChannel>`, ensuring identical Protocol v2 framing, chunking, CRC-32, and SHA-256 validation over both `TcpSocketChannel` and `WebRtcDataChannel`.

---

## 7. Transport Routing Selection Hierarchy

When a user initiates a transfer to a peer, LAN-Drop selects the fastest and most secure route according to the following strict decision hierarchy:

```
                      +-----------------------------+
                      |   Initiate Transfer to Peer |
                      +-----------------------------+
                                     |
                                     v
                    [Is Peer discovered on same LAN?]
                                   /   \
                            YES   /     \  NO / Unreachable
                                 v       v
         +--------------------------+   +-------------------------------+
         | Tier 1: Direct LAN Socket|   | Connect to Signaling Service  |
         | (Plaintext TCP / TLS 1.3)|   +-------------------------------+
         | - Sub-millisecond latency|                   |
         | - Line-rate 1-10 Gbps    |                   v
         | - Zero internet required |       [Perform ICE Negotiation]
         +--------------------------+                  /  \
                                           DIRECT P2P /    \ SYMMETRIC NAT
                                                     v      v
                           +---------------------------+  +-------------------+
                           | Tier 2: Direct WebRTC P2P |  | Tier 3: TURN Relay|
                           | (DTLS 1.3 / Host / Srflx) |  | (Encrypted Relay) |
                           +---------------------------+  +-------------------+
```

- **Connection Status Visibility**: The UI explicitly displays the active tier: `[Direct LAN]`, `[Direct P2P]`, or `[Relayed TURN]`.
- **Relay Soft Warning**: When initiating transfers > 1 GB over a TURN relay connection, the UI presents a soft bandwidth notification to ensure user awareness.

---

## 8. Security Model & End-to-End Encryption (E2EE)

1. **Transport Layer Security**:
   - WebRTC DataChannels strictly mandate **DTLS 1.3 / SCTP** encryption for all media and data channels. Unencrypted WebRTC connections cannot exist.
   - Intermediate signaling servers and Coturn relays cannot inspect, modify, or decrypt SCTP payloads.
2. **Anti-MITM Verification (SAS & QR Fingerprint)**:
   - During room negotiation, peers exchange ephemeral `X25519` public keys over signaling.
   - To eliminate active Man-in-the-Middle (MITM) risks from an untrusted or compromised signaling server:
     - An out-of-band **Short Authentication String (SAS)** (4-word mnemonic or 6-digit verification code) is derived from `HKDF-Extract(pubKeyA || pubKeyB)`.
     - The SAS and a QR fingerprint are displayed in the UI for visual confirmation between users.
3. **Application-Layer Key Derivation**:
   - If double encryption is active (e.g. for untrusted byte relays), directional session keys are derived:
     ```
     K_send = HKDF-Expand(MasterSecret, "lan-drop-send-v2", 32)
     K_recv = HKDF-Expand(MasterSecret, "lan-drop-recv-v2", 32)
     ```
   - 96-bit nonces comprise a 32-bit static salt and a strictly monotonic 64-bit sequence counter, eliminating IV reuse in bidirectional streams.
4. **Cryptographic Integrity & Resumption Semantics**:
   - Full SHA-256 digest computed before sending and verified upon receipt.
   - CRC-32 detects in-transit bit flips for fast frame recovery.
   - Interrupted transfers preserve `.part` files on disk with chunk offset records; resuming transfers verify existing byte offsets before streaming remaining chunks.

---

## 9. Failure Scenarios & Reconnect Protocol

| Failure Scenario | Recovery Mechanism |
|---|---|
| **Signaling Server Disconnect during Transfer** | **Zero Impact**: WebRTC DataChannels operate directly peer-to-peer once established. Disconnection of signaling does not disrupt active transfers. |
| **ICE Connection State: `Disconnected`** | Channel enters a 5-second grace period. If connectivity is restored, transfer resumes at current chunk offset. |
| **ICE Connection State: `Failed`** | Initiator triggers an **ICE Restart** via the signaling channel, generating new ICE credentials and gathering TURN relay candidates. |
| **Peer Process Crash / Network Severed** | `TransferEngine` detects channel closure, marks task as `Failed`, and preserves partially received file chunks (`.part`) for resumption. |

---

## 10. Verification & Test Matrix

| Test ID | Category | Target / Scope | Acceptance Criteria |
|---|---|---|---|
| `TC-SIG-01` | **Signaling Room Lifecycle** | Go Unit Tests (`backend/signaling`) | Room creation, 8-char Crockford Base32 formatting, 10m TTL cleanup, duplicate code rejection. |
| `TC-SIG-02` | **Signaling Rate Limiting** | Go Unit Tests (`backend/signaling`) | Rejects >5 failed `JOIN_ROOM` attempts within 10m with `ERR_RATE_LIMITED` (HTTP 429). |
| `TC-SIG-03` | **Glare & Disconnect** | Go Unit Tests (`backend/signaling`) | Room creator unconditionally assigned Offer role; server pushes `PEER_LEFT` on unexpected peer drop. |
| `TC-RTC-01` | **Candidate Buffering** | C++ Unit Tests (`tests/unit_tests.cpp`) | Trickle ICE candidates arriving prior to remote SDP description are queued and processed without error. |
| `TC-RTC-02` | **Backpressure Flow Control** | C++ Unit Tests (`tests/unit_tests.cpp`) | `sendExact()` suspends on `bufferedAmount() > 16 MB` and resumes on `onBufferedAmountLow` without throwing or aborting. |
| `TC-E2E-01` | **WebRTC Loopback P2P** | Automated C++ Integration Test | Two local nodes negotiate SDP/ICE via signaling service and stream 10 MB in-memory buffer. |
| `TC-E2E-02` | **Real File Transfer** | E2E File System Test | 100 MB binary file streamed over WebRTC; destination SHA-256 matches source hash exactly. |
| `TC-FAULT-01` | **Forced TURN Relay** | NAT Simulation Test | Direct host/srflx candidates filtered; transfer succeeds over TURN relay with bit-exact integrity. |
| `TC-FAULT-02` | **ICE Reconnect Test** | Fault Injection Test | Simulated network drop triggers ICE restart and resumes without data corruption. |
| `TC-FAULT-03` | **Chunk Resumption** | Resumption Test | Aborted transfer retains `.part` file; subsequent run resumes and validates completed file SHA-256. |

---

## 11. Target SLOs & Implementation Milestones

### 11.1 Target Quantitative Service Level Objectives (SLOs)
- **Local LAN Transfer Throughput**: $\ge 40\text{ MB/s}$ on Gigabit Ethernet / 5 GHz Wi-Fi.
- **Relayed (TURN) Transfer Throughput**: $\ge 10\text{ MB/s}$ on 100 Mbps broadband.
- **P2P Connection Handshake Latency**: $\le 1,500\text{ ms}$ (direct P2P) / $\le 3,000\text{ ms}$ (TURN fallback).
- **Process Memory Footprint**: Peak RSS $\le 64\text{ MB}$ during active 100 MB file transfer.

### 11.2 Implementation Milestones
```
+-------------------------------------------------------------------------------+
| STAGE 2 MILESTONE TIMELINE                                                    |
|                                                                               |
| [2A: Go Signaling Service] -> [2B: C++ Signaling Client]                      |
|                                         |                                     |
|                                         v                                     |
|                               [2C: WebRTC Data Pipeline]                      |
|                                         |                                     |
|                                         v                                     |
|                               [2D: E2E Verification & Audit Gate]             |
+-------------------------------------------------------------------------------+
```

### Milestone 2A: Go Signaling Service
- Setup `backend/signaling/` with Go 1.22+.
- Implement pure WebSocket signaling handler (`/ws/signaling`) and in-memory room manager.
- Implement 8-character Crockford Base32 codes (`DROP-XXXX-XXXX`) and 10-minute TTL cleanup.
- Implement IP rate limiting (5 failed attempts / 10m).
- Unit tests (`TC-SIG-01`, `TC-SIG-02`, `TC-SIG-03`).

### Milestone 2B: C++ Signaling Client & ICE Negotiation
- Implement `SignalingClient` in C++ connecting via `rtc::WebSocket`.
- Implement remote ICE candidate buffering queue.
- Handle `ROOM_CREATED`, `PEER_JOINED`, `SDP_OFFER`, `SDP_ANSWER`, `ICE_CANDIDATE`, and `PEER_LEFT`.
- Unit tests (`TC-RTC-01`).

### Milestone 2C: `TransferEngine` WebRTC Pipeline Integration
- Hook `WebRtcDataChannel` into `TransferEngine::sendFileOverChannel` and `receiveFileOverChannel`.
- Implement non-failing backpressure condition variable waiting on `onBufferedAmountLow` in `WebRtcDataChannel::sendExact()`.
- Unit tests (`TC-RTC-02`).

### Milestone 2D: End-to-End Verification & Subagent Audit
- Automated loopback integration test in CI (`TC-E2E-01`, `TC-E2E-02`).
- Verify SHA-256 hash match on 100 MB file.
- Subagent sign-offs (`architect`, `tester`, `reviewer`).
- Deliver Stage 2 Completion Report.
