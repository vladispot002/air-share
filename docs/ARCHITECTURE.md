# LAN-Drop System Architecture

## 1. Verified Architecture & Layer Boundaries

```
+---------------------------------------------------------------------------+
|                          PRESENTATION LAYER                               |
|   +-------------------------------------+   +--------------------------+  |
|   |         GtkDesktopApp (GTK3)        |   | DesktopGuiEngine (X11)   |  |
|   |   - macOS HIG Theme (CSS)           |   |   - Fallback Raw Xlib    |  |
|   |   - Traffic Lights, Segmented Tabs  |   |   - Minimal renderer     |  |
|   |   - Mobile 390x812 Viewport         |   +--------------------------+  |
|   +-------------------------------------+                                 |
|        ^                 ^                  ^                 ^           |
|        |                 |                  |                 |           |
|  IDiscoveryView    ITransferView       IStorageView    ISettingsView      |
|        ^                 ^                  ^                 ^           |
|        |                 |                  |                 |           |
|  DiscoveryPresenter TransferPresenter StoragePresenter SettingsPresenter |
+---------------------------------------------------------------------------+
                                     |
                                     v
+---------------------------------------------------------------------------+
|                             CORE ENGINE                                   |
|   +----------------------+   +-------------------+   +----------------+   |
|   |   MulticastBeacon    |   |  NetworkSession   |   | FileChunkHasher|   |
|   | (Mock counter logic; |   | (POSIX TCP Socket |   |  (SHA-256 pure |   |
|   |  payload serializer) |   |  sendAll/recvEx)  |   |  implementation|   |
|   +----------------------+   +-------------------+   +----------------+   |
|              +-----------------------------+   +-------------------+      |
|              |     SharedFolderManager     |   |  ChunkBuffer<T>   |      |
|              |  (Local Index & PIN Lock)   |   | (Thread-safe FIFO)|      |
|              +-----------------------------+   +-------------------+      |
+---------------------------------------------------------------------------+
         |                                           |
         v                                           v
+-----------------------------+             +-------------------------------+
|       PEER SUBSYSTEM        |             |        TASK SUBSYSTEM         |
|  NetworkPeer (Abstract Base)|             |  TransferTask (Abstract Base) |
|   ├── LocalPeer (Subnet/P2P)|             |   ├── P2PTransferTask (Memory)|
|   └── CloudPeer (Relay/Sync)|             |   └── HttpWebTransferTask (QR)|
|  filterPeers<T>() (RTTI cast)|            |  State: Idle->Transfer->Done  |
+-----------------------------+             +-------------------------------+
                                     |
                                     v
+---------------------------------------------------------------------------+
|                          MOBILE INTEROP SUBSYSTEM                         |
|  +--------------------+  +----------------------+  +-------------------+  |
|  | NfcPairingSession  |  | AirDropProtocolBridge|  |LandropMobileBridge|  |
|  | - Semicolon-delim  |  | - Formats mock HTTP  |  | - C-ABI FFI Export|  |
|  |   string parser    |  |   strings (_airdrop) |  | - Returns mock    |  |
|  +--------------------+  +----------------------+  +-------------------+  |
+---------------------------------------------------------------------------+
```

## 2. Decoupled Presentation Layer (MVP)
The user interface adheres to Model-View-Presenter (MVP):
- **`IDiscoveryView`** <---> **`DiscoveryPresenter`**
- **`ITransferView`** <---> **`TransferPresenter`**
- **`IStorageView`** <---> **`StoragePresenter`**
- **`ISettingsView`** <---> **`SettingsPresenter`**

This decoupling allows 100% headless testing via `MockView` (used in `tests/unit_tests.cpp`) without requiring an X11 or Wayland display server.

## 3. Polymorphism in the System
1. **Dynamic Polymorphism**:
   - `NetworkPeer` -> `LocalPeer`, `CloudPeer`
   - `TransferTask` -> `P2PTransferTask`, `HttpWebTransferTask`
2. **Static Polymorphism & Templates**:
   - `ChunkBuffer<T>`: Thread-safe generic buffer for chunk storage.
   - `filterPeers<T>()`: Template helper performing `std::dynamic_pointer_cast<T>` filtering across heterogeneous peer vectors.

## 4. Verified Build Systems & Toolchain
- **Makefile** (Primary & Working):
  - Requires `g++` (C++20), `pkg-config`, `gtk+-3.0`, `X11`, `pthread`, and `-ldl`.
  - Targets: `all`, `test`, `run` (demo), `mobile-test`, `gui`, `mobile-gui`, `clean`.
- **CMakeLists.txt** (Known Defect):
  - Currently missing `pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)` and `${CMAKE_DL_LIBS}`. Fails compilation on clean systems where CMake is available.

## 5. Current Architecture Limitations & Known Gaps
1. **AirDrop and Mobile Interoperability**:
   - `AirDropProtocolBridge` formats in-memory string representations of HTTP responses and mDNS records. It does not interface with Avahi, Apple Wireless Direct Link (AWDL), or BLE.
   - `NfcPairingSession` serializes plain text semicolon-separated strings rather than binary NDEF frames.
   - `LandropMobileBridge` discovery returns static mock records.
2. **Transfer Pipeline**:
   - The standalone CLI socket pipeline (`runSender` / `runReceiver`) transfers whole files over TCP with full-file SHA-256 verification, but writes directly to target files without intermediate `.part` files.
   - `P2PTransferTask::processChunk` verifies chunk hashes in memory without streaming to disk.
   - Progress reporting in `TransferPresenter::tick` is driven by a synthetic simulation timer for UI responsiveness rather than connected socket events.
3. **Multicast Discovery**:
   - `MulticastBeacon` validates payload structure and maintains internal state counters, but has not yet bound actual UDP multicast sockets.
