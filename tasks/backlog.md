# LAN-Drop Development Backlog & Milestone History

## Project Status: Production-Ready (v1.0.2)

LAN-Drop has completed all foundational engineering milestones, security hardening phases, cross-platform packaging pipelines, and unified ecosystem deployment across Desktop (C++20 / Qt 6), Web (TypeScript / React 19), Mobile Bridge (C-ABI FFI), and Cloud Signaling (Go 1.23 / PostgreSQL 16 / coturn).

---

## 1. Foundational Engineering Tasks (Milestone 1)

### TASK-001: Align CMakeLists.txt with GTK3/X11/Qt6 Toolchains
- **Status**: DONE
- **Scope**: `CMakeLists.txt`, `packaging/`
- **Verification**: Built and linked clean across GCC 14, Clang 18, and MSVC 2022; CPack generated `.deb` and `.rpm`.

### TASK-002: Remediate Path Traversal & Memory Allocation Bounds in Receiver
- **Status**: DONE
- **Scope**: `src/main.cpp`, `include/landrop/utils/Types.hpp`, `src/core/TransferEngine.cpp`
- **Verification**: 18/18 security assertions passed; traversal sequences (`..`), null bytes, and 4MB+ chunk attacks blocked.

### TASK-003: Thread-Safe ChunkBuffer Generic Queue
- **Status**: DONE
- **Scope**: `include/landrop/core/ChunkBuffer.hpp`
- **Verification**: Replaced unprotected references with mutex-guarded copy semantics and bounded FIFO pop operations.

### TASK-004: Intermediate File Streaming & Verified Disk Persistence (`.part`)
- **Status**: DONE
- **Scope**: `include/landrop/core/TransferEngine.hpp`, `src/core/TransferEngine.cpp`
- **Verification**: Atomic renaming via `std::filesystem::rename` only after bit-exact SHA-256 match; automatic cleanup on abort.

### TASK-005: Endianness Normalization for Binary Framing (Wire Protocol v2)
- **Status**: DONE
- **Scope**: `include/landrop/core/WireProtocolV2.hpp`, `src/core/WireProtocolV2.cpp`, `web/src/core/protocol/WireProtocolV2.ts`
- **Verification**: Strict Big-Endian integer conversions (`htons`/`htonl`/`toBigEndian64`) verified across x86_64, ARM64, and browser runtimes.

### TASK-006: Asynchronous Network Session & Non-Blocking Timeouts
- **Status**: DONE
- **Scope**: `include/landrop/core/NetworkSession.hpp`, `src/core/NetworkSession.cpp`
- **Verification**: `SO_RCVTIMEO` and `SO_SNDTIMEO` socket bounds configured; $\le 50$ms cancellation response.

---

## 2. Advanced Ecosystem Milestones (Milestones 2–5)

### MILESTONE-2A: Cryptographic Storage & Hardened PIN Authentication
- **Status**: DONE
- **Scope**: `include/landrop/core/SharedFolderManager.hpp`, `src/core/SharedFolderManager.cpp`
- **Deliverables**: PBKDF2-HMAC-SHA256 (100,000 iterations, 16-byte cryptographically secure salt), constant-time `CRYPTO_memcmp`, progressive brute-force delay, in-place hash auto-migration.

### MILESTONE-2B: Modular Desktop GUI (Model-View-Presenter Architecture)
- **Status**: DONE
- **Scope**: `include/landrop/ui/`, `src/ui/`, `tests/test_qt_gui.cpp`
- **Deliverables**: Qt 6 GUI with 5 modular tabs (`QtDiscoveryWidget`, `QtTransferWidget`, `QtRoomPairingWidget`, `QtStorageWidget`, `QtSettingsWidget`), dynamic dark/light themes, live 300ms timer, 15/15 GUI tests passing.

### MILESTONE-3A: Unified WebRTC Cloud Ecosystem & Go Signaling Server
- **Status**: DONE
- **Scope**: `server/`, `web/`, `include/landrop/core/WebRtcSessionCoordinator.hpp`
- **Deliverables**: Go 1.23 WebSocket signaling server with PostgreSQL 16 persistence, React 19 web client, bidirectional WebRTC DataChannel streaming via `libdatachannel`.

### MILESTONE-3B: Cross-Device Room Pairing & Discovery Sync
- **Status**: DONE
- **Scope**: `server/internal/signaling/room_manager.go`, `src/ui/QtRoomPairingWidget.cpp`, `web/src/core/webrtc/WebRtcManager.ts`
- **Deliverables**: 6-character room codes with case-insensitive normalization, QR deep-links, dual-direction hosting (Desktop host -> Web join, Web host -> Desktop join).

### MILESTONE-3C: Mobile Ecosystem Bridge (C-ABI FFI)
- **Status**: DONE
- **Scope**: `include/landrop/mobile/LandropMobileBridge.h`, `src/mobile/LandropMobileBridge.cpp`
- **Deliverables**: C99 linkage FFI interface, caller-allocated buffer model, non-blocking asynchronous lifecycle for Flutter/Swift/Kotlin integrations.

### MILESTONE-3D: Resilient Streaming & Byte-Offset Resumption (Wire Protocol v2)
- **Status**: DONE
- **Scope**: `include/landrop/core/WireProtocolV2.hpp`, `src/core/WireProtocolV2.cpp`, `web/src/core/streaming/`
- **Deliverables**: OpCode `0x0008` `ResumeRequestMessage` (136B), `ResumeAccepted` (`0x06`) / `ResumeRejected` (`0x07`), partial SHA-256 offset validation, OpCode `0x0009` `BatchManifestMessage`.

### MILESTONE-3E: Production Cloud Infrastructure Deployment
- **Status**: DONE
- **Scope**: Oracle Cloud VPS (`ubuntu@92.5.49.103`), Docker Compose, coturn, systemd
- **Deliverables**: Live signaling server (`https://landrop-server.duckdns.org`), Web client hosting, coturn STUN/TURN (`turn:92.5.49.103:3478`).

### MILESTONE-4: Diagnostic Self-Test Doctor Engine
- **Status**: DONE
- **Scope**: `include/landrop/core/DoctorEngine.hpp`, `src/core/DoctorEngine.cpp`, `src/main.cpp`
- **Deliverables**: `lan_drop doctor` and `--json` executing automated probes for socket loopback, ports, TLS, disk space, signaling latency, and STUN/TURN binding.

### MILESTONE-5: Documentation Overhaul & Installation Verification
- **Status**: DONE
- **Scope**: `README.md`, `LICENSE`, `docs/SPEC.md`, `docs/ARCHITECTURE.md`, `docs/PACKAGING.md`, `docs/PROTOCOL_V2.md`
- **Deliverables**: Complete English overhaul, verified Fedora DNF installation/uninstallation in clean container, AppImage FUSE 2 diagnosis, Ubuntu deb dependency documentation, bit-exact RPM host verification.

---

## 3. Future Roadmap Backlog (Post-v1.0.2)

| Task ID | Component | Description | Priority |
| :--- | :--- | :--- | :--- |
| **ROADMAP-001** | Mobile Apps | Native Flutter and iOS Swift UI apps consuming `LandropMobileBridge`. | P2 |
| **ROADMAP-002** | Hardware NFC | Linux/Android hardware NFC controller integration for tap-to-share. | P3 |
| **ROADMAP-003** | Native AWDL | Native Apple Wireless Direct Link (AWDL) driver integration for zero-config AirDrop. | P3 |
| **ROADMAP-004** | E2EE Multi-Hop | Multi-hop onion routed relaying for high-privacy environments. | P4 |
