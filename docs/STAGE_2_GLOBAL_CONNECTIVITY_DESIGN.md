# Stage 2: Global Connectivity Foundation — Architecture & Technical Design

**Document ID:** `STAGE_2_GLOBAL_CONNECTIVITY_DESIGN.md`  
**Author:** AI Orchestration Team  
**Reviewers:** Architect, Tester, Reviewer  
**Status:** DRAFT PROPOSAL FOR USER APPROVAL  
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
|                      - DTLS 1.3 / SCTP Transport                                        |
|                      - Wire Protocol v2 Framing                                         |
|                      - Real File Transfer + SHA-256 Verification                         |
+-----------------------------------------------------------------------------------------+
```

### Core Non-Negotiable Tenets:
1. **Transfer-First Principle**: Local LAN transfers must remain completely autonomous and function without an internet connection, account, or external server.
2. **Anonymous Guest Mode**: Global pairing operates on ephemeral, zero-knowledge pairing codes (e.g. `DROP-482910`) without registration, telemetry, or data retention.
3. **Zero-Knowledge Signaling**: The signaling service only relays opaque session descriptions (SDP) and ICE candidates; it never possesses encryption keys or file payloads.
4. **Automatic Fallback Hierarchy**: Direct LAN -> Direct WebRTC P2P (STUN) -> Relay WebRTC (TURN).

---

## 2. Signaling Architecture (Go + Pion)

### 2.1 Technology Selection Rationale
- **Language**: Go 1.22+.
- **WebRTC Engine**: `pion/webrtc` (pure Go, zero CGO, battle-tested in high-throughput production systems).
- **Network Footprint**: Single static binary (< 15 MB), low memory consumption (< 30 MB baseline), concurrent goroutine-per-client model capable of thousands of concurrent rooms.
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
      │   └── turn/
      │       └── credentials.go
      ├── go.mod
      └── go.sum
  ```

### 2.2 Component Diagram
```
                       +---------------------------------------+
                       |           Signaling Service           |
                       |              (Go + Pion)              |
                       +---------------------------------------+
                       | - HTTP /healthz, /api/v1/turn-config  |
                       | - WebSocket Handler: /ws/signaling    |
                       | - Ephemeral Room Manager (TTL 10m)    |
                       | - Rate Limiter & Abuse Guard          |
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
  "room_code": "DROP-839201",
  "client_id": "c-9b1deb4d-3b7d-4bad-9bdd-2b0d7b3dcb6d",
  "timestamp": 1730822400,
  "payload": { ... }
}
```

### 3.2 Action Catalog

| Action | Sender | Description |
|---|---|---|
| `CREATE_ROOM` | Initiator -> Server | Requests creation of a new ephemeral transfer room. Server returns 6-digit code. |
| `ROOM_CREATED` | Server -> Initiator | Acknowledges room creation with `room_code` and TTL. |
| `JOIN_ROOM` | Receiver -> Server | Requests to join an existing room via `room_code`. |
| `PEER_JOINED` | Server -> Both | Broadcast when both peers are present; signals initiator to generate SDP Offer. |
| `SDP_OFFER` | Initiator -> Server -> Peer | Transmits WebRTC SDP offer string. |
| `SDP_ANSWER` | Receiver -> Server -> Peer | Transmits WebRTC SDP answer string. |
| `ICE_CANDIDATE` | Either -> Server -> Peer | Transmits a trickle ICE candidate. |
| `LEAVE_ROOM` | Either -> Server | Explicitly closes the room and disconnects. |
| `ERROR` | Server -> Peer | Notifies of invalid room code, timeout, or rate-limit. |

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
Initiator (Sender)                Signaling Server                 Receiver
       |                                 |                             |
       |--- 1. CREATE_ROOM ------------->|                             |
       |<-- 2. ROOM_CREATED [DROP-7429] -|                             |
       |    (Displays code/QR)           |                             |
       |                                 |<-- 3. JOIN_ROOM [DROP-7429]-|
       |<-- 4. PEER_JOINED --------------|--- 4. PEER_JOINED --------->|
       |                                 |                             |
       |--- 5. SDP_OFFER --------------->|--- 5. SDP_OFFER ----------->|
       |<-- 6. SDP_ANSWER ---------------|<-- 6. SDP_ANSWER ----------|
       |                                 |                             |
       |<== 7. TRICKLE ICE CANDIDATES ==><== 7. TRICKLE ICE CANDIDATES>|
       |                                 |                             |
       +=================== DIRECT P2P ESTABLISHED ====================+
       |                                                               |
       |--- 8. LEAVE_ROOM -------------->|                             |
       |    (Server destroys room state) |<-- 8. LEAVE_ROOM -----------|
```

### Key Lifecycle Rules:
- **Maximum 2 Peers per Room**: Rooms are strictly peer-to-peer point-to-point. Attempts by a third client to join return `ERR_ROOM_FULL`.
- **TTL Expiration**: Unjoined rooms expire in **10 minutes**. Active rooms terminate automatically once the transfer completes or either peer disconnects.
- **Zero Disk Persistence**: Rooms exist strictly in ephemeral memory. No database writes, IP logging, or file metadata retention occurs on the signaling server.

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
    ```
    username = fmt.Sprintf("%d:%s", expiryTimestamp, clientID)
    password = base64(hmac_sha1(turnSecret, username))
    ```

---

## 6. Integration with C++ `ITransportChannel`

`libdatachannel` provides native C++20 asynchronous WebRTC support (`rtc::PeerConnection`, `rtc::DataChannel`, and `rtc::WebSocket`).

```
+-------------------------------------------------------------------------+
|                              TransferEngine                             |
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
| - onBufferedAmountLow callback notifies sender thread to resume flow    |
| - getBufferedAmount() queries rtc::DataChannel::bufferedAmount()        |
+-------------------------------------------------------------------------+
```

### Backpressure Synchronization:
1. `TransferEngine` streams file chunks via `channel->sendExact(chunk, size)`.
2. If `channel->getBufferedAmount() > 16 MB`, the sender thread enters `std::condition_variable::wait_for` until `onBufferedAmountLow` fires.
3. This guarantees optimal network utilization without kernel buffer exhaustion or SCTP message drops.

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

---

## 8. Security Model & End-to-End Encryption (E2EE)

1. **Transport Layer Security**:
   - WebRTC DataChannels strictly mandate **DTLS 1.3 / SRTP** encryption for all media and SCTP channels. No unencrypted WebRTC channels can exist.
2. **Application-Layer E2EE (Double Encryption)**:
   - During the signaling handshake, peers exchange ephemeral `X25519` public keys.
   - A shared session key is derived via ECDH + HKDF-SHA256.
   - All Wire Protocol v2 frames (`FILE_HEADER`, `CHUNK_DATA`) are encrypted using **AES-256-GCM** before passing into the DataChannel.
   - Even if the signaling server or TURN relay is compromised, an adversary sees only opaque ciphertext.
3. **Cryptographic Integrity**:
   - Full SHA-256 digest computed before sending and verified upon receipt.
   - CRC-32 detects in-transit bit flips for rapid frame retransmission.

---

## 9. Failure Scenarios & Reconnect Protocol

| Failure Scenario | Recovery Mechanism |
|---|---|
| **Signaling Server Disconnect during Transfer** | **Zero Impact**: WebRTC DataChannels operate directly peer-to-peer once established. Disconnection of signaling does not disrupt active transfers. |
| **ICE Connection State: `Disconnected`** | Channel enters a 5-second grace period. If connectivity is restored, transfer resumes at current chunk offset. |
| **ICE Connection State: `Failed`** | Initiator triggers an **ICE Restart** via the signaling channel, generating new ICE credentials and gathering TURN relay candidates. |
| **Peer Process Crash / Network Severed** | `TransferEngine` detects socket/channel closure, marks task as `Failed`, and preserves partially received file chunks with a `.part` extension for resumption. |

---

## 10. Verification & Test Matrix

| Test Category | Target / Scope | Acceptance Criteria |
|---|---|---|
| **Signaling Room Lifecycle** | Go Unit Tests (`backend/signaling`) | Room creation, duplicate rejection, TTL timeout, peer join, room destruction. |
| **Signaling Protocol Parsing** | Go & C++ Parser Tests | Validates all JSON-RPC schemas; rejects malformed payloads and oversize messages. |
| **WebRTC Loopback P2P** | Automated C++ Integration Test | Two local nodes negotiate SDP/ICE via signaling and transfer 10 MB in-memory buffer. |
| **Real File Transfer** | E2E File System Test | 100 MB binary file streamed over WebRTC; output SHA-256 matches input exactly. |
| **Forced TURN Relay** | NAT Simulation Test | Direct host/srflx candidates filtered out; transfer verified over TURN relay. |
| **ICE Reconnect Test** | Fault Injection Test | Simulated network drop triggers ICE restart and resumes without data corruption. |

---

## 11. Implementation Plan & Milestones

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
- Implement WebSocket signaling handler and in-memory room manager.
- Implement room codes (`DROP-XXXXXX`) and 10-minute TTL cleanup.
- Unit tests for room concurrency and message routing.

### Milestone 2B: C++ Signaling Client & ICE Negotiation
- Implement `SignalingClient` in C++ connecting to WebSocket signaling endpoint.
- Handle `ROOM_CREATED`, `PEER_JOINED`, `SDP_OFFER`, `SDP_ANSWER`, and `ICE_CANDIDATE`.
- Connect signaling client events to `WebRtcDataChannel`.

### Milestone 2C: `TransferEngine` WebRTC Pipeline Integration
- Hook `WebRtcDataChannel` into `TransferEngine::sendFileViaChannel` and `receiveFileViaChannel`.
- Wire up Wire Protocol v2 frames over `WebRtcDataChannel`.
- Implement dynamic backpressure monitoring (`onBufferedAmountLow`).

### Milestone 2D: End-to-End Verification & Subagent Audit
- Execute two-process file transfer over WebRTC.
- Verify SHA-256 hash match.
- Reviewer and Tester sign-off.
- Deliver Stage 2 Completion Report.
