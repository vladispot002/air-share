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

## ADR-011: CLI Global P2P (`p2p-send` and `p2p-receive`) & Clean Fallback
- **Status**: Accepted & Verified.
- **Context**: Enabling headless and scripted global P2P file transfers directly from the command line while cleanly communicating WebRTC availability in builds without `libdatachannel`.
- **Decision**:
  1. **CLI Commands**:
     - `p2p-send <file_path> [--signaling <ws_url>]`: Hosts an ephemeral room via `WebRtcSessionCoordinator`, outputs the normalized Crockford Base32 room code (`DROP-XXXX-XXXX`), waits for a joining peer, and streams the specified file over `WebRtcDataChannel` with live progress bars and SHA-256 verification.
     - `p2p-receive <room_code> [save_dir] [--signaling <ws_url>]`: Normalizes user room code input, connects to the room as Joiner, accepts incoming file transfer over `WebRtcDataChannel`, and writes to `save_dir` with atomic `.part` file assembly.
  2. **Strict Crockford Base32 Input Normalization**: `WebRtcSessionCoordinator::normalizeRoomCode` canonicalizes input strings: uppercase transformation, stripping prefixes (`DROP-`), removing whitespace and hyphens, and resolving visual confusions (`O` -> `0`, `I`/`L` -> `1`).
  3. **Conditional Compilation & Offline Fallback**: Under `#if !defined(LANDROP_HAVE_WEBRTC)`, CLI commands immediately inform the user that WebRTC is not compiled in (`"WebRTC global P2P support is not compiled into this build. Recompile with -DLANDROP_ENABLE_WEBRTC=ON."`) and exit with status 1, guaranteeing zero undefined behavior or runtime crashes.

## ADR-012: Real WebRTC & Signaling E2E Verification Harness (Suite 26)
- **Status**: Accepted & Verified.
- **Context**: Rigorously proving end-to-end file transfer capabilities across WebRTC data channels under heavy data loads and real Go signaling daemon WebSocket interactions.
- **Decision**:
  1. **100 MB Large-File Streaming Validation (`TC-E2E-100MB`)**: Validates `TransferEngine::sendFileOverChannel` and `receiveFileOverChannel` with a 100 MB randomized payload. Verifies in-flight dynamic backpressure handling (`onBufferedAmountLow`), Wire Protocol v2 chunking and framing, zero transmission aborts, atomic `.part` finalization, and bit-exact SHA-256 checksum matching across sender and receiver.
  2. **Live Go Signaling Server Integration (`TC-E2E-REAL-SIGNALING`)**: Spawns `backend/signaling/bin/signaling-server` on a dedicated localhost port in test harness, polls `/healthz`, connects real WebSocket clients (`SignalingClient`), verifies room creation, joiner notification, and disconnect propagation over live network loopback.
  3. **Room Normalization Test Coverage (`TC-ROOM-NORMALIZATION`)**: Exhaustively tests standard format, lowercase, optical confusion corrections (`O->0`, `I/L->1`), arbitrary whitespace/hyphen formatting, and strict rejection of illegal characters or invalid lengths.

## ADR-013: Desktop UI Global P2P Room Radar Pairing & Background Channel Listening
- **Status**: Accepted & Verified.
- **Context**: Providing a seamless, intuitive desktop user experience for global file sharing across the internet without manual IP configuration, port forwarding, or cloud account creation.
- **Decision**:
  1. **Unified Radar Discovery (`DiscoveryPresenter` & `QtDiscoveryWidget`)**: Added "🌍 Global P2P Room" action to the radar view. Hosts can create a room, view the generated room code with one-click clipboard copying, and wait for peer connection. Joiners can enter or paste a room code.
  2. **Dynamic WebRTC Peer Registration**: Upon establishing the WebRTC DataChannel session, `DiscoveryPresenter::onGlobalPeerConnected` injects the remote peer into the discovery radar with endpoint `webrtc:<room_code>`. Transfers directed at this peer in `QtDesktopApp` automatically route through `TransferEngine::sendFileOverChannel()`.
  3. **Bidirectional Background Channel Receiver**: Both host and joiner launch asynchronous receiver listener threads on the established `WebRtcDataChannel`. When the remote peer initiates a transfer, files are received, validated via SHA-256, staged atomically, and logged to the UI transfer list with desktop notifications.

## ADR-014: Web Client Architecture, Wire Protocol v2 Framing & Browser P2P Streaming Engine
- **Status**: Accepted & Verified.
- **Context**: Enabling zero-install browser-to-desktop and browser-to-browser P2P file transfers over WebRTC DataChannels with Wire Protocol v2 framing, memory-safe large file streaming (>2GB), glare-free signaling, and zero-telemetry security.
- **Decision**:
  1. **Wire Protocol v2 Binary Framing**: Implement strict 16-byte frame header (`LNDP` magic [0x4C, 0x4E, 0x44, 0x50], 2-byte BE Version [2], 2-byte BE OpCode [0x0001 Handshake, 0x0002 FileHeader, 0x0003 ChunkData, 0x0004 TransferAck, 0x0005 TransferCancel], 4-byte BE Payload Length, 4-byte BE CRC-32) using JavaScript `DataView` and a precomputed 256-entry IEEE 802.3 CRC-32 lookup table matching `WireProtocolV2.hpp` exactly.
  2. **Memory-Safe Large File Streaming**: Sender uses `File.stream()` with 64 KB / 256 KB chunks and strict backpressure flow control (`dataChannel.bufferedAmountLowThreshold = 1 MB` with `bufferedamountlow` event waiting) to safely handle files > 2GB without memory exhaustion. Receiver utilizes the File System Access API (`showSaveFilePicker()` writable stream) for direct-to-disk streaming, falling back to memory-bounded blob chunking.
  3. **Glare-Free Signaling & Ephemeral Rooms**: Connect to Go signaling daemon at `/ws/signaling`. Room Host initiates SDP Offer; Room Joiner initiates SDP Answer. Crockford Base32 room codes (`DROP-XXXX-XXXX`) normalized with optical character disambiguation (`O`->`0`, `I`/`L`->`1`). Coturn TURN credentials fetched dynamically via `GET /api/v1/turn-config` with Bearer auth.
  4. **Security & Zero Telemetry**: Streaming incremental SHA-256 validation, strict CSP, path traversal prevention, zero telemetry, and DTLS 1.3 encryption.
  5. **Static Asset Integration**: Embedded via Go signaling server serving static distribution from `web/dist` at `/`.

## ADR-015: Optical Pairing & Zero-Dependency QR Code Integration
- **Status**: Accepted & Specified.
- **Context**: Eliminating manual room code entry and network discovery friction across desktop, mobile, and web clients while maintaining the Transfer-First principle, client-side privacy, and zero external dependencies.
- **Decision**:
  1. **Zero-Dependency C++ & TypeScript Generators**: Implement ISO/IEC 18004:2015 (Versions 1-10, ECC Levels L/M/Q/H, Alphanumeric and Byte modes) natively in `src/ui/QrCodeGenerator.cpp` and `web/src/core/session/QrCode.ts`. Strictly prohibit external libraries (`libqrencode`, npm `qrcode`).
  2. **URL Schema with URI Hash Fragment**: Optical QR codes encode `https://<signaling_host>/#<ROOM_CODE>` (or `https://drop.local/#DROP-XXXX-XXXX`). In accordance with RFC 3986, the fragment `#...` is parsed client-side only and never transmitted in HTTP request headers, ensuring zero telemetry and zero server eavesdropping.
  3. **High-DPI & Contrast Guarantees**: Integer module scaling with `QPainter` eliminates interpolation blur. In dark mode, QR codes are isolated within an opaque white card container with a 4-module quiet zone to ensure camera decodability.
  4. **Full Web P2P Integration**: Web client automatically joins rooms detected in `window.location.hash`. Host Room button spawns an optical pairing modal, streaming files over `WebRtcCoordinator` and `FileStreamSender`/`FileStreamReceiver`.


