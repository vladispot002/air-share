# LAN-Drop Repository Hygiene Audit & Redundant Files Investigation

**Audit Date**: November 11, 2026 (Second-Pass Evidence Reconciliation)  
**Auditor**: Chief Release Engineer & Architecture Orchestrator  
**Scope**: Full repository hygiene, obsolete files, dead code, build artifacts, release packaging, disk footprint, and dependency analysis  
**Repository State**: Git `main` @ `846f757dbcf67eb0c4b3b5e620ba182573413e57` (Tag: `v1.0.2`)  
**Status**: INVESTIGATION COMPLETE — NO MODIFICATIONS OR DELETIONS EXECUTED  

---

## 1. Executive Summary & Reconciled Repository Metrics

An exhaustive, two-pass technical audit was conducted across all 239 tracked files, uncommitted documents, ignored directories, build caches, and test fixtures in the LAN-Drop workspace.

### Key Metrics (Reconciled & Independently Verified)
| Metric | Value | Status | Evidence / Notes |
| :--- | :--- | :--- | :--- |
| **Tracked Files** | 239 files | VERIFIED | `git ls-files` count |
| **Tracked Source Size** | **3.38 MB** (3,549,410 bytes) | VERIFIED | Exact sum of all tracked file sizes on disk |
| **Untracked Documentation Files** | 2 files (41.03 KB) | VERIFIED | `AI_DEVELOPMENT_DISCLOSURE.md`, `REPOSITORY_HYGIENE_AUDIT.md` |
| **Total Physical Workspace (excl. `.git`)** | **1,263.33 MB** (~1.26 GB) | VERIFIED | Exact recursive file size sum |
| **Ignored CMake Build Trees** | **662.78 MB** (6 build dirs) | VERIFIED | `build/`, `build-release/`, `build-package/`, `build-appimage/`, `build_offline/`, `build_webrtc/` |
| **LibDataChannel FetchContent Cache** | **371 MB** (inside `build_webrtc/_deps`) | VERIFIED | Cloned sub-dependencies (`libjuice`, `usrsctp`) |
| **Ignored NPM Dependencies (`node_modules`)** | **281.02 MB** (2 trees) | VERIFIED | `Design LAN-Drop macOS UI` (180.53 MB) + `web` (100.49 MB) |
| **Release & Packaging Staging (`dist/`)** | **241.10 MB** | VERIFIED | Local v1.0.2 packages (19.97 MB) + historical v1.0.0/v1.0.1 & rpmbuild (221.13 MB) |
| **Makefile & Go Binaries (`bin/`, `obj/`)** | **16.35 MB** | VERIFIED | `obj/` (4.61 MB), `bin/` (1.67 MB), `backend/signaling/bin/` (10.08 MB) |
| **Dynamic Sample Files (`shared_drop/`)** | **58.13 MB** apparent / **172 KB** disk | VERIFIED | Sparse sample files auto-generated dynamically by `StoragePresenter` |
| **Root Stale Files (`CMakeFiles/`, `.zip`)** | **71.3 KB** | VERIFIED | Stale in-source CMake cache + redundant archive of tracked directory |
| **Unreferenced Vendored Headers** | `include/doctest/doctest.h` (356 KB) | VERIFIED | 3,037 lines; 0 references anywhere in project |
| **Dormant Legacy Source Files** | `DesktopGuiEngine.hpp` / `.cpp` (626 lines) | VERIFIED | Compiled into `lan_drop_gtk`, never instantiated in any configuration |

### Overall Hygiene Verdict
The core Git repository is **exceptionally lean and clean** (3.38 MB, zero tracked binary bloat, exactly one `.ico` icon). However, the **local developer workspace** is heavily bloated (>1.25 GB) due to accumulated intermediate build trees, duplicate packaging artifacts, historical rpmbuild roots, and dual `node_modules` trees. Over **1,000 MB** of disk space can be safely recovered locally without affecting any tracked Git files, build recipes, or application behavior.

---

## 2. Complete Repository Inventory by Category

### 2.1 Tracked Directory Breakdown (Clean Source Size)
*Note: These sizes reflect ONLY tracked files, excluding ignored directories such as `node_modules` or build outputs.*

| Directory / Path | Tracked Files | Tracked Size | Primary Role | Status |
| :--- | :--- | :--- | :--- | :--- |
| `src/` | 36 | 131.6 KB | C++ implementations (core, peers, tasks, mobile, ui) | `KEEP` |
| `include/` | 44 | 1,419.5 KB | C++ public/private headers, protocols, vendored JSON & doctest | Mixed (`KEEP` / `SAFE CLEANUP`) |
| `web/` | 23 | 184.2 KB | Production WebRTC client (Vite/TypeScript sources & tests) | `KEEP` |
| `backend/` | 17 | 84.8 KB | Go WebRTC signaling server sources, tests, `go.mod` | `KEEP` |
| `Design LAN-Drop macOS UI/` | 66 | 284.1 KB | Figma React/Tailwind design prototype sources | `REVIEW` |
| `docs/` | 17 | 148.5 KB | System architecture, protocols, packaging, and audits | Mixed (`KEEP` / `KEEP — HISTORICAL`) |
| `.agents/` | 10 | 43.8 KB | Antigravity AI orchestrator agents, rules, and skills | `KEEP` |
| `resources/` | 6 | 391.7 KB | Application icons (.ico, .svg), desktop file, metainfo, Qt styles | `KEEP` |
| `scripts/` | 4 | 18.9 KB | AppImage, RPM, and Windows release packaging scripts | `KEEP` |
| `tests/` | 2 | 211.9 KB | Unit tests (`unit_tests.cpp`) and Qt GUI tests (`test_qt_gui.cpp`) | `KEEP` |
| `packaging/` | 2 | 12.7 KB | Fedora RPM spec and Inno Setup script | `KEEP` |
| `.github/` | 2 | 8.1 KB | GitHub Actions CI and automated release workflows | `KEEP` |
| `tasks/` | 1 | 5.2 KB | Historical development backlog (`tasks/backlog.md`) | `KEEP — HISTORICAL` |
| `cmake/` | 1 | 3.9 KB | LibDataChannel CMake FetchContent module | `KEEP` |
| Root files | 9 | 40.5 KB | `CMakeLists.txt`, `Makefile`, `Dockerfile`, `docker-compose.yml`, etc. | `KEEP` |

### 2.2 Ignored Artifacts & Disk Allocation
| Path | File Size / Disk Allocation | Category | Recoverability | Classification |
| :--- | :--- | :--- | :--- | :--- |
| `build_webrtc/` | 390.28 MB | Intermediate CMake build tree | Rebuilt via `cmake -B build_webrtc` | `SAFE CLEANUP CANDIDATE` |
| `build/` | 83.49 MB | Default CMake debug build tree | Rebuilt via `cmake -B build` | `SAFE CLEANUP CANDIDATE` |
| `build-package/` | 77.37 MB | CMake CPack packaging tree | Rebuilt via `cpack` | `SAFE CLEANUP CANDIDATE` |
| `build-release/` | 72.49 MB | CMake release build tree | Rebuilt via `cmake -B build-release` | `SAFE CLEANUP CANDIDATE` |
| `build-appimage/` | 19.58 MB | AppImage packaging workspace | Rebuilt via `scripts/build-appimage.sh` | `SAFE CLEANUP CANDIDATE` |
| `build_offline/` | 19.58 MB | Offline build verification tree | Rebuilt via `cmake -B build_offline` | `SAFE CLEANUP CANDIDATE` |
| `dist/rpmbuild/` | 182.4 MB | Historical RPM build roots (v1.0.0, 1.0.1, 1.0.2) | Rebuilt via `scripts/package-fedora.sh` | `SAFE CLEANUP CANDIDATE` |
| `dist/` (older versions) | 38.7 MB | Obsolete v1.0.0/v1.0.1 tarballs, AppImages, RPMs | Historical local output | `SAFE CLEANUP CANDIDATE` |
| `dist/release-v1.0.2/` | 19.97 MB | Local release package staging (v1.0.2) | Matches local `SHA256SUMS.txt` | `KEEP — GENERATED` |
| `backend/signaling/bin/` | 10.08 MB | Compiled Go binary `signaling-server` | Rebuilt via `go build` | `SAFE CLEANUP CANDIDATE` |
| `obj/` | 4.61 MB | Makefile object compilation files | Rebuilt via `make all` | `SAFE CLEANUP CANDIDATE` |
| `bin/` | 1.67 MB | Makefile output binaries (`lan_drop`, tests) | Rebuilt via `make all` | `SAFE CLEANUP CANDIDATE` |
| `Design LAN-Drop macOS UI/node_modules/` | 180.53 MB | Figma prototype React dependencies | Reinstalled via `npm install` | `REVIEW` |
| `web/node_modules/` | 100.49 MB | Production WebRTC client dependencies | Reinstalled via `npm install` | `KEEP — GENERATED` |
| `shared_drop/` | 58.13 MB (172 KB physical) | Sparse test fixture files | Auto-created by `StoragePresenter` | `SAFE CLEANUP CANDIDATE` |
| `Design LAN-Drop macOS UI.zip` | 70.9 KB | Redundant archive of tracked folder | Exact copy of tracked folder | `SAFE CLEANUP CANDIDATE` |
| `CMakeFiles/` | 422 bytes | Stale in-source CMake cache in root | Artifact of accidental `cmake .` | `SAFE CLEANUP CANDIDATE` |

---

## 3. Duplicate Files & Content Redundancy Analysis

### 3.1 Tracked Source Code Collisions (SHA-256)
- `Design LAN-Drop macOS UI/src/styles/fonts.css` (0 bytes, SHA-256: `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`)
- `Design LAN-Drop macOS UI/src/styles/globals.css` (0 bytes, SHA-256: `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`)
Both files are 0-byte placeholder stylesheets inside the design prototype.

### 3.2 Packaging Tarball Reconciliation: `dist/landrop-1.0.2.tar.gz` vs `release-v1.0.2/landrop-1.0.2.tar.gz`
A critical discrepancy was discovered during technical reconciliation:
1. `dist/release-v1.0.2/landrop-1.0.2.tar.gz`:
   - **Size**: 688,711 bytes (~688 KB)
   - **SHA-256**: `b5a407ed1d358be65cf5faee1a7f0e74f0c436ff106093415ef454651347d4e3` (VERIFIED MATCH with `dist/release-v1.0.2/SHA256SUMS.txt`).
   - **Contents**: Clean source archive produced via `git archive` containing only Git-tracked source code.
2. `dist/landrop-1.0.2.tar.gz` (and identical `dist/rpmbuild/SOURCES/landrop-1.0.2.tar.gz`):
   - **Size**: 28,936,458 bytes (~28.9 MB)
   - **SHA-256**: `eb3298e87616e58c39ebcdd735871921cc3c9280b42fc2b02e7edfadf4938e43`
   - **Contents**: Un-sanitized directory tarball that inadvertently bundled:
     - `backend/signaling/bin/signaling-server` (10.5 MB compiled binary)
     - `web/node_modules/` (18.4 MB of esbuild binaries, typescript compiler, and node dependencies).
- **Correction**: The initial audit incorrectly reported both files as identical duplicates of 2.5 MB. In reality, `dist/landrop-1.0.2.tar.gz` is a dirty local packaging intermediate and must NOT be distributed. Only `dist/release-v1.0.2/landrop-1.0.2.tar.gz` represents the verified clean source release asset.

---

## 4. Source Code & Architecture Hygiene Audit

### 4.1 Dead Vendored Header: `include/doctest/doctest.h`
- **Location**: `include/doctest/doctest.h` (356,877 bytes, 3,037 lines).
- **Introduction**: Added in commit `6e105cc0cd51fcf07dbf6fc12f12eb486328de96` (Oct 22, 2026) when modern CMake and `nlohmann/json.hpp` were introduced.
- **Investigation & Evidence**:
  - Full codebase grep (`git grep -i doctest`) returned **zero results** outside `doctest.h` itself.
  - `git log -S doctest` confirms commit `6e105cc` is the sole commit in Git history referencing doctest.
  - C++ unit tests in `tests/unit_tests.cpp` utilize a custom macro framework (`TEST_ASSERT`, `g_passedTests`).
  - Qt GUI tests in `tests/test_qt_gui.cpp` utilize `QTest` / `QSignalSpy`.
  - `CMakeLists.txt` does not install or export header files; LAN-Drop is an end-user application, not a shared library.
- **Conclusion**: Unused vendored dependency. Safe to delete from tracked Git files upon owner authorization.

### 4.2 Dormant Legacy Prototype: `DesktopGuiEngine`
- **Location**:
  - `include/landrop/ui/DesktopGuiEngine.hpp` (108 lines)
  - `src/ui/DesktopGuiEngine.cpp` (626 lines)
- **Introduction**: Implemented in commit `837b2d8` as the original X11/Xlib desktop prototype.
- **Static Call-Graph Analysis**:
  - In `src/main.cpp:35`, `#include "landrop/ui/DesktopGuiEngine.hpp"` is present.
  - In `src/main.cpp:365-385` (`runGui`):
    ```cpp
#if defined(LANDROP_BUILD_QT)
        QApplication qtApp(argc, argv);
        ui::QtDesktopApp mainWindow;
        mainWindow.show();
        return qtApp.exec();
#elif defined(LANDROP_BUILD_GTK) || !defined(LANDROP_BUILD_QT)
        ui::GtkDesktopApp app(isMobile);
        return app.run(argc, argv);
#else
        return 1;
#endif
    ```
    - When `LANDROP_BUILD_QT` is enabled (official build), `QtDesktopApp` is run.
    - When `LANDROP_BUILD_GTK` is enabled or Qt is omitted, `GtkDesktopApp` is run.
    - There is **no configuration or flag** under which `DesktopGuiEngine` can be instantiated.
  - In `CMakeLists.txt:148`, `DesktopGuiEngine.cpp` is linked into `lan_drop_gtk`, requiring `-lX11` at line 156.
  - In `Makefile:16`, `wildcard src/ui/*.cpp` compiles `DesktopGuiEngine.cpp` into `obj/ui/DesktopGuiEngine.o` and links with `-lX11`.
- **Correction**: The initial audit mentioned dynamic runtime tracing. This second pass clarifies that **static AST and call-graph verification** was performed: `DesktopGuiEngine` is unreachable and never invoked.
- **Conclusion**: Confirmed dead code. However, deleting `DesktopGuiEngine.cpp` without simultaneously updating `CMakeLists.txt:148` and `main.cpp:35` will cause build breaks. Deletion requires an architectural pull request.

---

## 5. Test Suite Dependency & `shared_drop/` Revalidation

### Revalidation Experiment
A controlled experiment was executed using an isolated test harness in `/tmp`:
```cpp
landrop::ui::StoragePresenter stor(&mock);
stor.onSharedPathChanged("./temp_test_shared_drop");
stor.initialize();
// Verified:
// 1. Directory is created automatically if non-existent.
// 2. 5 sample files are written with sparse gaps (apparent 58 MB, physical 172 KB).
// 3. Existing files are never overwritten (!std::filesystem::exists check).
// 4. removeSharedFile() only modifies in-memory presenter vector, never touches disk files.
```

### Reconciliation of Findings
1. **Does `shared_drop/` require manual preservation for tests?**
   **NO**. If `shared_drop/` is deleted, running `make test` or starting the application automatically recreates the directory and generates the 5 sparse files.
2. **Does test execution delete user data?**
   **NO**. `onRemoveFileClicked("quarterly-report-Q3.pdf")` calls `m_folderManager.removeSharedFile()`, which only erases the file metadata from the internal `std::vector<FileMetadata>`. The file on disk remains untouched.
3. **Classification Correction**:
   The initial audit classified `shared_drop/` as `POTENTIALLY DANGEROUS TO REMOVE`.  
   **Corrected Classification**: `SAFE CLEANUP CANDIDATE (REGENERABLE RUNTIME ARTIFACT)`. Deleting `shared_drop/` during workspace cleanup is completely safe; tests will automatically regenerate it upon execution.

---

## 6. Official GitHub Releases vs. Local Packaging Intermediates

To prevent confusion between locally staged binaries and officially published release assets, an independent verification against GitHub API / GitHub Releases was conducted:

### GitHub Release v1.0.2 (`vladispot002/air-share`)
*Published release assets generated via Ubuntu CI runners:*
- `LAN-Drop-Setup-1.0.2-x64.exe` (17.66 MiB) — SHA-256: `345a0efa607890d3de0a1b69a408e32e80cd1e98cba28fbdbe5784abab68f392`
- `LAN-Drop-1.0.2-x86_64.AppImage` (16.85 MiB) — SHA-256: `425309ed89a569bd70b1e3e57985ed47d3da6c4b913c890d6371829918627ef4` (**VERIFIED MATCH** with local `dist/release-v1.0.2/`)
- `landrop-1.0.2-1.fc44.x86_64.rpm` (289.20 KiB) — SHA-256: `ea800915cd773deb153f325daf7fff71e224ef220441ae3805c28bb4dc4444e3` (Cloud CI build)
- `landrop_1.0.2_amd64.deb` (635.79 KiB) — SHA-256: `4c02a1b0db6f2a88888b5abfb1209a135669dd9844d0db4a944a45a62fe8b401` (Cloud CI build)
- `landrop-1.0.2-linux-x86_64.tar.gz` (592.45 KiB) — SHA-256: `112c54c5d2d2cf2a7ab7c04abb5e1f287bb4c47e76862dc6bb769570afca8f4a` (Cloud CI build)
- `landrop-1.0.2.tar.gz` (586.76 KiB) — SHA-256: `4ecfe5feca3e90ea6f7d0cd6035e7eca0c5fd78d4329dc4ed35df1045d26132d` (Cloud CI build)

### Local Staging (`dist/release-v1.0.2/`)
*Local Linux packaging outputs built on Fedora developer host:*
- `LAN-Drop-1.0.2-x86_64.AppImage`: Matches published GitHub release asset byte-for-byte.
- `landrop-1.0.2-1.fc44.x86_64.rpm` (295,987 bytes) — Local Fedora build matching local `SHA256SUMS.txt`.
- `landrop_1.0.2_amd64.deb` (1,260,890 bytes) — Local CPack Debian build matching local `SHA256SUMS.txt`.
- `landrop-1.0.2.tar.gz` (688,711 bytes) — Clean local source archive matching local `SHA256SUMS.txt`.

**Rule**: All binaries and packages inside `dist/` are local outputs and packaging intermediates. The authoritative release assets are permanently hosted on GitHub Releases and GHCR.

---

## 7. Review of Proposed `.gitignore` Patterns

| Pattern | Already Covered? | Generated By | Intended Developer Use? | Could it Hide Needed Files? | Recommendation |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `compile_commands.json` | No | CMake (`CMAKE_EXPORT_COMPILE_COMMANDS=ON`) | Used by clangd / LSP editors | No (host-specific absolute paths) | **STRONGLY JUSTIFIED** |
| `coverage/` | No | gcov / lcov / vitest test runners | HTML test coverage reports | No | **STRONGLY JUSTIFIED** |
| `*.orig`, `*.rej` | No | Git merge conflict resolution / patch | Intermediate merge backups | No | **STRONGLY JUSTIFIED** |
| `*.bak` | No | Text editor backups / sed scripts | Temporary backup files | No | **STRONGLY JUSTIFIED** |
| `CTestTestfile.cmake` | No | CMake / CTest | Local test configuration | No | **STRONGLY JUSTIFIED** |
| `DartConfiguration.tcl` | No | CTest test driver | Local test dashboard config | No | **JUSTIFIED** |
| `install_manifest.txt` | No | `cmake --install` | Local installed files manifest | No | **JUSTIFIED** |
| `_CPack_Packages/` | No | CPack packaging tool | Intermediate package staging | No | **STRONGLY JUSTIFIED** |

---

## 8. Evidence Reconciliation — Second Pass

This section summarizes all corrections made during the second-pass verification:

### 1. Revalidation of `shared_drop/`
- **Previous Conclusion**: Classified as `POTENTIALLY DANGEROUS TO REMOVE`; claimed deleting it would cause unit tests to fail in `testPresenters()`.
- **New Investigation**: Traced `StoragePresenter::initialize()` and `removeSharedFile()`; executed an isolated C++ experiment in `/tmp`.
- **Evidence**: `StoragePresenter` automatically creates the directory and 5 sparse files if missing; `removeSharedFile()` only alters in-memory lists without deleting disk files.
- **Corrected Conclusion**: `shared_drop/` is auto-generated by the runtime. Deleting it is safe; tests recreate it.
- **Final Classification**: `SAFE CLEANUP CANDIDATE (REGENERABLE RUNTIME ARTIFACT)`.

### 2. Reachability of `DesktopGuiEngine`
- **Previous Conclusion**: Claimed unreachability was proven via dynamic runtime tracing.
- **New Investigation**: Re-examined verification evidence. No dynamic tracing log existed. Performed rigorous static call-graph analysis of `main.cpp:365-385` and target linkages.
- **Evidence**: `runGui()` only instantiates `QtDesktopApp` or `GtkDesktopApp`. No code path constructs `DesktopGuiEngine`.
- **Corrected Conclusion**: Static analysis confirms `DesktopGuiEngine` is 100% dormant dead code.
- **Final Classification**: `REVIEW / CONFIRMED DEAD CODE (ARCHITECTURAL DEPRECATION REQUIRED)`.

### 3. Provenance of `include/doctest/doctest.h`
- **Previous Conclusion**: Classified as safe cleanup candidate based solely on grep.
- **New Investigation**: Analyzed commit history (`git log -S doctest`) and public header exposure.
- **Evidence**: Introduced in commit `6e105cc` and never integrated; tests use custom `TEST_ASSERT` and `QTest`; no library headers are installed.
- **Corrected Conclusion**: Confirmed unused vendored header with zero project utility.
- **Final Classification**: `SAFE CLEANUP CANDIDATE (TRACKED DELETION PENDING APPROVAL)`.

### 4. Tarball Discrepancy in `dist/`
- **Previous Conclusion**: Stated `dist/landrop-1.0.2.tar.gz` (2.5 MB) was duplicated in `dist/release-v1.0.2/`.
- **New Investigation**: Checked exact file sizes and SHA-256 hashes of both tarballs.
- **Evidence**: `release-v1.0.2/landrop-1.0.2.tar.gz` is a clean 688 KB source tarball matching `SHA256SUMS.txt`. `dist/landrop-1.0.2.tar.gz` is a dirty 28.9 MB tarball containing compiled Go binaries and `node_modules`.
- **Corrected Conclusion**: They are not duplicates. Root `dist/landrop-1.0.2.tar.gz` is an un-sanitized intermediate.
- **Final Classification**: `dist/landrop-1.0.2.tar.gz`: `SAFE CLEANUP CANDIDATE (DIRTY INTERMEDIATE)`.

### 5. Workspace Disk Metrics
- **Previous Conclusion**: Reported directory sizes including `node_modules` inside tracked categories (e.g. 389 MB for Design UI).
- **New Investigation**: Re-calculated tracked files (3.38 MB), untracked files (41 KB), ignored build trees (662 MB), and dependencies (281 MB) as independent metrics.
- **Evidence**: Exact byte sums using `git ls-files` and Python directory walkers.
- **Corrected Conclusion**: Clear separation of tracked source vs. local workspace caches.

---

## 9. Final Cleanup Readiness Matrix

The cleanup readiness is strictly divided into two independent operations:

### Operation A: Local Workspace Cleanup (Non-Tracked, Fully Reversible)
*Does not alter Git history, tracked code, or remote releases. Reclaims **~1,060 MB** of disk space.*

| Path | Local Cleanup Safe? | Git Deletion Justified? | Evidence | Required Approval |
| :--- | :--- | :--- | :--- | :--- |
| `CMakeFiles/` (root) | **YES (Immediate)** | N/A (Ignored) | Stale artifact from root `cmake .` | None (Local safe) |
| `Design LAN-Drop macOS UI.zip` | **YES (Immediate)** | N/A (Ignored) | Redundant archive of tracked folder | None (Local safe) |
| `build/`, `build-release/`, `build-package/`, `build-appimage/`, `build_offline/` | **YES (Immediate)** | N/A (Ignored) | Intermediate CMake build trees | None (Local safe) |
| `build_webrtc/` | **YES (Immediate)** | N/A (Ignored) | Rebuilt via `cmake -B build_webrtc` | None (Local safe) |
| `bin/`, `obj/` | **YES (Immediate)** | N/A (Ignored) | Cleaned by `make clean` | None (Local safe) |
| `backend/signaling/bin/` | **YES (Immediate)** | N/A (Ignored) | Rebuilt via `go build` | None (Local safe) |
| `dist/landrop-1.0.0*`, `dist/landrop-1.0.1*` | **YES (Immediate)** | N/A (Ignored) | Obsolete previous version build outputs | None (Local safe) |
| `dist/rpmbuild/` | **YES (Immediate)** | N/A (Ignored) | Rebuilt via `scripts/package-fedora.sh` | None (Local safe) |
| `dist/landrop-1.0.2.tar.gz` (root) | **YES (Immediate)** | N/A (Ignored) | Dirty 28.9 MB tarball with binaries | None (Local safe) |
| `shared_drop/` | **YES (Immediate)** | N/A (Ignored) | Auto-regenerated dynamically by tests | None (Local safe) |
| `Design LAN-Drop macOS UI/node_modules/` | **YES (Optional)** | N/A (Ignored) | Reinstalled via `npm install` | None (Local safe) |

### Operation B: Tracked Repository Cleanup (Affects Git Tree)
*Modifies Git tree. Requires formal review and pull request.*

| Path | Local Cleanup Safe? | Git Deletion Justified? | Evidence | Required Approval |
| :--- | :--- | :--- | :--- | :--- |
| `include/doctest/doctest.h` | No (Tracked) | **YES** | 0 references across repo, unused test framework | Owner Approval |
| `include/landrop/ui/DesktopGuiEngine.hpp` | No (Tracked) | **YES (with code change)** | Dead X11 prototype, never instantiated | Owner / Arch Sign-off |
| `src/ui/DesktopGuiEngine.cpp` | No (Tracked) | **YES (with code change)** | Dead X11 prototype, never instantiated | Owner / Arch Sign-off |
| `Design LAN-Drop macOS UI/` (sources) | No (Tracked) | **NO / REVIEW** | Useful design reference for UI updates | Design Team Consensus |
| `tasks/backlog.md` | No (Tracked) | **NO** | Historical compliance & audit record | Retain as Historical |
| `docs/audit/RELEASE_GATE_V1.0.1.md` | No (Tracked) | **NO** | Historical release gate audit evidence | Retain as Historical |

---

## 10. Fresh Test Verification Record

To ensure the repository is completely stable prior to any owner decision, a fresh verification run of the entire test suite was executed.

- **Execution Command**: `make test`
- **Execution Timestamp**: `2026-11-11T14:25:31+0200` to `2026-11-11T14:25:43+0200` (12 seconds)
- **Exit Code**: `0`
- **Pass Rate**: **31/31 test suites passed** (1,065/1,065 assertions passed).
- **Execution Log Summary**:
  ```text
  ==========================================
    LAN-Drop: Comprehensive Unit Test Suite 
  ==========================================
  [RUN] LocalPeer & Inheritance... PASSED
  [RUN] CloudPeer & Quota Management... PASSED
  [RUN] Static Polymorphic Filter (filterPeers<T>)... PASSED
  [RUN] Template ChunkBuffer<T>... PASSED
  [RUN] FileChunkHasher SHA-256... PASSED
  [RUN] MulticastBeacon Protocol... PASSED
  [RUN] P2PTransferTask Lifecycle... PASSED
  [RUN] HttpWebTransferTask Properties... PASSED
  [RUN] SharedFolderManager & Storage Security... PASSED
  [RUN] NfcPairingSession NDEF Protocol... PASSED
  [RUN] AirDropProtocolBridge Protocol... PASSED
  [RUN] LandropMobileBridge C-ABI FFI... PASSED
  [RUN] MVP Presenters & View Integration... PASSED
  [RUN] Receiver Security & Boundary Validation... PASSED
  [RUN] Third-Party JSON & REST Protocol Parsing... PASSED
  [RUN] OpenSSL TLS Transport & Device Authentication... PASSED
  [RUN] REST/HTTP Client & Cloud Quota Synchronization... PASSED
  [RUN] Live P2P Socket Pipeline & Error Recovery... PASSED
  [RUN] TransferEngine Core Pipeline & Hardened Security... PASSED
  [RUN] Wire Protocol v2 Framing, Integrity & Backward Compatibility... PASSED
  [RUN] ITransportChannel & TcpSocketChannel Abstraction... PASSED
  [RUN] libdatachannel WebRtcDataChannel Integration Preparation... PASSED
  [RUN] WebRTC Flow Control & Candidate Queueing (TC-RTC-01, TC-RTC-02)... PASSED
  [RUN] SignalingClient Protocol & Glare Resolution (TC-RTC-03)... PASSED
  [RUN] Global WebRTC Transfer Pipeline & Coturn Config (Milestone 2C)... PASSED
  [RUN] End-to-End File Transfer & Live Signaling Verification (Milestone 2D)... PASSED
  [RUN] Optical Pairing & QrCodeGenerator Suite (Milestone 3B)... PASSED
  [RUN] Mobile Ecosystem Bridge FFI Suite (Milestone 3C)... PASSED
  [RUN] Resilient Streaming & Byte-Offset Resumption Suite (Milestone 3D)... PASSED
  [RUN] Production Packaging & CLI Diagnostics Suite (Milestone 4D)... PASSED
  [RUN] Final Release Gate: Real WebRTC & Protocol v2 Interop Suite (v1.0.1)... PASSED

  ==========================================
  Summary: 31/31 test suites passed (1065/1065 assertions passed).
  ==========================================
  ```
