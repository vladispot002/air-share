# LAN-Drop Technical Specification

## 1. System Overview

**LAN-Drop** is a high-performance, cross-platform file transfer and sharing ecosystem designed to operate seamlessly across both local area networks (LAN/Wi-Fi) and cross-network / internet environments. It unifies native desktop clients (C++20 / Qt 6), modern web clients (TypeScript / React 19), mobile applications (C-ABI FFI bridge), and high-throughput cloud signaling services (Go 1.23).

---

## 2. Functional Specifications

### 2.1 Network Discovery & Peer Pairing

1. **Local Subnet Multicast Discovery**:
   - Transmits and listens for UDP multicast beacons on `239.255.88.48:8848`.
   - Beacon format: `LANDROP_BEACON|<DeviceName>|<DeviceUUID>|<Port>`.
   - Dynamic peer filtering via templated `filterPeers<T>()` across heterogeneous peer types.
   - Manual direct connection via IPv4/IPv6 address and TCP port with strict input validation.

2. **Cross-Network & Internet Room Pairing**:
   - WebRTC signaling coordinated through a centralized or self-hosted signaling server over secure WebSockets (`wss://`).
   - 6-character alphanumeric room codes with case-insensitive normalization and automatic whitespace stripping.
   - Dual-mode room hosting: either Desktop or Web client can act as Room Host or Joiner.
   - QR code generation and parsing embedding deep-link connection payloads (`https://<domain>/#room=<CODE>`).
   - Ephemeral session lifecycles with automatic room expiration and cleanup.

3. **NFC Quick-Pair Simulation**:
   - Standardized NDEF text payload format: `LANDROP:NFC:v1;id=<uuid>;name=<name>;ip=<ip>;port=<port>;pin=<pin>;key=<pubkey>`.
   - Safe parsing, generation, and validation of session configuration strings.

4. **Apple AirDrop Interoperability Emulation**:
   - Service record generation for `_airdrop._tcp.local.` via mDNS/Bonjour structures.
   - Synthesizes `/Discover` and `/Ask` HTTP endpoint payloads for interoperability testing.

---

### 2.2 Transport Framing & Wire Protocol v2

1. **Fixed Frame Framing**:
   - Fixed 16-byte header: `Magic` (4 bytes ASCII `'LNDP'`), `Version` (2 bytes BE, `0x0002`), `MessageType` (2 bytes BE), `PayloadLength` (4 bytes BE, max 16 MB), `CRC32OrSeq` (4 bytes BE).
   - Strict Big-Endian (Network Byte Order) encoding for all scalar integer fields.
   - CRC-32 IEEE 802.3 transport checksum validated before payload buffers are passed to application layers.

2. **Message Types**:
   - `0x0001` (`HANDSHAKE`): Device capabilities, unique IDs, device names, and client versions.
   - `0x0002` (`FILE_HEADER`): Total file size, recommended chunk size, flags (`RESUME_SUPPORTED = 0x0001`), filename, and 64-char full SHA-256 digest.
   - `0x0003` (`CHUNK_DATA`): Sliced chunk payloads with 8-byte byte offset, chunk index, per-chunk CRC-32, and raw data bytes.
   - `0x0004` (`TRANSFER_ACK`): Status codes (`Success = 0x00`, `HashMismatch = 0x01`, `DiskFull = 0x02`, `Rejected = 0x03`, `PathUnsafe = 0x04`, `GenericError = 0x05`, `ResumeAccepted = 0x06`, `ResumeRejected = 0x07`), cumulative received bytes, and human-readable message.
   - `0x0005` (`TRANSFER_CANCEL`): Abort notification with numeric error code and cancellation reason string.
   - `0x0006` (`PING`) & `0x0007` (`PONG`): Heartbeat keep-alive probes.
   - `0x0008` (`RESUME_REQUEST`): 136-byte payload containing target `fileHash` (64B hex), verified `bytesReceivedSoFar` (8B BE), and `partialSha256` (64B hex).
   - `0x0009` (`BATCH_MANIFEST`): Manifest header with batch ID, total files count, aggregate byte count, and per-file relative paths and SHA-256 digests.

3. **Backward Compatibility & Downgrade Attack Prevention**:
   - Peeks the first 4 bytes of incoming TCP streams. If non-`'LNDP'` and evaluated as `<= 255`, safely delegates to legacy v1 transfer parser.
   - Enforces capability pinning (`CAP_PROTOCOL_V2`) to prevent malicious downgrade attacks on local networks.

---

### 2.3 Streaming Engine & Resilient Resumption

1. **Chunked Socket Streaming**:
   - Configurable chunk size from 64 KB up to 4 MB (default 64 KB).
   - Speed limit throttling integrated into the streaming loop via sleep-yield rate regulation.
   - Responsive cancellation tokens abort transfers in-flight within $\le 50$ ms without orphaned sockets or file handles.

2. **Atomic Verification & Resumption**:
   - Inbound files are written to `.part` temporary files.
   - When an interrupted transfer is re-initiated, the receiver verifies the `.part` file size and computes the cumulative partial SHA-256 digest up to that offset.
   - If sender accepts resumption (`ResumeAccepted = 0x06`), sender seeks directly to `byte_offset` and streams remaining chunks.
   - Upon completion, the entire file SHA-256 digest is verified against the header before atomically renaming the `.part` file to the destination filename via `std::filesystem::rename`.
   - On hash mismatch or unrecoverable error, corrupt partial files are cleaned up immediately.

---

### 2.4 WebRTC DataChannel Subsystem

1. **SCTP/DTLS Peer-to-Peer Streaming**:
   - Direct browser-to-browser, desktop-to-web, and desktop-to-desktop transfer via WebRTC DataChannels using `libdatachannel` (C++) and browser standard `RTCDataChannel`.
   - STUN/TURN fallback traversal powered by coturn (`turn:92.5.49.103:3478`).
   - High-performance binary streaming directly running Wire Protocol v2 frames inside RTCDataChannels.

---

### 2.5 Identity & Account Management

1. **User Authentication & Quotas**:
   - Native `IdentityClient` (C++) and web auth client managing user registration, login, and refresh tokens via JWT.
   - Tiered user accounts (Free vs Premium) with server-enforced daily transfer quotas and file size boundaries.
   - Presentation via `AccountPresenter` and `IAccountView` in desktop and web navigation headers.

---

### 2.6 Diagnostic Self-Test Engine (`DoctorEngine`)

1. **Probes & Diagnostics**:
   - Loopback TCP and UDP socket bind capability.
   - LAN multicast interface availability on port 8848.
   - TLS 1.3 cipher suite and OpenSSL initialization.
   - Writable storage directory permissions and available disk space.
   - Signaling server network reachability and latency probe.
   - STUN/TURN UDP binding probe.
2. **Execution Interfaces**:
   - CLI invocation: `lan_drop doctor` (human-readable) and `lan_drop doctor --json` (machine-readable for CI/CD and packaging checks).
   - In-app diagnostic reporting dialog.

---

### 2.7 Storage Security & PIN Protection

1. **Path Traversal & Symlink Defense**:
   - Filenames are sanitized via `isSafeFileName` rejecting control characters, directory separators, and path traversal sequences (`..`).
   - Canonical path resolution via `std::filesystem::relative` verifies that all target paths remain strictly inside the configured shared storage root.
   - Symlinks resolving outside the root directory are rejected.
   - Configurable file size ceiling (default 10 GB) prevents disk space exhaustion attacks.

2. **Cryptographic PIN Authentication**:
   - Folder protection PINs are hashed using **OpenSSL PBKDF2-HMAC-SHA256** with a 16-byte cryptographically secure random salt and 100,000 iterations (`$pbkdf2-sha256$i=100000$<salt_hex>$<key_hex>`).
   - Constant-time verification using `CRYPTO_memcmp` mitigates timing analysis attacks.
   - Progressive exponential delay penalty applied after consecutive failed authentication attempts.
   - Transparent in-place migration upgrades legacy unsalted PIN hashes upon first successful verification.

---

### 2.8 Desktop Presentation Layer (MVP)

1. **Qt 6 Desktop Application (`lan_drop gui`)**:
   - Modular Model-View-Presenter (MVP) architecture:
     - `IDiscoveryView` <-> `DiscoveryPresenter` (Radar discovery and peer list)
     - `ITransferView` <-> `TransferPresenter` (Active/completed transfer management)
     - `IRoomPairingView` <-> `RoomPairingPresenter` (Cloud room code & QR pairing)
     - `IStorageView` <-> `StoragePresenter` (PIN-locked shared folder browser)
     - `ISettingsView` <-> `SettingsPresenter` (Port, device name, theme, TLS configuration)
     - `IAccountView` <-> `AccountPresenter` (User auth, quota telemetry)
   - Dynamic dark and light themes via stylesheet injection (`QSS`).
   - 300ms non-blocking progress timer updating transfer metrics without UI stutter.
   - 100% headless testability using `MockView` and `QtTest`.

2. **GTK+ 3.0 Alternative Desktop Application (`lan_drop_gtk gui`)**:
   - Desktop view adhering to macOS Human Interface Guidelines (traffic lights, segmented controls).
   - Mobile view adapting to 390x812 viewport with bottom navigation bar.

---

## 3. Non-Functional Specifications

| Category | Specification | Detail |
| :--- | :--- | :--- |
| **C++ Standard** | ISO C++20 | Compiled with `-std=c++20`, strict warnings (`-Wall -Wextra`), thread sanitizers compatible. |
| **GUI Framework** | Qt 6.7+ / GTK+ 3.24+ | Native OS rendering, dark/light styling, FreeDesktop AppStream compliance. |
| **Signaling Backend**| Go 1.23 | High-concurrency Gorilla WebSockets, PostgreSQL 16 persistence, Redis-ready. |
| **Web Frontend** | TypeScript 5.8 / React 19 | Vite 6 build pipeline, Tailwind CSS, zero external WebRTC plugins. |
| **Cryptography** | OpenSSL 3.0+ | PBKDF2-HMAC-SHA256, TLS 1.3, DTLS 1.3, SHA-256 integrity digests. |
| **WebRTC Library** | `libdatachannel` v0.22+ | C++ WebRTC DataChannels over SCTP/DTLS. |
| **Memory Safety** | RAII Idiom | Zero raw pointer ownership, strict `std::unique_ptr`/`std::shared_ptr`, bounded allocations. |
| **Throughput** | $\ge 100$ MB/s | Capable of saturating Gigabit LAN and high-speed Wi-Fi links on modern hardware. |

---

## 4. Implementation Status Matrix

| Module | Status | Verification & Notes |
| :--- | :--- | :--- |
| **Unified TransferEngine** | **Implemented & Verified** | Shared across CLI and Qt GUI; chunked socket streaming, `.part` management, SHA-256 verified. |
| **Wire Protocol v2** | **Implemented & Verified** | Fixed 16B header, CRC-32 integrity, byte-offset resumption (0x0008), batch manifest (0x0009). |
| **WebRTC DataChannels** | **Implemented & Verified** | Peer-to-peer data streaming between Desktop (libdatachannel) and Web clients via coturn STUN/TURN. |
| **Go Signaling Server** | **Implemented & Verified** | Production deployment on Oracle Cloud VPS (`https://landrop-server.duckdns.org`), room pairing, auth. |
| **React Web Client** | **Implemented & Verified** | Full responsive web application hosted on VPS with QR generation, room joining, WebRTC streaming. |
| **Diagnostic Doctor** | **Implemented & Verified** | `lan_drop doctor` self-tests socket binds, TLS, disk, signaling latency, and STUN/TURN binding. |
| **Qt 6 Desktop GUI** | **Implemented & Verified** | Modular MVP architecture with 5 tabs, dark/light themes, 15/15 QtTest GUI tests passing. |
| **GTK+ 3 Desktop / Mobile** | **Implemented & Verified** | Preserved macOS HIG desktop layout and 390x812 responsive mobile interface. |
| **Cryptographic PIN** | **Implemented & Verified** | PBKDF2-HMAC-SHA256 (100,000 iterations), constant-time compare, progressive brute-force delay. |
| **Storage Security** | **Implemented & Verified** | Path traversal filtering, symlink escape checks, 10 GB configurable boundary. |
| **Speed Limit Throttling** | **Implemented & Verified** | Active token-bucket throttling inside socket and WebRTC streaming loops. |
| **Mobile C-ABI FFI Bridge** | **Implemented & Verified** | Plain C99 linkage (`LandropMobileBridge.h`), caller-allocated buffers, non-blocking lifecycle. |
| **AirDrop Interoperability** | **Developer Preview / Sim**| Bonjour service record generation and `/Discover` / `/Ask` payload synthesis in memory. |
| **NFC Fast Pairing** | **Developer Preview / Sim**| NDEF text payload generation and parsing (`LANDROP:NFC:v1`) in memory. |
| **Multicast LAN Discovery** | **Implemented (Lab Verified)**| Fully verified under non-isolated LAN; subject to physical AP IGMP snooping settings. |

---

## 5. Test Environment Requirements

For complete physical and automated verification of all LAN-Drop subsystems:
1. **Local Network Testing**:
   - Two physical hosts or bridged virtual machines on the same L2/L3 subnet.
   - UDP port 8848 open for multicast discovery (`239.255.88.48`).
   - TCP port 8848 open for direct socket file streaming.
2. **Cloud & WebRTC Testing**:
   - Outbound HTTPS (port 443) and WebSocket connections permitted to signaling server.
   - Outbound UDP ports 3478 (STUN/TURN) and 49152–65535 (relay media allocation) open.
3. **Headless CI/CD Testing**:
   - Linux host with offscreen Qt platform plugin (`export QT_QPA_PLATFORM=offscreen`).
   - CMake 3.22+, Ninja, GCC 13+ / Clang 16+, OpenSSL 3.0+ development headers.
