# LAN-Drop System Architecture

## 1. Verified Architecture & Layer Boundaries

```
+---------------------------------------------------------------------------------------------------------+
|                                           PRESENTATION LAYER                                            |
|   +------------------------------------+   +----------------------------+   +-----------------------+   |
|   |         QtDesktopApp (Qt 6)        |   |    GtkDesktopApp (GTK3)    |   |  Web Client (React 19)|   |
|   |   - Modern QSS (Dark / Light)      |   |   - macOS HIG Theme (CSS)  |   |   - TypeScript 5.8    |   |
|   |   - 5 Modular Views                |   |   - Traffic Lights         |   |   - Vite 6 + Tailwind |   |
|   |   - Live 300ms Progress Timer      |   |   - Mobile 390x812 Viewport|   |   - WebRTC DataChannel|   |
|   |   - Room Code / QR Pairing Dialog  |   +----------------------------+   |   - QR Scanner & Code |   |
|   +------------------------------------+                                    +-----------------------+   |
|        ^              ^              ^                 ^                ^                ^              |
|        |              |              |                 |                |                |              |
|  IDiscoveryView ITransferView IRoomPairingView   IStorageView    ISettingsView     IAccountView         |
|        ^              ^              ^                 ^                ^                ^              |
|        |              |              |                 |                |                |              |
|  Discovery   Transfer   RoomPairing     Storage    Settings     Account                                 |
|  Presenter   Presenter  Presenter       Presenter  Presenter    Presenter                               |
+---------------------------------------------------------------------------------------------------------+
                                                     |
                                                     v
+---------------------------------------------------------------------------------------------------------+
|                                               CORE ENGINE                                               |
|   +-----------------------+   +------------------------+   +----------------------------------------+   |
|   |    TransferEngine     |   |     WireProtocolV2     |   |       WebRtcSessionCoordinator         |   |
|   | (Unified Socket / RTC |   | (16B Header, CRC-32,   |   | (libdatachannel C++ WebRTC client,     |   |
|   |  streaming & resume)  |   |  0x0008 Resume Request)|   |  WebSocket signaling & STUN/TURN ICE)  |   |
|   +-----------------------+   +------------------------+   +----------------------------------------+   |
|   +-----------------------+   +------------------------+   +----------------------------------------+   |
|   |    MulticastBeacon    |   |    FileChunkHasher     |   |              DoctorEngine              |   |
|   | (UDP beacon parser &  |   | (Pure SHA-256 hash calc|   | (Self-test diagnostic probes: loopback,|   |
|   |  serializer logic)    |   |  & chunk verification) |   |  ports, TLS, storage, STUN/TURN, ping) |   |
|   +-----------------------+   +------------------------+   +----------------------------------------+   |
|   +----------------------------------------------------+   +----------------------------------------+   |
|   |                SharedFolderManager                 |   |             IdentityClient             |   |
|   | (Symlink & Path Traversal Guards, 10GB Boundary,   |   | (JWT user registration, login auth,    |   |
|   |  Cryptographic PBKDF2-HMAC-SHA256 PIN Security)    |   |  refresh tokens, and quota tracking)   |   |
|   +----------------------------------------------------+   +----------------------------------------+   |
|   +-----------------------+   +------------------------+   +----------------------------------------+   |
|   |      TlsContext       |   |       HttpClient       |   |             ChunkBuffer<T>             |   |
|   | (OpenSSL 3 CA check,  |   | (HTTPS Sync, Quota,    |   | (Thread-safe generic FIFO queue)       |   |
|   |  hostname validation) |   |  Truncation Guard)     |   |                                        |   |
|   +-----------------------+   +------------------------+   +----------------------------------------+   |
+---------------------------------------------------------------------------------------------------------+
             |                                                                             |
             v                                                                             v
+----------------------------------------+               +------------------------------------------------+
|             PEER SUBSYSTEM             |               |                 TASK SUBSYSTEM                 |
|      NetworkPeer (Abstract Base)       |               |          TransferTask (Abstract Base)          |
|       ├── LocalPeer (Subnet / P2P)     |               |           ├── P2PTransferTask (Memory/Disk)    |
|       └── CloudPeer (Relay / Sync)     |               |           └── HttpWebTransferTask (QR / Web)   |
|      filterPeers<T>() (RTTI cast)      |               |          State: Idle -> Transfer -> Done       |
+----------------------------------------+               +------------------------------------------------+
             |                                                                             |
             v                                                                             v
+----------------------------------------------------+   +------------------------------------------------+
|              MOBILE INTEROP SUBSYSTEM              |   |          CLOUD SIGNALING & TURN BACKEND        |
|  +--------------------+  +-----------------------+ |   |  +---------------------+  +-----------------+  |
|  | NfcPairingSession  |  | AirDropProtocolBridge | |   |  | Go Signaling Server |  |  coturn Server  |  |
|  | - Semicolon-delim  |  | - Formats mock HTTP   | |   |  | - Go 1.23 WebSockets|  | - STUN & TURN   |  |
|  |   NDEF payload     |  |   responses (_airdrop)| |   |  | - PostgreSQL 16 DB  |  | - UDP/TCP 3478  |  |
|  +--------------------+  +-----------------------+ |   |  | - Room Pairing APIs |  | - Dynamic Relay |  |
|  +-----------------------------------------------+ |   |  +---------------------+  +-----------------+  |
|  |              LandropMobileBridge              | |   +------------------------------------------------+
|  | - C-ABI FFI Plain C99 Export                  | |
|  | - Thread-safe non-blocking mobile lifecycle   | |
|  +-----------------------------------------------+ |
+----------------------------------------------------+
```

---

## 2. Decoupled Presentation Layer (MVP)

The presentation layer strictly enforces the **Model-View-Presenter (MVP)** architectural pattern to completely decouple visual controls from business logic and networking:
- **`IDiscoveryView`** <---> **`DiscoveryPresenter`** (Local subnet radar discovery)
- **`ITransferView`** <---> **`TransferPresenter`** (Transfer progress, rate limits, cancellation)
- **`IRoomPairingView`** <---> **`RoomPairingPresenter`** (WebRTC cloud room pairing & QR codes)
- **`IStorageView`** <---> **`StoragePresenter`** (PIN-secured shared directory access)
- **`ISettingsView`** <---> **`SettingsPresenter`** (Persistent user configurations and themes)
- **`IAccountView`** <---> **`AccountPresenter`** (User authentication and transfer quota telemetry)

This decoupling allows 100% headless testing via `MockView` (used in `tests/unit_tests.cpp`) as well as comprehensive GUI test suites via `tests/test_qt_gui.cpp` using offscreen Qt (`QT_QPA_PLATFORM=offscreen`).

---

## 3. Polymorphism & Generic Structures

1. **Dynamic Polymorphism**:
   - `NetworkPeer` -> `LocalPeer`, `CloudPeer`
   - `TransferTask` -> `P2PTransferTask`, `HttpWebTransferTask`
2. **Static Polymorphism & Templates**:
   - `ChunkBuffer<T>`: Thread-safe circular / FIFO queue buffer.
   - `filterPeers<T>()`: Template helper performing `std::dynamic_pointer_cast<T>` filtering across heterogeneous peer collections.

---

## 4. Build Systems & Targets

- **CMake** (Primary & Recommended):
  - Targets:
    - `landrop_core` (STATIC library containing all core transfer, protocol, and networking logic).
    - `lan_drop` (Qt 6 executable GUI and CLI `doctor` diagnostic suite).
    - `lan_drop_gtk` (GTK+ 3.0 executable GUI).
    - `lan_drop_tests` (Comprehensive unit test executable, 31 test suites, 319+ assertions).
    - `lan_drop_gui_tests` (Qt 6 GUI test executable with QtTest, 15 test items).
- **Makefile**:
  - Targets: `all` (`bin/lan_drop` [GTK3]), `test` (`bin/lan_drop_tests`), `mobile-test`, `gui`, `mobile-gui`, `clean`.

---

## 5. Security & Boundary Architecture

1. **TransferEngine & Socket Streaming**:
   - Single unified `TransferEngine` shared between CLI commands (`send`/`receive`) and Qt 6 GUI.
   - Transferred files stream into temporary `*.part` files with chunked sizing (64 KB default up to 4 MB).
   - Speed limit throttling natively integrated into the socket and WebRTC streaming loops.
   - Wire Protocol v2 frame headers validated with CRC-32 before processing.
   - Upon completion, the full-file SHA-256 checksum is computed and compared against the sender header.
   - On match, `std::filesystem::rename` performs an atomic move to the target file.
   - Incomplete or interrupted transfers retain the verified `.part` file, allowing byte-offset resumption via `ResumeRequestMessage` (`0x0008`).
   - Responsive cancellation tokens abort transfer in-flight within $\le 50$ ms without leaving orphaned sockets or file handles.

2. **Hardened Storage & PIN Security**:
   - Access PINs are hashed using **OpenSSL PBKDF2-HMAC-SHA256** with a 16-byte cryptographically secure random salt and 100,000 iterations (`$pbkdf2-sha256$i=100000$<salt_hex>$<key_hex>`).
   - Constant-time comparison using `CRYPTO_memcmp` protects against side-channel timing analysis attacks.
   - Progressive brute-force delay penalty on consecutive failed attempts.
   - Automatic migration upgrades legacy unsalted SHA-256 PIN hashes on next successful verification.
   - Inbound file paths are sanitized via `isSafeFileName` and canonical traversal checks (`std::filesystem::relative`).
   - Symlink targets are strictly checked to ensure they do not escape the root shared directory.
   - Configurable file size boundary (default 10 GB limit) prevents storage exhaustion.

3. **Transport Layer Security (TLS)**:
   - Client connections verify server certificates and hostnames by default using the system CA store (`X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS`).
   - Self-signed certificates require providing the expected SHA-256 fingerprint (`--tls <fingerprint>`).
   - Insecure TLS mode is strictly opt-in (`allowInsecureTls`).

---

## 6. Implementation Status Matrix

| Subsystem / Feature | Implementation Status | Verification & Technical Details |
| :--- | :--- | :--- |
| **Unified TransferEngine** | **Implemented & Verified** | Shared across CLI and Qt GUI; streaming TCP sockets and WebRTC channels; atomic `.part` renaming; SHA-256 verified. |
| **Wire Protocol v2** | **Implemented & Verified** | 16-byte fixed framing, CRC-32 integrity, byte-offset resumption (`0x0008`), batch manifest (`0x0009`), capability pinning. |
| **WebRTC P2P DataChannels** | **Implemented & Verified** | `libdatachannel` (C++) and browser standard WebRTC streaming files across NATs via STUN/TURN fallback. |
| **Go Signaling Server** | **Implemented & Verified** | Production Go 1.23 service deployed on VPS (`https://landrop-server.duckdns.org`); PostgreSQL 16 persistence; room pairing. |
| **React Web Client** | **Implemented & Verified** | TypeScript 5.8 / React 19 web app; QR code scanner/display; room pairing; direct streaming sender/receiver. |
| **Qt 6 Desktop GUI** | **Implemented & Verified** | 5 modular tabs; dark/light themes; direct IP and room code pairing; PIN storage lock; 15/15 GUI tests passing. |
| **Diagnostic DoctorEngine** | **Implemented & Verified** | CLI command `lan_drop doctor` and `--json`; automated validation of loopback, ports, TLS, disk, network, and STUN/TURN. |
| **GTK3 Desktop & Mobile GUI** | **Implemented & Verified** | macOS HIG styling for desktop and adaptive 390x812 viewport for mobile preview. |
| **MVP Presenter/View Arch** | **Implemented & Verified** | Complete isolation of business logic from UI; shared Presenters across GUI toolchains; 100% headless testability. |
| **Cryptographic PIN Security** | **Implemented & Verified** | PBKDF2-HMAC-SHA256 (100k iterations, 16B salt), constant-time `CRYPTO_memcmp`, progressive delay, auto-migration. |
| **Storage Security & Guards** | **Implemented & Verified** | Strict Path Traversal defense (`isSafeFileName`), symlink escape prevention, 10 GB configurable boundary. |
| **Resilient Resumption** | **Implemented & Verified** | Byte-offset seeking with partial SHA-256 validation; `.part` file retention; fallback to restart on hash mismatch. |
| **Speed Limit Throttling** | **Implemented & Verified** | Rate regulation implemented in streaming loops across both C++ TransferEngine and TypeScript WebRTC streams. |
| **Mobile C-ABI FFI Bridge** | **Implemented & Verified** | Plain C99 linkage in `LandropMobileBridge.h`; caller-allocated buffers; non-blocking asynchronous lifecycle. |
| **AirDrop Emulation** | **Developer Preview / Sim** | In-memory Bonjour service records and mock HTTP `/Discover` and `/Ask` payload synthesis. |
| **NFC Fast Pairing** | **Developer Preview / Sim** | In-memory generation and parsing of `LANDROP:NFC:v1` NDEF text records. |
| **Multicast Subnet Discovery** | **Implemented (Lab Verified)** | `MulticastBeacon` verified in local lab environments; operational status depends on network switch IGMP snooping. |

---

## 7. Environment Requirements for Physical Verification

For complete end-to-end testing of LAN-Drop across physical devices:
1. **Network Topology**: Two physical computers or bridged virtual machines on the same L2/L3 subnet.
2. **IGMP / Multicast**: Network switches/routers must forward UDP multicast packets on port 8848 (`239.255.88.48`). Wireless client isolation must be disabled.
3. **Firewall Rules**: Open inbound TCP port 8848 (and UDP 8848) on both test machines.
4. **Certificates**: Custom CA root certificates installed in system trust store for TLS validation, or use fingerprint pinning.

---

## 8. AI-Assisted Engineering & Development Methodology

The system architecture outlined above is strictly deterministic and contains **zero runtime artificial intelligence models, neural runtimes, or external AI API dependencies**. All runtime data transfers, protocol framing, cryptographic guarantees, and network routing are handled natively by C++20, Qt 6, GTK3, OpenSSL 3, libdatachannel, and Go.

During the software development lifecycle, auxiliary Artificial Intelligence tools (Google Antigravity, LLM-based agents, and the multi-agent framework described in `.agents/`) were utilized for architectural decomposition, code generation, refactoring, comprehensive test synthesis (31 test suites), and CI/CD triage under strict human oversight and automated compiler/test verification gates.

For the complete technical disclosure on the AI toolchain, agent roles, governance principles, and verifiable evidence, refer to:
📄 **[Disclosure: Artificial Intelligence in the Software Development Process](AI_DEVELOPMENT_DISCLOSURE.md)**
