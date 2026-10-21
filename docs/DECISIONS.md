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
