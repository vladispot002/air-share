# LAN-Drop: Production Readiness Audit Report

**Date**: 2026-11-10  
**Audit Target**: Branch `audit/production-readiness` (Commit `bdfd076` on `main`)  
**Auditing Team**: Chief Software Architect, Lead QA Engineer, Lead Security Engineer, DevOps & Cross-Platform Engineer  
**Status**: **CONDITIONAL BLOCK (NOT PRODUCTION READY)**

---

## 1. Executive Summary & Verdict

LAN-Drop exhibits strong foundational engineering across many subsystems:
- **Transfer-First Principle**: Fully preserved. Direct local LAN transfers (TCP/TLS) are 100% serverless, independent of internet connectivity, cloud accounts, or third-party servers.
- **Signaling Daemon**: High-performance Go signaling service (<10 MB static binary, <20 MB Alpine container) with strict 64 KB read limits, Crockford Base32 rooms (`DROP-XXXX-XXXX`, 40-bit entropy), and glare prevention.
- **Diagnostic Tooling**: Zero-dependency C++20 RFC 5389 STUN client and system diagnostics (`lan_drop doctor`).
- **Core Security Primitives**: Constant-time PIN validation (`CRYPTO_memcmp`), zero-dependency ISO/IEC 18004 QR generation, and strict path traversal protection.

However, a rigorous code-level and cross-component audit has revealed **critical blockers, protocol disconnects, and operational defects** that prevent certifying production readiness:

```
+---------------------------------------------------------------------------------------+
|                              PRODUCTION READINESS VERDICT                             |
|                                                                                       |
|   [ CONDITIONAL BLOCK ] — The core system cannot be approved for general release      |
|   until 6 Critical/High defects are remediated and verified on live networks.         |
+---------------------------------------------------------------------------------------+
```

### Top Critical Blockers Summary
1. **P0 Protocol Disconnect (Desktop ↔ Web Incompatibility)**: `TransferEngine.cpp` exclusively communicates using Wire Protocol v1 (raw unversioned endian-dependent metadata), while the Web client (`FileStreamSender.ts` / `FileStreamReceiver.ts`) communicates using Wire Protocol v2 (16-byte `LNDP` binary framing). Cross-platform Desktop ↔ WebRTC file transfers **fail 100% of the time**.
2. **P0 Operational Outage (`TURN_SECRET` Mismatch)**: `docker-compose.yml` passes `COTURN_STATIC_AUTH_SECRET=${TURN_SECRET}`, but Go signaling daemon `main.go` queries `TURN_SECRET`. In production, the signaling server falls back to `"local-test-turn-secret"`, causing **100% TURN 401 Unauthorized failures** for NAT traversal.
3. **P1 Global Denial-of-Service (`TrustProxy` Blackout)**: `TrustProxy` defaults to `false` in `server.go`. Behind Caddy, `RemoteAddr` is Caddy's container IP (`172.18.0.2`). Five failed join attempts from any single client triggers a global rate-limit block, **blacklisting all users worldwide for 10 minutes**.
4. **P1 Mobile Bridge Deadlock**: `LandropMobileBridge.cpp` acquires `ctx->mutex` and calls `ctx->transferWorker.join()`. When the worker thread fires a progress callback, it attempts to acquire `ctx->mutex`, deadlocking the host application.
5. **P1 Mock WebRTC Verification Gap**: All C++ "E2E" WebRTC tests in Suites 25 and 26 run with `opts.mockLoopbackMode = true`, completely bypassing `libdatachannel`. Real WebRTC DataChannels have zero automated end-to-end transfer tests.
6. **P1 Web Browser Heap Exhaustion (OOM)**: `FileStreamReceiver.ts` buffers received chunks in an in-memory `Uint8Array[]` array instead of streaming to disk via the File System Access API. Transfers >1.5 GB crash the browser tab.

---

## 2. Capability Verification Status: Claimed vs. Actual Reality

The audit compared the codebase implementation against `docs/DECISIONS.md` (ADR-001 through ADR-018), `docs/SPEC.md`, and previous milestone reports.

| Subsystem / Capability | Reported Status | Actual Audit Finding | Status |
| :--- | :--- | :--- | :---: |
| **Local LAN Direct Transfer** | Completed | Clean, robust C++20 TCP/TLS pipeline. Fully serverless, offline, and zero-telemetry. | **VERIFIED** |
| **Local Subnet Discovery** | Completed | UDP multicast beacon (`239.255.255.250:52637`) announces and discovers peers without central servers. | **VERIFIED** |
| **Go Signaling Daemon** | Completed | Lightweight Go binary, in-memory rooms, Crockford Base32 normalization, write pump, rate limiting. | **VERIFIED** |
| **CLI Diagnostics (`doctor`)** | Completed | RFC 5389 STUN binding client, interface enumeration, and port availability inspection work cleanly without external network libraries. | **VERIFIED** |
| **Optical Pairing (QR)** | Completed | Clean ISO/IEC 18004 encoder in C++ and TypeScript with `#DROP-XXXX-XXXX` URI fragment. | **VERIFIED** |
| **Linux Packaging (.deb, .rpm)**| Completed | Debian CPack works. Fedora RPM spec fails `rpmbuild` due to missing metainfo XML in `%files`. | **PARTIAL** |
| **Linux AppImage** | Completed | `scripts/build-appimage.sh` builds 16.65 MB AppImage, but uses fragile `ldd` bundling and is omitted from CI release workflows. | **FRAGILE** |
| **Windows Packaging (.exe)** | Completed | Inno Setup script compiles on Windows MSVC. Lacks code signing (SmartScreen block), lacks firewall rules, and WebRTC is disabled in CI. | **PARTIAL** |
| **Desktop ↔ Web P2P Transfer**| Claimed Complete | **Completely broken**. Desktop `TransferEngine` does not use `WireProtocolV2`. Reading `LNDP` magic throws "Invalid filename length: 1346653772". | **FAILED** |
| **Byte-Offset Resumption (ADR-017)**| Claimed Complete | C++ `TransferEngine` has zero resumption logic and deletes `.part` files on startup. `.part.meta` is not implemented in C++ or TS. | **INCOMPLETE** |
| **Production Self-Hosting** | Claimed Complete | Dockerfile works (<20 MB), but `TURN_SECRET` mismatch breaks Coturn credentials and `TrustProxy=false` causes global blackouts. | **DEFECTIVE** |
| **Real WebRTC E2E Testing** | Claimed Complete | All C++ E2E transfer tests use `mockLoopbackMode = true`. Real `libdatachannel` data channel transfer is never tested in CI. | **SIMULATED** |
| **Mobile Ecosystem (Flutter/iOS/Android)**| Claimed Prepared | C-ABI exists in `LandropMobileBridge`, but has fatal worker thread deadlock, hardcoded fake peers in discovery, and a no-op send stub. | **STUB / BLOCKED** |
| **Apple AirDrop Interoperability**| Partially Complete| `AirDropProtocolBridge` is an in-memory string formatter with zero AWDL/BLE/Avahi connection. Real AirDrop is impossible in user-space POSIX. | **MOCK / UNREAL** |
| **macOS Native Desktop Support** | Claimed HIG UI | Zero macOS CI runners, no Xcode project, no `.app` bundle, no `.dmg`. "HIG UI" is a Linux GTK3 theme and a web mockup. | **UNSUPPORTED** |

---

## 3. Critical and High-Risk Audit Findings

### 3.1 Severity 1 (Critical Blockers)

#### [SEC-02 / DEF-OPS-01] Coturn TURN Credential Shared Secret Mismatch
- **Files**: [`backend/signaling/cmd/server/main.go:19`](file:///home/vladis/lan-drop/backend/signaling/cmd/server/main.go#L19), [`docker-compose.yml:16`](file:///home/vladis/lan-drop/docker-compose.yml#L16)
- **Defect**: In `docker-compose.yml`, the environment variable passed to the signaling container is `COTURN_STATIC_AUTH_SECRET=${TURN_SECRET}`. However, `main.go` reads `os.Getenv("TURN_SECRET")`. Because `TURN_SECRET` is unset inside the container, the Go server falls back to `"local-test-turn-secret"`.
- **Impact**: All TURN credentials minted by `/api/v1/turn-config` are signed with the fallback secret and rejected by Coturn with HTTP/STUN 401. WebRTC relay fallback is completely non-functional in production NAT environments.

#### [DEF-ENG-01 / DEF-QA-01] WireProtocolV2 Disconnect Between Desktop and Web
- **Files**: [`src/core/TransferEngine.cpp:193-204, 508-546`](file:///home/vladis/lan-drop/src/core/TransferEngine.cpp#L193-L204), [`src/core/WireProtocolV2.cpp`](file:///home/vladis/lan-drop/src/core/WireProtocolV2.cpp), [`web/src/core/streaming/FileStreamSender.ts:63`](file:///home/vladis/lan-drop/web/src/core/streaming/FileStreamSender.ts#L63)
- **Defect**: `TransferEngine` still uses legacy Wire Protocol v1:
  ```cpp
  // TransferEngine.cpp:511
  uint32_t nameLen = 0;
  channel->receiveExact(&nameLen, sizeof(uint32_t));
  ```
  The Web client sends a WireProtocolV2 frame:
  `0x4C 0x4E 0x44 0x50` (`LNDP`), `0x0002` (Version), `0x0002` (Opcode FileHeader).
  On Little-Endian systems, `0x50444E4C` is read as `nameLen = 1,346,653,772`. Desktop immediately rejects the transfer.
- **Impact**: Zero interoperability between Desktop and Web clients.

---

### 3.2 Severity 2 (High Risk / Operational Hazards)

#### [SEC-03 / DEF-OPS-02] Global Server Blackout via Unconfigured `TrustProxy`
- **Files**: [`backend/signaling/cmd/server/main.go:39-49`](file:///home/vladis/lan-drop/backend/signaling/cmd/server/main.go#L39-L49), [`backend/signaling/internal/server/server.go:581`](file:///home/vladis/lan-drop/backend/signaling/internal/server/server.go#L581)
- **Defect**: `TrustProxy` is hardcoded to `false`. When deployed behind Caddy reverse proxy, `r.RemoteAddr` resolves to Caddy's bridge IP (`172.18.0.2`).
- **Impact**: If one client triggers the rate limiter (5 failed room attempts in 10 minutes), Caddy's IP is blocked, preventing **all users globally** from joining rooms.

#### [DEF-MOB-01] Mutex Deadlock on Mobile Bridge Worker Thread
- **Files**: [`src/mobile/LandropMobileBridge.cpp:350-366, 395-407`](file:///home/vladis/lan-drop/src/mobile/LandropMobileBridge.cpp#L350-L366)
- **Defect**: `landrop_mobile_webrtc_send_file_async` acquires `ctx->mutex` and calls `ctx->transferWorker.join()`. When the worker thread invokes the progress callback, it also acquires `ctx->mutex`.
- **Impact**: Deadlock on the host application UI/worker thread.

#### [DEF-QA-02] Real WebRTC DataChannel Transfers Are Completely Untested
- **Files**: [`tests/unit_tests.cpp:2597, 2675, 2748, 2821, 2955`](file:///home/vladis/lan-drop/tests/unit_tests.cpp#L2597)
- **Defect**: All end-to-end test cases (TC-E2E-01 through TC-E2E-100MB) set `opts.mockLoopbackMode = true`. In this mode, `libdatachannel` is bypassed, and data is copied between in-memory `std::vector` instances.
- **Impact**: No automated test verifies that `libdatachannel` PeerConnection, SCTP stream framing, and backpressure actually function end-to-end.

#### [DEF-WEB-02] Web Client Out-of-Memory on Large Files
- **Files**: [`web/src/core/streaming/FileStreamReceiver.ts:116-231`](file:///home/vladis/lan-drop/web/src/core/streaming/FileStreamReceiver.ts#L116-L231)
- **Defect**: In-memory chunk array `chunks.push(chunk.data)` retains the entire transfer payload in the JavaScript heap.
- **Impact**: Browser tabs crash on files >1.5 GB.

---

## 4. Test Evidence & Coverage Limitations

```
+-------------------------------------------------------------------------------+
|                        TEST COVERAGE BY ACCREDITATION LEVEL                   |
|                                                                               |
|  [ Level 1: In-Memory Unit Tests ] ................. 100% GREEN (30/30 suites)|
|  [ Level 2: Localhost Loopback Integration ] ....... 100% GREEN (Go, TLS, TCP)|
|  [ Level 3: Automated Real WebRTC E2E ] ............   0% (Mocked via loopback)|
|  [ Level 4: Physical Cross-Network / WAN Tests ] ...   0% (Untested)           |
+-------------------------------------------------------------------------------+
```

### What is Confirmed:
1. `make test` runs 30 C++ test suites (1,043 assertions): passes cleanly in-memory.
2. `ctest --test-dir build` runs `ComprehensiveTests` and `QtGuiTests`: passes offscreen.
3. `go test -count=1 -race ./...` runs signaling server tests: passes with 0 race conditions.
4. `npm test` runs 57 Vitest tests: passes using `MockDataChannel`.
5. `./build/lan_drop doctor` validates localhost interfaces, port bindings, and STUN encoder.

### What is Unverified / Unproven:
1. **Real-world WebRTC DataChannel transfer**: No test transmits a file over real SCTP data channels between two distinct processes.
2. **Web-to-Desktop transfer**: Zero tests attempt interoperability between C++ and TypeScript clients.
3. **Cross-network WAN / NAT Traversal**: Never executed across physical cellular or symmetric NAT networks.
4. **Resumption after crash**: Never tested with actual process kill and restart.
5. **Physical Mobile Execution**: Never compiled or executed on Android ART or iOS Darwin.

---

## 5. Production Readiness Scorecard

| Dimension | Weight | Score (0-100) | Weighted | Status |
| :--- | :---: | :---: | :---: | :--- |
| **Core Architecture & C++20 Quality** | 20% | 82 | 16.4 | Strong base, but Qt coupling and sync server loop |
| **Transfer-First LAN Autonomy** | 20% | 98 | 19.6 | Outstanding; fully offline, zero-telemetry, private |
| **Global WebRTC & Signaling** | 20% | 60 | 12.0 | Go server solid; WebRTC mocked in tests, protocol desync |
| **Security & Threat Mitigation** | 15% | 68 | 10.2 | Strong crypto; lacking DTLS SAS peer auth, proxy DoS risk |
| **Cross-Platform Interoperability** | 15% | 35 | 5.25 | Web ↔ Desktop broken; macOS/Mobile are mocks/stubs |
| **Release & Packaging Maturity** | 10% | 55 | 5.5 | Docker works; RPM broken, AppImage fragile, Windows unsigned |
| **TOTAL READINESS SCORE** | **100%** | — | **68.95 / 100** | **FAIL (Threshold: 85.0)** |

---

## 6. Required Remediation Tasks Before Production Sign-Off

To move LAN-Drop from **CONDITIONAL BLOCK** to **PRODUCTION APPROVED**, the following tasks must be completed in priority order:

1. **Integrate WireProtocolV2 into `TransferEngine`**: Update `sendFileOverChannel` and `receiveFileOverChannel` to serialize and parse 16-byte `LNDP` frames, unifying Desktop and Web.
2. **Fix `docker-compose.yml` Environment Variables**: Set `TURN_SECRET=${TURN_SECRET}` and `TRUST_PROXY=true` to eliminate the Coturn auth failure and Caddy global rate-limit lockout.
3. **Fix Mobile Bridge Deadlock**: Move worker thread handle out of `ctx->mutex` scope before calling `join()`.
4. **Implement Real WebRTC Integration Test**: Add a test in Suite 26 transferring a 10 MB file over real loopback `libdatachannel` PeerConnections with `mockLoopbackMode = false`.
5. **Implement Direct-to-Disk Streaming in Web Client**: Use the File System Access API or Origin Private File System (OPFS) to prevent browser heap exhaustion.
6. **Fix Fedora RPM Spec**: Add `%{_datadir}/metainfo/*.xml` to `%files` in `packaging/fedora/landrop.spec`.
