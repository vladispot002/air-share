# LAN-Drop WebRTC & libdatachannel Integration Guide

## 1. Executive Summary & Technology Selection

As part of the evolution towards global peer-to-peer data transfers, LAN-Drop adopts **WebRTC DataChannels** as the primary NAT-traversal and firewall-punching transport mechanism.

To integrate WebRTC into our modern C++20 core without the multi-gigabyte footprint and massive dependency graph of the Google WebRTC native codebase (`webrtc.org`), we selected **`libdatachannel`** (`paullouisageneau/libdatachannel`).

### Key Specifications & Evaluation

| Criteria | Assessment | Rationale / Evidence |
|---|---|---|
| **Version** | `v0.22.2` | Production-grade C++17/C++20 standalone implementation of WebRTC Data Channels and media. |
| **License** | MPL 2.0 (Mozilla Public License 2.0) | File-level copyleft. Commercially permissive. Safe to link statically or dynamically with LAN-Drop without licensing infection. |
| **Dependencies** | OpenSSL, libsrtp, usrsctp | Leverages LAN-Drop's existing OpenSSL 3.x dependency. SCTP and SRTP can be vendored cleanly or fetched automatically. |
| **Platforms** | Linux, Windows, macOS, Android, iOS | Fully supported on Windows MSVC 2019/2022 and Linux GCC 11+/Clang 13+. |
| **Footprint** | Minimal (~2 MB binary size) | Compiles in under 30 seconds compared to 45+ minutes for Google WebRTC. |

---

## 2. Transport Architecture & `ITransportChannel` Mapping

LAN-Drop uses the `ITransportChannel` pure virtual interface to decouple application logic, protocol framing (v2), and transfer tasks from the network layer.

```
       +---------------------------------------------+
       |             TransferEngine                  |
       +---------------------------------------------+
                              |
       +---------------------------------------------+
       |          ITransportChannel (Interface)      |
       +---------------------------------------------+
               /                              \
              /                                \
+-----------------------+            +-----------------------+
|   TcpSocketChannel    |            |   WebRtcDataChannel   |
| (Direct / TLS-TCP)    |            | (libdatachannel / SCTP|
+-----------------------+            +-----------------------+
```

### 2.1 Interface Mapping

| `ITransportChannel` Method | WebRTC / `libdatachannel` Equivalent | Semantic Behavior |
|---|---|---|
| `getTransportType()` | `TransportType::WebRtcDataChannel` | Identifies channel as WebRTC. |
| `getRemoteAddress()` | `peerId` / remote node public key | Identifies global peer identifier. |
| `getRemotePort()` | `0` (N/A) | Port is dynamic and handled by ICE candidate pairs. |
| `isConnected()` | `rtc::DataChannel::isOpen()` | True when SCTP data channel state is `Open`. |
| `isEncrypted()` | `true` | Always encrypted via DTLS 1.2/1.3. |
| `sendExact(data, len)` | `rtc::DataChannel::send(...)` | Sends binary message chunk over SCTP. |
| `receiveExact(...)` | `rtc::DataChannel::onMessage(...)` | Receives incoming binary chunk with flow control. |
| `getBufferedAmount()` | `rtc::DataChannel::bufferedAmount()` | Returns queued bytes in SCTP buffer. |
| `close()` | `rtc::DataChannel::close()` | Closes data channel and terminates ICE peer connection. |

---

## 3. Backpressure & Flow Control Strategy

WebRTC DataChannels run over SCTP over DTLS over UDP. If the sender pushes data faster than the link bandwidth, chunks buffer in the SCTP queue. If the buffer overflows, the channel drops packets or aborts the connection.

To prevent this:
1. `WebRtcDataChannel` monitors `getBufferedAmount()`.
2. When `bufferedAmount > maxBufferedAmount` (configured to 16 MB by default), `sendExact()` either pauses via condition variable or returns backpressure signal to `TransferEngine`.
3. When `bufferedAmount` drops below threshold (via `onBufferedAmountLow`), streaming resumes at full line rate.

---

## 4. NAT Traversal & Signaling Topology

WebRTC requires an out-of-band signaling mechanism to exchange SDP offers, SDP answers, and ICE candidates.

```
+------------+                                            +------------+
|  Peer A    | <==== WebSocket / JSON Signaling =====>    |  Peer B    |
| (Initiator)|               (Go + Pion)                  | (Receiver) |
+------------+                                            +------------+
      \                                                         /
       \====== STUN / TURN (NAT Traversal via coturn) =========/
                                  ||
                                  \/
               +--------------------------------------+
               | Direct P2P Encrypted DataChannel     |
               | (DTLS-SRTP / SCTP Wire Protocol v2)  |
               +--------------------------------------+
```

1. **Signaling Server**: Go + Pion signaling backend maintains ephemeral WebSocket sessions for pairing.
2. **STUN**: Discovers public IP and NAT mapping (using Google STUN / Twilio STUN).
3. **TURN**: Coturn server deployed for symmetric NAT-to-NAT fallback relaying.
4. **Local LAN Transfer-First**: If both peers reside on the same subnet, LAN-Drop's direct TCP socket transport is prioritized, bypassing STUN/TURN entirely without requiring internet access.

---

## 5. Build Configuration & Modular Enabling

To ensure zero disruption to current developers, CI workflows, and lightweight desktop environments:
- `LANDROP_ENABLE_WEBRTC` is configured as an explicit CMake option, defaulting to `OFF`.
- When `OFF`, `WebRtcDataChannel` provides an architectural reference and mockable interface allowing unit tests to verify transport contracts without network access.
- When `ON`, CMake automatically pulls `libdatachannel v0.22.2` via `FetchContent` or links against a pre-installed system package.
