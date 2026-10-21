
# Development Backlog

## Project Goal
Harden, secure, and complete the LAN-Drop peer-to-peer file transfer engine, resolving security vulnerabilities, build issues, and concurrency risks while establishing a verified resilient pipeline.

## Status Legend
- TODO
- IN_PROGRESS
- REVIEW
- BLOCKED
- DONE

---

## Tasks

### TASK-001: Align CMakeLists.txt with GTK3/X11 Dependencies
- **Status**: TODO
- **Priority**: High (P0 - Build & Toolchain Integrity)
- **Owner**: developer
- **Depends on**: none
- **Scope**: `CMakeLists.txt`
- **Acceptance Criteria**:
  - [ ] Add `find_package(PkgConfig REQUIRED)` and `pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)`.
  - [ ] Link `PkgConfig::GTK3` and `${CMAKE_DL_LIBS}` in `target_link_libraries`.
  - [ ] Validate that when CMake is available, `CMakeLists.txt` configures and links all targets identical to `Makefile`.
- **Verification**:
  - `make all && make test` continues to succeed without regression.
- **Result**: Not started.

### TASK-002: Remediate Path Traversal & Memory Allocation Bounds in Receiver
- **Status**: TODO
- **Priority**: High (P0 - Security Vulnerability)
- **Owner**: developer
- **Depends on**: none
- **Scope**: `src/main.cpp` (`runReceiver`)
- **Acceptance Criteria**:
  - [ ] Sanitize incoming `fileName` to strip directory components (e.g. using `std::filesystem::path(fileName).filename()`).
  - [ ] Reject path traversal sequences (`..`, absolute paths) attempting to escape `saveDir`.
  - [ ] Enforce upper limit on `chunkSize` (e.g., maximum 4 MB or configured chunk size) before buffer allocation to prevent DoS via `bad_alloc`.
- **Verification**:
  - Test receiving files with relative and malicious paths (`../../test.txt`).
  - Loopback transfer demo (`make run`) passes.
- **Result**: Not started.

### TASK-003: Fix Mutex Race & Dangling Reference in ChunkBuffer::operator[]
- **Status**: TODO
- **Priority**: High (P0 - Memory Safety)
- **Owner**: developer
- **Depends on**: none
- **Scope**: `include/landrop/core/ChunkBuffer.hpp`
- **Acceptance Criteria**:
  - [ ] Prevent returning references `T&` to internal vector elements after releasing `m_mutex`.
  - [ ] Provide thread-safe value retrieval (e.g. `bool getItemAt(size_t index, T& outItem) const` or return `std::optional<T>`).
  - [ ] Update unit test assertions in `tests/unit_tests.cpp` to verify safe concurrent access.
- **Verification**:
  - `make test` passes all `testChunkBuffer` assertions (11 assertions maintained).
- **Result**: Not started.

### TASK-004: Intermediate File Streaming & Verified Disk Persistence
- **Status**: TODO
- **Priority**: Medium (P1 - Core Reliability)
- **Owner**: developer
- **Depends on**: TASK-002
- **Scope**: `src/main.cpp`, `src/tasks/P2PTransferTask.cpp`
- **Acceptance Criteria**:
  - [ ] Write incoming files to temporary destination (`<filename>.part`) during transfer.
  - [ ] Atomically rename `<filename>.part` to final filename only after complete SHA-256 validation.
  - [ ] Remove incomplete `.part` files on transmission failure or abort.
- **Verification**:
  - Automated transfer test with forced interruption verifies `.part` cleanup or isolation.
  - `make run` demo succeeds.
- **Result**: Not started.

### TASK-005: Endianness Normalization for Binary Socket Framing
- **Status**: TODO
- **Priority**: Medium (P1 - Protocol Correctness)
- **Owner**: developer
- **Depends on**: none
- **Scope**: `src/main.cpp` (`runSender`, `runReceiver`)
- **Acceptance Criteria**:
  - [ ] Convert `nameLen`, `fileSize`, `chunkSize` to network byte order (`htons`/`htonl`/64-bit endian swap) before transmission.
  - [ ] Convert from network byte order (`ntohs`/`ntohl`) upon receipt.
- **Verification**:
  - Loopback CLI tests (`make run`, `receive` + `send`) pass with zero regressions.
- **Result**: Not started.

### TASK-006: Asynchronous Network Session & Non-Blocking Timeouts
- **Status**: TODO
- **Priority**: Low (P2 - Performance & Scaling)
- **Owner**: developer
- **Depends on**: TASK-004, TASK-005
- **Scope**: `include/landrop/core/NetworkSession.hpp`, `src/core/NetworkSession.cpp`
- **Acceptance Criteria**:
  - [ ] Configure `SO_RCVTIMEO` and `SO_SNDTIMEO` socket options.
  - [ ] Provide non-blocking error detection and graceful disconnection handling.
- **Verification**:
  - Unit tests and loopback transfers pass.
- **Result**: Not started.

---

## Blockers
None currently.

## Last Verification
- **Date**: 2026-10-21
- **Tests**: `make test` passed (13/13 suites, 97/97 assertions).
- **Demo**: `make run` passed (TCP loopback transfer verified via SHA-256).

