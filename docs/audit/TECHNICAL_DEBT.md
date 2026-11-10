# LAN-Drop: Technical Debt & Architectural Weaknesses Audit

**Date**: 2026-11-10  
**Target**: Branch `audit/production-readiness`  
**Lead Author**: Chief Software Architect  
**Classification**: Architectural Maintenance & Refactoring Registry

---

## 1. Executive Summary

This registry catalogs all known technical debt, architectural coupling, dead code, simulated placeholders, and concurrency hazards identified during the Production Readiness Audit. Each item is classified by impact, architectural component, and recommended remediation.

```
+-------------------------------------------------------------------------------+
|                        TECHNICAL DEBT SEVERITY BREAKDOWN                      |
|                                                                               |
|   [ Architectural Coupling & Module Leakage ] .......... 4 items              |
|   [ Concurrency, Threading & Deadlock Hazards ] ....... 3 items              |
|   [ Performance & Memory Allocation Inefficiencies ] .. 3 items              |
|   [ Dead Code, Simulated Stubs & Illusions ] .......... 6 items              |
+-------------------------------------------------------------------------------+
```

---

## 2. Architectural Coupling & Module Boundary Debt

### DEBT-01: Coupling of `landrop_core` to Qt Framework
- **Files**: [`CMakeLists.txt:54-69`](file:///home/vladis/lan-drop/CMakeLists.txt#L54-L69)
- **Description**: The static library target `landrop_core` is declared with `find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent Test)` and links `Qt6::Core` and `Qt6::Network`. UI Presenters (`DiscoveryPresenter.cpp`, `TransferPresenter.cpp`, `StoragePresenter.cpp`, `SettingsPresenter.cpp`) and `QrCodeGenerator.cpp` are bundled into the core library.
- **Consequence**:
  - `landrop_core` cannot be compiled for mobile (Android NDK / iOS Swift) or embedded/headless servers without installing Qt 6.
  - Violates clean separation of concerns (Core Engine should depend only on C++20 standard library and OpenSSL).
- **Remediation**:
  1. Split `CMakeLists.txt` into three libraries:
     - `landrop_core`: Pure C++20 engine (`TransferEngine`, `WireProtocolV2`, `DoctorEngine`, `SignalingClient`, `TlsContext`, `NetworkSession`, `FileChunkHasher`). Links only `OpenSSL` and POSIX/Winsock.
     - `landrop_mobile`: C-ABI FFI wrapper (`LandropMobileBridge`). Links `landrop_core`.
     - `landrop_ui_qt`: Qt 6 Widgets and Presenters. Links `landrop_core` and `Qt6`.

---

### DEBT-02: Synchronous Connection Processing in `TransferEngine::serverLoop`
- **Files**: [`src/core/TransferEngine.cpp:772-824`](file:///home/vladis/lan-drop/src/core/TransferEngine.cpp#L772-L824)
- **Description**: The background listener loop accepts incoming client socket connections and immediately calls `handleIncomingConnection(...)` synchronously on the listener thread:
  ```cpp
  while (m_serverRunning.load()) {
      auto clientSession = m_listenSession->acceptConnection(&err);
      if (clientSession) {
          handleIncomingConnection(std::move(clientSession), ...); // BLOCKS LISTENER THREAD
      }
  }
  ```
- **Consequence**:
  - While one peer is transferring a 10 GB file over TCP, the entire server is blocked from accepting any other connections from nearby peers.
  - Other peers receive connection timeouts (`ECONNREFUSED` or timeout).
- **Remediation**:
  Dispatch accepted connections to an asynchronous `std::jthread` or a worker thread pool, tracking active sessions in a thread-safe map.

---

### DEBT-03: Dual Protocol Stack (Wire Protocol v1 vs v2 Disconnect)
- **Files**:
  - [`src/core/TransferEngine.cpp:193-204, 508-546`](file:///home/vladis/lan-drop/src/core/TransferEngine.cpp#L193-L204)
  - [`src/core/WireProtocolV2.cpp`](file:///home/vladis/lan-drop/src/core/WireProtocolV2.cpp)
  - [`web/src/core/protocol/WireProtocolV2.ts`](file:///home/vladis/lan-drop/web/src/core/protocol/WireProtocolV2.ts)
- **Description**: Two distinct wire protocols exist in the codebase:
  - **Protocol v1**: Hardcoded raw byte offsets (`uint32 nameLen`, `char name[]`, `uint64 fileSize`, `char sha256[64]`). Used in `TransferEngine.cpp`. Endian-sensitive, lacks magic bytes, lacks opcodes, and lacks chunk-level CRC-32.
  - **Protocol v2**: 16-byte binary framing (`LNDP`), Big-Endian, CRC-32, message types, and resumption opcodes. Used in `web/` and `tests/unit_tests.cpp:1855`.
- **Consequence**: Code duplication, technical debt, and 100% failure on cross-platform transfers between Desktop and Web.
- **Remediation**: Standardize `TransferEngine` entirely on `WireProtocolV2`.

---

### DEBT-04: Unconditional Inclusion of WebRTC Stubs in CMake
- **Files**: [`CMakeLists.txt:26-29`](file:///home/vladis/lan-drop/CMakeLists.txt#L26-L29)
- **Description**: Even when `LANDROP_ENABLE_WEBRTC=OFF`, `WebRtcDataChannel.cpp`, `SignalingClient.cpp`, `TurnConfigClient.cpp`, and `WebRtcSessionCoordinator.cpp` are compiled into `landrop_core`. Inside these files, massive `#if defined(LANDROP_HAVE_WEBRTC)` blocks wrap dummy implementations.
- **Remediation**: Isolate WebRTC source files in CMake so they are only compiled when `LANDROP_ENABLE_WEBRTC` is enabled.

---

## 3. Concurrency, Threading & Lifecycle Hazards

### DEBT-05: Worker Thread Join Deadlock in Mobile Bridge
- **Files**: [`src/mobile/LandropMobileBridge.cpp:350-366, 427-443`](file:///home/vladis/lan-drop/src/mobile/LandropMobileBridge.cpp#L350-L366)
- **Description**:
  ```cpp
  int landrop_mobile_webrtc_send_file_async(...) {
      auto* ctx = static_cast<LandropMobileContext*>(handle);
      std::lock_guard<std::recursive_mutex> lock(ctx->mutex); // Mutex held
      if (ctx->transferWorker.joinable()) {
          ctx->transferWorker.join(); // Blocks waiting for worker thread
      }
  ```
  The worker thread invokes:
  ```cpp
      [ctx](const landrop::TransferProgressUpdate& upd) {
          std::lock_guard<std::recursive_mutex> lock(ctx->mutex); // Attempts to acquire mutex
  ```
- **Consequence**: Classic deadlock. If a new transfer is requested while an old worker is wrapping up, both threads hang permanently.
- **Remediation**: Move the `transferWorker` instance out of the mutex scope before calling `.join()`.

---

### DEBT-06: Shared Pointer Circular Reference in Qt UI
- **Files**: [`src/ui/qt/QtDesktopApp.cpp:643-650`](file:///home/vladis/lan-drop/src/ui/qt/QtDesktopApp.cpp#L643-L650)
- **Description**:
  ```cpp
  coordinator->onChannelOpened = [this, coordinator, code](std::shared_ptr<ITransportChannel> channel) {
      // coordinator is captured by value inside its own member callback!
  };
  ```
- **Consequence**: Creates a cyclic reference (`coordinator -> onChannelOpened -> coordinator`). The session coordinator object is never destructed, leaking sockets, memory, and thread handles upon room closure.
- **Remediation**: Capture a `std::weak_ptr<WebRtcSessionCoordinator>` in the lambda.

---

### DEBT-07: Unchecked Disk Write Failures Misreported as Hash Mismatches
- **Files**: [`src/core/TransferEngine.cpp:656, 683-686`](file:///home/vladis/lan-drop/src/core/TransferEngine.cpp#L656)
- **Description**:
  `outFile.write(reinterpret_cast<const char*>(chunkBuf.data()), chunkSize);` is invoked without checking `outFile.fail()` or `outFile.bad()`. If the disk fills up, writes fail silently, `totalReceived` continues to increment, and the post-transfer hash verification fails.
- **Consequence**: The system reports `"Integrity verification failed: SHA-256 mismatch"` instead of `"Disk write error: No space left on device"`, confusing users and masking fatal I/O errors.
- **Remediation**: Add explicit stream error checks after every `write()`.

---

## 4. Performance & Memory Inefficiencies

### DEBT-08: O(N) Buffer Shift in `WebRtcDataChannel::receiveExact`
- **Files**: [`src/core/WebRtcDataChannel.cpp:161-164`](file:///home/vladis/lan-drop/src/core/WebRtcDataChannel.cpp#L161-L164)
- **Description**:
  ```cpp
  m_rxBuffer.erase(m_rxBuffer.begin(), m_rxBuffer.begin() + m_rxOffset);
  m_rxOffset = 0;
  ```
  Every time a chunk is read, `std::vector::erase` shifts the remaining bytes in memory. For large incoming buffers, this causes severe CPU overhead and cache thrashing during high-speed transfers.
- **Remediation**: Replace `std::vector<uint8_t>` with a circular ring buffer or `std::deque<uint8_t>`.

---

### DEBT-09: Redundant Double-Pass SHA-256 Disk I/O
- **Files**: [`src/core/TransferEngine.cpp:173, 240, 656, 683`](file:///home/vladis/lan-drop/src/core/TransferEngine.cpp#L173)
- **Description**:
  - **Sender**: Reads entire file from disk to compute SHA-256 (`hasher.computeFullFileHash`). Then re-reads the entire file chunk-by-chunk to stream over the socket.
  - **Receiver**: Writes received chunks to disk. Closes file. Then re-reads the entire file from disk to verify the SHA-256 hash.
- **Consequence**: For a 5 GB file, this causes **10 GB of disk reads on the sender** and **5 GB of writes + 5 GB of reads on the receiver**. On mobile flash storage, this causes thermal throttling and excessive battery drain.
- **Remediation**: Stream and hash incrementally in a single pass using an in-flight `crypto::Sha256` context.

---

### DEBT-10: In-Memory Blob Accumulation in Web Client
- **Files**: [`web/src/core/streaming/FileStreamReceiver.ts:116-231`](file:///home/vladis/lan-drop/web/src/core/streaming/FileStreamReceiver.ts#L116-L231)
- **Description**:
  Received chunks are accumulated in a JavaScript array: `chunks.push(chunk.data)`.
- **Consequence**: Storing large files (>1.5 GB) in RAM causes browser tab crashes (V8 Out-Of-Memory).
- **Remediation**: Implement File System Access API (`createWritable()`) or Origin Private File System (OPFS) direct-to-disk streaming.

---

## 5. Dead Code, Simulated Stubs & Illusions

| Component / Artifact | File Location | Nature of Debt | Remediation Action |
| :--- | :--- | :--- | :--- |
| **`AirDropProtocolBridge`** | [`src/mobile/AirDropProtocolBridge.cpp`](file:///home/vladis/lan-drop/src/mobile/AirDropProtocolBridge.cpp) | Generates dummy Bonjour strings in RAM; zero hardware AWDL/BLE integration. | Deprecate or document as an experimental format generator. |
| **`NfcPairingSession`** | [`src/mobile/NfcPairingSession.cpp`](file:///home/vladis/lan-drop/src/mobile/NfcPairingSession.cpp) | Formats a static text string; zero hardware NFC interaction. | Retain as payload parser; decouple from mobile bridge. |
| **Mobile Discovery Mock** | [`src/mobile/LandropMobileBridge.cpp:528-531`](file:///home/vladis/lan-drop/src/mobile/LandropMobileBridge.cpp#L528) | Hardcodes fake peers `"Desktop-Workstation"` and `"iPhone-AirDrop"`. | Wire up real `MulticastBeacon` listener callbacks. |
| **Mobile Send File Stub** | [`src/mobile/LandropMobileBridge.cpp:571`](file:///home/vladis/lan-drop/src/mobile/LandropMobileBridge.cpp#L571) | Returns `LANDROP_OK` without transmitting file data. | Wire up `TransferEngine::sendFile`. |
| **`HttpWebTransferTask`** | [`src/tasks/HttpWebTransferTask.cpp`](file:///home/vladis/lan-drop/src/tasks/HttpWebTransferTask.cpp) | Formats URL/QR strings; no local HTTP download daemon exists to serve files. | Remove or implement local embedded HTTP file server. |
| **UI Transfer Simulation** | [`src/ui/TransferPresenter.cpp:25-65`](file:///home/vladis/lan-drop/src/ui/TransferPresenter.cpp#L25-L65) | Populates 6 dummy tasks (`project-assets.zip`, etc.) and increments fake bytes on a timer. | Remove sample data in production builds; bind strictly to `TransferEngine` events. |
| **UI Pause/Resume Illusion** | [`src/tasks/TransferTask.cpp:19-29`](file:///home/vladis/lan-drop/src/tasks/TransferTask.cpp#L19-L29) | `task->pause()` sets an enum; underlying socket streaming continues unabated. | Implement actual channel-level pausing or disable the Pause button in UI. |
| **`e2ee_public_key`** | [`backend/signaling/internal/protocol/messages.go:72`](file:///home/vladis/lan-drop/backend/signaling/internal/protocol/messages.go#L72) | Field defined in envelope protocol, but never evaluated or checked. | Implement true SAS peer verification or remove field. |

---

## 6. Cleanup & Refactoring Roadmap

```
+-------------------------------------------------------------------------------+
|                       TECHNICAL DEBT REMEDIATION SPRINTS                      |
|                                                                               |
|  [ Sprint 1: Deadlocks & Fatal Flaws ]                                        |
|    - DEBT-05: Eliminate worker thread join deadlock in Mobile Bridge          |
|    - DEBT-06: Break QtDesktopApp shared_ptr cyclic reference                  |
|    - DEBT-07: Add explicit outFile.write() error handling                     |
|                                                                               |
|  [ Sprint 2: Core Decoupling & Protocol Unification ]                         |
|    - DEBT-01: Decouple landrop_core from Qt 6 runtime                         |
|    - DEBT-03: Standardize TransferEngine on WireProtocolV2                    |
|    - DEBT-02: Asynchronous worker dispatch in TransferEngine::serverLoop     |
|                                                                               |
|  [ Sprint 3: Performance & Memory Optimization ]                              |
|    - DEBT-08: Replace vector erase with RingBuffer in WebRtcDataChannel       |
|    - DEBT-09: Single-pass in-flight streaming SHA-256                         |
|    - DEBT-10: File System Access API / OPFS in Web client                     |
|                                                                               |
|  [ Sprint 4: Dead Code & Mock Purge ]                                         |
|    - Remove dummy sample data from TransferPresenter in production builds     |
|    - Wire dynamic MulticastBeacon into LandropMobileBridge                    |
|    - Clarify status of AirDrop and NFC simulation modules in documentation    |
+-------------------------------------------------------------------------------+
```
