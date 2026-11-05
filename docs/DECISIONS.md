# Architectural Decision Records (ADR)

## ADR-001: C++20 as Primary Standard
- **Status**: Accepted & Verified.
- **Context**: High performance file transfer, system socket interfaces, RAII resource management, `std::span`, and `std::filesystem`.
- **Decision**: Base compiler standard set to C++20 (`-std=c++20`).

## ADR-002: Model-View-Presenter (MVP) Pattern
- **Status**: Accepted & Verified.
- **Context**: Complete headless unit testability of discovery, transfer, storage, and settings presenters independently of GTK3 and X11/Wayland display servers.
- **Decision**: Interfaces `IDiscoveryView`, `ITransferView`, `IStorageView`, `ISettingsView` are implemented by `GtkDesktopApp` and mocked via `MockView` in unit tests.

## ADR-003: Native GTK3 UI with Apple macOS HIG Styling
- **Status**: Accepted & Verified.
- **Context**: Lightweight Linux execution without Chromium/Electron, adhering to the design prototype (`Design LAN-Drop macOS UI`).
- **Decision**: Native GTK3 backend with CSS styling, custom titlebar/traffic lights, and responsive mobile layout mode (390x812).

## ADR-004: Block-level SHA-256 File Integrity Verification
- **Status**: Partially Accepted / Under Refinement.
- **Context**: Detecting bitrot and data corruption during network transmission.
- **Current Reality**: End-to-end full file SHA-256 hashing is implemented and verified in the CLI transfer pipeline (`runSender` / `runReceiver`). Block-level in-flight verification and `.part` file resume require hardening before claiming full resumption.

## ADR-005: Mobile Bridging & Interoperability
- **Status**: Clarified Scope.
- **Context**: Mobile platform integration (Flutter, iOS, Android).
- **Decision**: The in-tree AirDrop and NFC modules provide format serialization and simulated bridge endpoints for local testing. Native Apple AirDrop (requiring kernel AWDL and BLE) is out of scope for user-space POSIX sockets and remains a future bridge integration.

## Proposed Decisions Status
- **ADR-006 (Asynchronous Network Reactor)**: Deferred / Premature. Focus first on non-blocking timeouts and robust socket error handling.
- **ADR-007 (LAN-Drop Wire Protocol v1)**: Accepted in principle for basic streaming framing (`.part` streaming and length validation); advanced multi-peer state machines deferred.
- **ADR-008 (Kernel AWDL / AirDrop Plugin)**: Deferred. In-tree implementation remains scoped to local LAN and mock bridge testing.

## ADR-009: Stage 2 Global Connectivity & WebRTC Signaling Architecture
- **Status**: Accepted with Architectural Refinements.
- **Context**: Enabling global peer-to-peer file transfers across disparate NAT/firewall boundaries while maintaining the Transfer-First principle for local networks.
- **Decision**:
  1. Go 1.22+ lightweight pure WebSocket router for signaling service, managing ephemeral in-memory rooms with zero telemetry and IP rate limiting (5 attempts / 10m).
  2. 8-character Crockford Base32 room codes (`DROP-XXXX-XXXX`, 40 bits of entropy) with out-of-band SAS verification (anti-MITM).
  3. libdatachannel (C++20) for WebRTC DataChannel transport using native `rtc::WebSocket` for signaling.
  4. Strict transport hierarchy: Direct LAN (TCP/TLS) -> Direct WebRTC P2P (ICE Host/Srflx) -> TURN Relay.
  5. Core transfer logic unified over `ITransportChannel` abstraction. Native DTLS 1.3 encryption is leveraged for WebRTC P2P; application-layer double encryption is scoped for untrusted cloud byte relays.
  6. Backpressure handling in `WebRtcDataChannel::sendExact()` utilizes condition-variable wait on `onBufferedAmountLow` rather than hard error aborts. Remote ICE candidates arriving prior to remote SDP descriptions are queued.

## ADR-010: WebRTC Transfer Engine Integration & TurnConfig Client
- **Status**: Accepted & Verified.
- **Context**: Unifying the `TransferEngine` streaming pipeline across TCP, TLS, and WebRTC DataChannels, while integrating ephemeral Coturn TURN credential fetching from the Go signaling daemon.
- **Decision**:
  1. **Polymorphic Channel Integration**: `TransferEngine::sendFileOverChannel` and `TransferEngine::receiveFileOverChannel` accept `std::shared_ptr<ITransportChannel>`. All security protections (safe file name enforcement, path traversal checks, symlink refusal, disk space validation, duplicate auto-rename resolution, SHA-256 pre-calculation, in-flight backpressure, atomic `.part` staging, and post-transfer checksum verification) are unified in a single hardened codebase.
  2. **Coturn Ephemeral Credentials (`TurnConfigClient`)**: Implements `GET /api/v1/turn-config?token=...&client_id=...` with Bearer token authentication, parsing W3C `RTCIceServer` dictionaries into `landrop::WebRtcConfig`.
  3. **High-Level Session Coordination (`WebRtcSessionCoordinator`)**: Orchestrates the state lifecycle (`ConnectingSignaling`, `WaitingForPeer`, `NegotiatingSdp`, `Connected`), SDP exchange, ICE candidate queueing (`CandidateQueue`), and `WebRtcDataChannel` creation.
  4. **Headless Verification Architecture**: In-memory cross-wiring hook (`enableLoopbackMode`) enables end-to-end P2P unit testing (`TC-E2E-01`, `TC-E2E-02`, `TC-E2E-03`, `TC-E2E-04`) without external network sockets or kernel SCTP dependencies.
