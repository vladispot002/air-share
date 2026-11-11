# Disclosure: Artificial Intelligence in the Software Development Process

**Document Version:** 1.0.0
**Effective Date:** November 2026
**Status:** Authoritative Project Disclosure
**Target Audience:** Users, Contributors, Security Auditors, Academic Evaluators

---

## 1. Executive Summary & Purpose

This document provides a formal, transparent, and comprehensive disclosure of the use of Artificial Intelligence (AI) technologies during the development lifecycle of the **LAN-Drop** data transfer project.

The development of LAN-Drop incorporated modern AI-assisted software engineering practices, utilizing autonomous multi-agent orchestration, large language models (LLMs), and automated verification agents to assist human engineers in architecture planning, systems programming, test suite synthesis, security threat modeling, and release engineering.

The purpose of this disclosure is to:
1. Provide complete transparency regarding how AI was utilized as an auxiliary development instrument.
2. Establish a clear, unambiguous boundary between the **runtime software architecture** of LAN-Drop and the **development-time auxiliary toolchain**.
3. Detail the human-in-the-loop governance and rigorous automated verification processes that validate all generated or refactored code.
4. Ground every statement in verifiable repository evidence, separating documented artifacts from general workflow practices.

---

## 2. Fundamental Architectural Separation

A critical distinction must be made between the **technologies executing inside the LAN-Drop application** and the **tools utilized by developers during its engineering lifecycle**.

```
+-------------------------------------------------------------------------------------------------+
|                                LAN-DROP RUNTIME ARCHITECTURE                                    |
|                       (Executes on End-User Devices — 100% Deterministic)                       |
|                                                                                                 |
|   +-----------------------------------------------------------------------------------------+   |
|   |  Languages & Frameworks:                                                                |   |
|   |  - Core Systems & Transfer Engine: Modern C++20 (ISO/IEC 14882:2020)                    |   |
|   |  - Desktop Graphical Interfaces: Qt 6 Widgets (C++) and GTK+ 3.0 (C)                   |   |
|   |  - Web Client: TypeScript / Vite SPA (Standard ECMAScript & Web APIs)                   |   |
|   |  - Signaling Server: Go 1.22 (Standard Library WebSocket router)                        |   |
|   +-----------------------------------------------------------------------------------------+   |
|   |  Network & Cryptography Libraries:                                                      |   |
|   |  - Cryptographic Primitives: OpenSSL 3.0 (PBKDF2-HMAC-SHA256, TLS 1.3, SHA-256)        |   |
|   |  - Transport Protocols: POSIX TCP/UDP Sockets & libdatachannel (WebRTC DataChannels)    |   |
|   |  - NAT Traversal: Coturn TURN/STUN daemon (RFC 8445, RFC 5766)                          |   |
|   +-----------------------------------------------------------------------------------------+   |
|   |  CRITICAL NOTICE: ZERO AI / ML RUNTIME DEPENDENCIES                                     |   |
|   |  - No neural networks, inference runtimes, ONNX models, or PyTorch/TensorFlow engines.   |   |
|   |  - No remote AI API calls, telemetry collection, or cloud LLM communication at runtime.|   |
|   |  - Fully local, offline-capable, deterministic P2P and LAN socket communications.        |   |
|   +-----------------------------------------------------------------------------------------+   |
+-------------------------------------------------------------------------------------------------+
                                                ▲
                                                │ Engineered, verified, and audited by
                                                │ human engineers utilizing
                                                ▼
+-------------------------------------------------------------------------------------------------+
|                           DEVELOPMENT-TIME AUXILIARY AI TOOLCHAIN                               |
|                     (Used Exclusively During Engineering, Building, and QA)                     |
|                                                                                                 |
|   +------------------------------------+   +------------------------------------------------+   |
|   |  Autonomous Agent Framework:       |   |  Agent Roles (.agents/agents/):                |   |
|   |  - Google Antigravity Agent Engine |   |  - Orchestrator (workflow & gate management)   |   |
|   |  - AGENTS.md Task Orchestrator     |   |  - Architect (ADR design & protocol specs)     |   |
|   |  - Gemini Assistants (model: flash)|   |  - Developer (C++20, Go, TypeScript coding)    |   |
|   +------------------------------------+   |  - Reviewer (security & memory safety audit)   |   |
|                                            |  - Tester (31 test suites, 1065-1087 asserts)  |   |
|                                            |  - UI Designer (Qt 6 layout & styling)         |   |
|                                            +------------------------------------------------+   |
+-------------------------------------------------------------------------------------------------+
```

### 2.1. The LAN-Drop Application Contains Zero AI Runtime Logic
- **No AI runtime components**: The distributed binaries (`lan_drop`, `lan_drop_gtk`, `lan_drop_signaling`, container images, installers) contain no machine learning models, weights, or generative components.
- **Pure systems networking**: All transfer logic, discovery mechanisms, protocol framing, encryption, and throttling operate through standard algorithmic, mathematical, and cryptographic logic (POSIX sockets, OpenSSL 3, libdatachannel).
- **Privacy & Telemetry**: The application communicates solely with user-specified LAN peers, signaling nodes, or TURN servers; it has zero capabilities or code paths to transmit data to AI providers.

---

## 3. Confirmed AI Tools, Models, and Agent Framework

During the design, implementation, remediation, and release phases of LAN-Drop, specific auxiliary AI software engineering systems were employed:

| Component | Identifier / Source | Evidence Status | Role in Development |
|---|---|---|---|
| **Primary Agentic Environment** | **Google Antigravity** (Advanced Agentic Coding Environment) | **VERIFIED** | Provided autonomous multi-agent tool execution, shell interaction, persistent memory, and workspace orchestration. |
| **Foundation LLMs** | **Google Gemini Family** (configured as `model: flash` in `.agents/agents/*.md`) | **VERIFIED** | Served as underlying reasoning engines for code generation, semantic analysis, error triage, and documentation authoring. |
| **Agent Role Hierarchy** | Defined in `.agents/agents/` and root `AGENTS.md` | **VERIFIED** | Structured role specialization partitioning engineering tasks across specialized personas. |
| **Skill Framework** | Defined in `.agents/skills/` | **VERIFIED** | Standardized operating procedures for project planning, atomic implementation, and independent verification. |

> [!NOTE]
> *Evidence Boundary:* While developers may interact with external general-purpose LLMs during informal development, only Google Antigravity and the Gemini model family (configured as `model: flash` in `.agents/agents/*.md`) are verified and confirmed by in-repository configuration and execution logs. Unsupported models or unverified version numbers are strictly excluded from formal claims.

### 3.1. Multi-Agent Organization
As documented in the repository's `.agents/agents/` configuration, AI assistance was structured into specialized operational roles:

1. **Orchestrator (`.agents/agents/orchestrator.md`)**:
   - Manages task decomposition, dependency tracking, release gates, and inter-agent coordination.
   - Enforces strict safety rules (e.g., prohibition of unverified claims, safe handling of credentials, preservation of existing functionality).
2. **Architect (`.agents/agents/architect.md`)**:
   - Researches codebase structure and drafts Architectural Decision Records (ADRs).
   - Formulates protocol specifications (such as Wire Protocol v2 in `docs/PROTOCOL_V2.md`).
3. **Developer (`.agents/agents/developer.md`)**:
   - Implements bounded C++20, Go, and TypeScript components.
   - Strictly adheres to the project's **Zero-Comments Policy** and standard C++20 compiler flags (`-std=c++20 -Wall -Wextra -Wpedantic`).
4. **Reviewer (`.agents/agents/reviewer.md`)**:
   - Conducts independent diff audits for memory leaks, iterator invalidation, thread safety, and security vulnerabilities (e.g., path traversal, symlink attacks, timing leaks).
5. **Tester (`.agents/agents/tester.md`)**:
   - Formulates automated test cases, reproduces defects, and verifies assertion integrity across unit, GUI, and end-to-end suites.
6. **UI Designer (`.agents/agents/ui-designer.md`)**:
   - Refines Qt 6 Widgets layouts, responsive styling, and presentation views while strictly preserving underlying `TransferEngine` logic.

---

## 4. Scope of AI Involvement Across the Engineering Lifecycle

AI tools assisted human engineers across five key disciplines:

### 4.1. Architecture & Protocol Specification
- **Architectural Decision Records**: Assisted in drafting and structuring ADRs in `docs/DECISIONS.md` (e.g., ADR-001 through ADR-011), documenting technical context, trade-offs, and implementation decisions.
- **Wire Protocol v2 Specification**: Helped draft the binary frame layout (`LNDP` magic header, 16-byte fixed header, CRC-32 integrity, chunk offsets) documented in `docs/PROTOCOL_V2.md`.
- **MVP Presentation Architecture**: Formulated the clean separation between business logic and UI views (`IDiscoveryView`, `ITransferView`, `IStorageView`, `ISettingsView`), enabling 100% headless testing.

### 4.2. Systems Programming & Implementation
- **C++20 Implementation**: Drafted and refactored core engine components, including `TransferEngine`, `ChunkBuffer<T>`, `FileChunkHasher`, `TlsContext`, and `MulticastBeacon`.
- **Memory Safety & Modern Idioms**: Ensured strict compliance with RAII, smart pointer ownership (`std::unique_ptr`, `std::shared_ptr`), `std::span` buffer views, and zero raw owning pointers.
- **Go Signaling Service**: Assisted in drafting the ephemeral WebSocket signaling router (`backend/cmd/signaling/`) with in-memory room management and Crockford Base32 room codes.
- **WebRTC DataChannel Bridge**: Implemented abstraction wrappers over `libdatachannel` for browser-to-desktop and peer-to-peer file streaming.

### 4.3. Test Suite Design & Quality Assurance
- **Comprehensive Unit Testing**: Generated and maintained 31 test suites in `tests/unit_tests.cpp`. When executed, the suite evaluates **1,065 dynamic assertions** in default offline mode (verified via `make clean && make test`) and up to **1,087 dynamic assertions** when built with optional WebRTC support via `libdatachannel` (as recorded in release audit logs), verifying edge cases such as zero-byte transfers, interrupted downloads, disk space saturation, and path traversal attempts.
- **Qt GUI Headless Testing**: Assisted in designing `tests/test_qt_gui.cpp` comprising **15 test case slots** (17 total execution steps including fixture setup and teardown) executed via `QTest` to verify widget state transitions without requiring an active desktop display server.
- **Loopback End-to-End Testing**: Implemented simulated loopback modes in WebRTC session coordinators, enabling deterministic headless P2P testing without external STUN/TURN servers.

### 4.4. Security Hardening & Vulnerability Remediation
- **Cryptographic Storage Protection**: Guided the implementation of OpenSSL PBKDF2-HMAC-SHA256 with 100,000 iterations, 16-byte cryptographic salt, and constant-time `CRYPTO_memcmp` to eliminate timing side channels.
- **File System Guardrails**: Synthesized path canonicalization routines to block path traversal (`../`) and verify that symlink targets do not escape the designated shared folder.
- **Atomic Transfer Staging**: Designed the temporary file staging pipeline (`*.part`), ensuring incomplete transfers are scrubbed on disconnect and only promoted to the target filename upon verified full-file SHA-256 match.

### 4.5. Release Engineering, Packaging & CI/CD Diagnostics
- **Cross-Platform Packaging**: Assisted in writing and debugging packaging specifications:
  - Debian package generation via CPack (`CPACK_DEBIAN_PACKAGE_SHLIBDEPS`).
  - Fedora RPM spec and packaging scripts (`scripts/package-fedora.sh`).
  - Linux AppImage bundling (`scripts/package-appimage.sh`).
  - Windows x64 Inno Setup compilation (`scripts/package-windows.ps1`).
  - Production multi-stage Dockerfile (`Dockerfile`).
- **CI/CD Root Cause Analysis**: Diagnosed complex CI failures across GitHub Actions workflows (e.g., resolving Debian `dpkg-shlibdeps` library discovery, Windows MSVC POSIX process signal incompatibility, and progress callback throttle edge cases in high-speed loopback tests).

---

## 5. Human Oversight, Governance, and Verification Principles

LAN-Drop adheres to a strict **Human-in-the-Loop Governance** model. Artificial Intelligence was at no point granted unsupervised authority over production code, cryptographic keys, release publication, or repository integration.

### 5.1. The Verification Principle
> **Fundamental Principle:**
> *AI recommendations and drafts never replace actual compilation, automated test execution, security review, or physical validation.*

Every code change, configuration, or documentation update generated with AI assistance was subject to the following validation gates before adoption:

1. **Strict Compiler Standards**:
   - Every C++ line must compile cleanly under GCC/Clang with `-std=c++20 -Wall -Wextra -Wpedantic`.
   - **Zero-Warning Tolerance**: Compilation must produce 0 compiler warnings.
2. **Automated Regression Testing**:
   - Full test target execution via `ctest --test-dir build` (`ComprehensiveTests` and `QtGuiTests`) and `make clean && make test` (31 unit test suites).
   - **Non-Decreasing Assertion Rule**: As codified in `.agents/rules/engineering.md`, no commit or modification is accepted if the total number of passing assertions decreases.
3. **Static Analysis & Sanitizer Verification**:
   - Memory checks via AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan) during audit phases.
4. **Human Review & Final Authority**:
   - The repository owner and lead human engineer retained exclusive control over:
     - Merging branches into `main`.
     - Creating and signing Git release tags (`v1.0.1`, `v1.0.2`).
     - Managing GitHub authentication, secrets, and API credentials.
     - Authorizing and executing public distribution to GitHub Releases and GitHub Container Registry (GHCR).

---

## 6. Verifiable Repository Evidence vs. General Workflow Descriptions

To maintain academic and professional rigor, this section distinguishes between **concrete, verifiable artifacts present in this repository** and **general workflow descriptions**, applying a strict three-tier evidence taxonomy:

- **VERIFIED**: Directly confirmed through repository inspection, source code, reproducible test execution logs, or environment configuration.
- **REPORTED**: Supported by previous development and audit logs but dependent on external conditions (e.g., WAN Coturn infrastructure, WebRTC compilation flags).
- **NOT VERIFIED**: Hypothesized or general practices lacking independent repository-grounded proof.

### 6.1. Verifiable In-Repository Evidence
The following items are permanently preserved in the repository and provide direct, reproducible evidence of the AI-assisted development workflow:

| Evidence Artifact | Repository Location | Evidence Status | Verifiable Fact |
|---|---|---|---|
| **Orchestrator Rules & Safety** | [`AGENTS.md`](../AGENTS.md) | **VERIFIED** | Establishes the AI orchestrator role, multi-agent workflow, and explicit safety boundaries. |
| **Agent Role Specifications** | [`.agents/agents/`](../.agents/agents/) | **VERIFIED** | Six distinct specialized agent role definitions (`orchestrator`, `architect`, `developer`, `reviewer`, `tester`, `ui-designer`), with `model: flash` subagent tiers. |
| **Engineering Directives** | [`.agents/rules/engineering.md`](../.agents/rules/engineering.md) | **VERIFIED** | Enforces Zero-Comments Policy, `-std=c++20 -Wall -Wextra -Wpedantic`, RAII, and test regression bounds. |
| **Skill Standard Operating Procedures** | [`.agents/skills/`](../.agents/skills/) | **VERIFIED** | Workflows for `project-planning`, atomic `implementation`, and independent `verification`. |
| **Architectural Decision Records** | [`docs/DECISIONS.md`](DECISIONS.md) | **VERIFIED** | ADR-001 through ADR-011 documenting collaborative architectural decisions and trade-offs. |
| **Verification & Audit Reports** | [`docs/audit/`](audit/) and [`docs/RELEASE_PACKAGING_VERIFICATION_v1.0.2.md`](RELEASE_PACKAGING_VERIFICATION_v1.0.2.md) | **VERIFIED** | Detailed pre-release audit logs, packaging verification, test execution tallies, and threat models. |
| **31 Test Suites (1,065–1,087 Assertions)** | [`tests/unit_tests.cpp`](../tests/unit_tests.cpp) | **VERIFIED** (1,065 offline) / **REPORTED** (1,087 WebRTC) | Test suites evaluating boundary conditions, edge cases, and failure scenarios (1,065 assertions passed via `make clean && make test`). |
| **Qt GUI Automated Tests** | [`tests/test_qt_gui.cpp`](../tests/test_qt_gui.cpp) | **VERIFIED** | 15 test case slots (17 total execution steps with fixture setup/teardown) passed via `lan_drop_gui_tests`. |

### 6.2. General Workflow Descriptions & Unverified Claims
Certain aspects of the collaborative workflow represent developer-side interactions that are inherently ephemeral or external to the Git tree:
- Interactive conversational prompts between the human engineer and AI models during brainstorming sessions (**REPORTED** / developer practice).
- Intermediate draft suggestions discarded during interactive refactoring prior to committing (**NOT VERIFIED** in persistent Git tree).
- Third-party models not configured in `.agents/` or commit records (**NOT VERIFIED**; excluded from verified tool claims).
- Local scratch script executions used for temporary diagnostic measurements during development.

---

## 7. Ethical, Academic, and Professional Responsibility Statement

The authors of LAN-Drop acknowledge that modern software engineering increasingly integrates AI assistance. By publishing this explicit disclosure:
1. **Intellectual Honesty**: We openly acknowledge the tools and methodologies used to construct the software, rejecting opacity or ambiguous attribution.
2. **Accountability**: The human author(s) accept full engineering responsibility for the correctness, security, performance, and legal compliance of the codebase. AI was an instrument, not an author.
3. **Reproducibility**: The preservation of `.agents/`, `AGENTS.md`, ADRs, and verification logs ensures that our methodology is fully auditable and reproducible by external researchers, developers, and security evaluators.

---
*For technical inquiries or questions regarding this disclosure, please open an issue on the [LAN-Drop GitHub Repository](https://github.com/vladispot002/air-share).*
