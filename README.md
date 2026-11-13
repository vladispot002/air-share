<div align="center">

# LAN-Drop

### Peer-to-Peer and Cross-Network File Transfer

[![Version](https://img.shields.io/badge/version-1.0.2-blue.svg)](https://github.com/vladispot002/air-share)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/standard-C%2B%2B20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Qt 6](https://img.shields.io/badge/GUI-Qt%206-green.svg)](https://www.qt.io/)
[![Go](https://img.shields.io/badge/signaling-Go%201.23-00ADD8.svg)](https://go.dev/)
[![TypeScript](https://img.shields.io/badge/web-TypeScript%205.8-blue.svg)](https://www.typescriptlang.org/)
[![WebRTC](https://img.shields.io/badge/transport-WebRTC%20DataChannel-orange.svg)](https://webrtc.org/)

LAN-Drop is an open-source, cross-platform file transfer tool for local area networks (LAN/Wi-Fi) and the public Internet. It connects desktop applications and web browsers directly, transferring data point-to-point without storing file contents on intermediate servers.

[Features](#key-features) • [Architecture](#architecture-overview) • [Installation](#installation) • [Building from Source](#building-from-source) • [Quick Start](#quick-start-and-usage) • [Configuration](#configuration) • [Testing](#testing) • [Troubleshooting](#troubleshooting) • [AI Disclosure](#engineering-methodology-and-ai-disclosure)

</div>

---

## Overview

LAN-Drop transfers files directly between devices:

1. **Direct Peer-to-Peer**: Data moves directly between sender and receiver over local TCP/TLS sockets or WebRTC DataChannels across the Internet.
2. **Zero Cloud File Storage**: The signaling server and database handle room coordination and ICE candidate exchange only; file payloads never pass through or get saved on the server.
3. **No Mandatory Account**: Transfers can be initiated immediately using room codes or local network discovery. Optional accounts provide persistent device pairing and transfer history.
4. **Wire Protocol v2**: Uses a fixed 16-byte binary header with CRC-32 transport checks, whole-file SHA-256 verification, and byte-offset resumption for interrupted transfers.

---

## Key Features

### Implemented (v1.0.2)

- **Desktop Interface (Qt 6 & GTK3)**:
  - Qt 6 GUI (`lan_drop`): Main desktop interface with subnet radar discovery, transfer queue with progress tracking, PIN-protected shared storage, and light/dark theme support.
  - GTK3 GUI (`lan_drop_gtk`): Alternative desktop and mobile (390x812) layout.
- **Web Client (TypeScript & React)**:
  - Browser client running in modern desktop and mobile browsers (Chrome, Firefox, Safari, Edge).
  - WebRTC DataChannel transfer with backpressure flow control.
  - HTML5 camera QR code scanner for room pairing.
- **Transports**:
  - Local LAN: Direct TCP/TLS sockets with configurable chunking (64 KB – 4 MB).
  - Cross-Network: WebRTC DataChannels using STUN for NAT discovery and automated TURN relay fallback for symmetric NATs.
- **Pairing**:
  - In-tree QR code generator (ISO/IEC 18004) in C++ and TypeScript without external library dependencies.
  - 8-character Crockford Base32 room codes (`DROP-XXXX-XXXX`) with character normalization (`O` -> `0`, `I`/`L` -> `1`).
- **Data Integrity & Security**:
  - Wire Protocol v2 binary framing (`LNDP` magic, CRC-32 per frame).
  - Inbound files stream into temporary `.part` files; atomic rename occurs only after complete SHA-256 verification.
  - Byte-offset resumption (`ResumeRequest` opcode `0x0008`) for interrupted transfers.
  - Path traversal checks (`isSafeFileName`), symlink boundary validation, and disk space limits.
  - PBKDF2-HMAC-SHA256 (100,000 iterations) for folder protection PINs.
- **Signaling Backend (Go 1.23)**:
  - WebSocket signaling with ephemeral room management.
  - Optional PostgreSQL 16 persistence for user accounts and paired devices.
  - Token-bucket rate limiting to mitigate connection abuse.
- **Diagnostics (`lan_drop doctor`)**:
  - CLI diagnostic command verifying network interfaces, port bindings, TLS support, signaling reachability, and STUN connectivity.

### Developer Preview / Simulated

- **AirDrop Protocol Emulation (`AirDropProtocolBridge`)**: Synthesizes Bonjour and HTTP `/Discover` and `/Ask` payloads in memory; does not interface with native AWDL or Bluetooth hardware.
- **NFC Quick-Pair Simulation (`NfcPairingSession`)**: Generates and parses `LANDROP:NFC:v1` NDEF text records in memory; does not access physical NFC controller hardware.

### Roadmap

- **Native Mobile Apps**: Dedicated Flutter/iOS/Android applications using the C-ABI bridge (`LandropMobileBridge`).
- **Web OPFS Storage**: Direct-to-disk streaming via the Origin Private File System API for multi-gigabyte transfers in browser tabs.

---

## Architecture Overview

LAN-Drop separates the signaling control plane from the peer-to-peer data plane:

```mermaid
graph TD
    subgraph Clients["LAN-Drop Clients"]
        Desktop["Desktop Application (C++20 / Qt 6)<br/>TransferEngine<br/>WebRtcSessionCoordinator<br/>IdentityClient"]
        Web["Web Client (TypeScript / Vite)<br/>WebRtcCoordinator<br/>FileStreamSender / Receiver<br/>Camera QR Scanner"]
    end

    subgraph Infrastructure["Production Cloud Infrastructure (Oracle Cloud VPS)"]
        Caddy["Caddy Reverse Proxy<br/>TLS (Let's Encrypt)<br/>HTTPS / WSS :443<br/>Static Asset Serving"]
        Signaling["Go 1.23 Signaling Service (:8080)<br/>Base32 Rooms<br/>Rate Limiting (Token Bucket)<br/>REST Auth & TurnConfig API"]
        Postgres["PostgreSQL 16 Database<br/>User Accounts (bcrypt)<br/>Linked Devices<br/>Transfer Metadata"]
        Coturn["Coturn TURN/STUN Server<br/>STUN :3478 UDP/TCP<br/>TURN Relay :49160-49200 UDP<br/>Ephemeral HMAC Auth"]
    end

    %% Signaling Connections
    Desktop -.->|"WSS Signaling & Auth"| Caddy
    Web -.->|"WSS Signaling & Auth"| Caddy
    Caddy -->|"/ws/signaling & /api/*"| Signaling
    Signaling -->|"TCP :5432"| Postgres

    %% Direct Peer-to-Peer Data Paths
    Desktop ===|"Direct LAN (TCP/TLS :8848)<br/>No server involved"| Desktop
    Desktop ===|"Direct Internet P2P<br/>(WebRTC DataChannel / DTLS 1.3)"| Web
    Web ===|"Direct Web-to-Web P2P<br/>(WebRTC DataChannel / DTLS 1.3)"| Web
    Desktop ===|"Direct Desktop-to-Desktop P2P<br/>(WebRTC DataChannel / DTLS 1.3)"| Desktop

    %% TURN Relay Fallback
    Desktop -.-|"Symmetric NAT Relay"| Coturn
    Web -.-|"Symmetric NAT Relay"| Coturn
```

### Communication Paths

- **Control Path (Signaling)**: WebSocket connections used to establish rooms, exchange SDP offers/answers, and transmit ICE candidates. Once the data channel opens, signaling is no longer used for that transfer.
- **Data Path (P2P)**: Direct data transfer over DTLS 1.3/SCTP (WebRTC) or TLS 1.3/TCP (local network). The signaling server does not handle file content.

---

## Installation

### 1. Fedora Linux (RPM)

Tested on Fedora 40, 41, and 44:

```bash
# Install release RPM (DNF resolves Qt 6 and OpenSSL dependencies)
sudo dnf install ./dist/rpm/landrop-1.0.2-1.fc44.x86_64.rpm
```

Installed files:
- Binary: `/usr/bin/lan_drop`
- Desktop entry: `/usr/share/applications/lan-drop.desktop`
- Icon: `/usr/share/icons/hicolor/scalable/apps/lan-drop.svg`
- Theme stylesheet: `/usr/share/landrop/styles.qss`
- AppStream metadata: `/usr/share/metainfo/io.landrop.LAN-Drop.metainfo.xml`

Run the application:
```bash
lan_drop
```

Uninstall:
```bash
sudo dnf remove landrop
```

---

### 2. Universal Linux (AppImage)

The AppImage bundles the Qt 6 runtime, plugins, and WebRTC dependencies (`libdatachannel`, `libusrsctp`). It runs on modern 64-bit Linux distributions.

Make executable:
```bash
chmod +x dist/LAN-Drop-1.0.2-x86_64.AppImage
```

Run:
```bash
# Systems with FUSE 2 installed:
./dist/LAN-Drop-1.0.2-x86_64.AppImage

# Systems with FUSE 3 only (Fedora 40+, Ubuntu 24.04+):
./dist/LAN-Drop-1.0.2-x86_64.AppImage --appimage-extract-and-run
```

Alternatively, install legacy FUSE 2 support:
```bash
# Fedora / RHEL
sudo dnf install fuse-libs

# Ubuntu / Debian
sudo apt install libfuse2t64  # or libfuse2
```

---

### 3. Ubuntu & Debian Linux

- **Recommended**: Run the **[AppImage](#2-universal-linux-appimage)** or **[build from source](#building-from-source)**.
- **Debian Package (`.deb`)**: Available at `dist/release-v1.0.2/landrop_1.0.2_amd64.deb`. Note that builds with `-DLANDROP_ENABLE_WEBRTC=ON` require `libdatachannel` and `libusrsctp`, which are bundled in the AppImage.

Runtime dependencies for the Qt 6 desktop package:
```bash
sudo apt-get update
sudo apt-get install -y libqt6widgets6 libqt6network6 libqt6concurrent6t64 libssl3 hicolor-icon-theme
```

---

### 4. Windows & macOS

- **Web Client**: Access the web client directly at `https://landrop-server.duckdns.org/`.
- **Windows Desktop**: Build an Inno Setup installer using the script at `packaging/windows/landrop.iss` via PowerShell (`.\scripts\package-windows.ps1`).

---

## Building from Source

### Prerequisites

| Component | Minimum Version | Package (Fedora) | Package (Ubuntu/Debian) |
|---|---|---|---|
| C++ Compiler | GCC 11+ or Clang 13+ | `gcc-c++` | `g++` |
| CMake | 3.20+ | `cmake` | `cmake` |
| Qt 6 | 6.4+ (Widgets, Network, Concurrent, Test) | `qt6-qtbase-devel` | `qt6-base-dev` |
| OpenSSL | 3.0+ | `openssl-devel` | `libssl-dev` |
| Go (Signaling) | 1.23+ | `golang` | `golang-go` |
| Node.js (Web) | 22+ (LTS) & npm | `nodejs` `npm` | `nodejs` `npm` |

### 1. Build Desktop Application (C++20 / Qt 6)

```bash
git clone https://github.com/vladispot002/air-share.git lan-drop
cd lan-drop

cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel $(nproc)
ctest --test-dir build --output-on-failure

./build/lan_drop
```

### 2. Build Web Client (TypeScript & Vite)

```bash
cd web
npm ci
npm test
npm run build
npm run dev
```

### 3. Build Go Signaling Server

```bash
cd backend/signaling
go test -v ./...
CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o bin/signaling-server ./cmd/server
./bin/signaling-server
```

---

## Quick Start and Usage

### 1. Launching the Desktop Application

```bash
# Launch Qt 6 GUI
lan_drop

# Run network diagnostics
lan_drop doctor
```

### 2. Modes

- **Guest Mode (Default)**: Use "Створити кімнату" or "Приєднатися до кімнати" to transfer files without registration.
- **Account Mode**: Switch to the Settings tab to sign in or register. This links devices under a common handle and syncs transfer metadata.

### 3. Transfer Scenarios

#### Scenario A: Desktop Host to Web Receiver
1. On Desktop: Select "Підключити пристрій (Web / Desktop)" -> "Створити кімнату".
2. Choose the file to send. The application displays an 8-character room code (e.g., `DROP-VGY4-FHA6`) and a QR code.
3. On Web (`https://landrop-server.duckdns.org`): Scan the QR code with a camera or enter the room code and click "Join Room".
4. The WebRTC connection opens, streams the file, verifies the SHA-256 checksum, and saves it.

#### Scenario B: Web Host to Desktop Receiver
1. On Web: Click "Choose File" to stage a file, then click "Host Room". The client displays the room code.
2. On Desktop: Select "Підключити пристрій" -> "Приєднатися (очікувати файл)" and enter the code.
3. The desktop connects, receives the stream, verifies the checksum, and saves the file to Downloads.

#### Scenario C: Desktop to Desktop
1. On Device 1: Host a room with a file or in standby receive mode.
2. On Device 2: Enter the room code or select "Приєднатися та надіслати файл".
3. The transfer proceeds directly over WebRTC or LAN.

#### Scenario D: Web to Web
1. Open `https://landrop-server.duckdns.org` on both devices.
2. Device 1 clicks "Host Room" with a selected file; Device 2 enters the code and clicks "Join Room".
3. Data streams directly between browser tabs.

#### Scenario E: Local LAN Offline Transfers (CLI)
For offline or air-gapped networks:
```bash
# Receiver (port 8848):
lan_drop receive 8848 /path/to/downloads --tls

# Sender:
lan_drop send document.pdf 192.168.1.50 8848 --tls
```

---

## Configuration

LAN-Drop supports environment variables for configuring signaling endpoints, networking, and security.

### Desktop and CLI Environment Variables

| Variable | Description | Default Value | Required? |
|---|---|---|---|
| `LANDROP_SIGNALING_URL` | WebSocket URL of the signaling service | `wss://landrop-server.duckdns.org/ws/signaling` | Optional |
| `QT_QPA_PLATFORM` | Qt platform plugin (set to `offscreen` for headless/CI runs) | System default (e.g., `xcb`, `wayland`) | Optional |
| `LD_LIBRARY_PATH` | Path to shared libraries (`libdatachannel`, `libusrsctp`) | System default / `~/.local/lib` | Optional |

### Production Server Configuration (`.env`)

A reference template is available in [`.env.example`](.env.example):

| Variable | Description | Example Format |
|---|---|---|
| `DOMAIN_NAME` | Fully Qualified Domain Name for Caddy HTTPS/WSS | `landrop.example.com` |
| `EXTERNAL_IP` | Public IPv4/IPv6 for Coturn WebRTC relay | `203.0.113.10` |
| `TURN_SECRET` | Cryptographic shared secret for ephemeral TURN credentials | `64-character random hex string` |
| `STUN_SERVERS` | Comma-separated public STUN endpoints | `stun:landrop.example.com:3478,stun:stun.l.google.com:19302` |
| `TURN_SERVERS` | Comma-separated TURN relay endpoints | `turn:landrop.example.com:3478?transport=udp` |
| `DATABASE_URL` | PostgreSQL connection string | `postgres://user:password@postgres:5432/landrop?sslmode=disable` |
| `ENV` | Operational environment mode | `production` |
| `TRUST_PROXY` | Trust reverse proxy headers (`X-Forwarded-For`) | `true` |
| `TRUSTED_PROXIES` | CIDR blocks of trusted reverse proxies | `127.0.0.1/8,10.0.0.0/8,172.16.0.0/12,192.168.0.0/16` |

---

## Testing

Automated test suites across all components:

```bash
# 1. C++ Desktop Core & Network Suites (34 suites, 1232 assertions)
ctest --test-dir build --output-on-failure

# 2. C++ Qt 6 GUI Suites (20 automated GUI tests)
./build/lan_drop_gui_tests

# 3. Web Client Unit & Integration Tests (86 tests)
cd web && npm test

# 4. Go Signaling Backend Tests (All packages)
cd backend/signaling && go test -v ./...

# 5. Network Diagnostics
lan_drop doctor --json
```

### Coverage Highlights
- **Protocol Framing & CRC-32**: Validation of 16-byte `LNDP` framing and Big-Endian integer conversions.
- **Resilient Resumption**: Verification of `ResumeRequest` (opcode `0x0008`) and byte-offset seeking.
- **Security & Path Traversal**: Rejection of directory traversal (`../../`), null bytes, and malicious filenames.
- **PIN Cryptography**: Validation of PBKDF2-HMAC-SHA256 hashing (100,000 iterations) and `CRYPTO_memcmp`.
- **E2E WebRTC Transfers**: Verification of DataChannel streaming, in-flight backpressure, and SHA-256 verification.

---

## Troubleshooting

### 1. AppImage Fails with `dlopen(): error loading libfuse.so.2`
- **Cause**: Modern Linux distributions (Fedora 40+, Ubuntu 24.04+) default to `fuse3` and omit legacy `libfuse.so.2`.
- **Solution A (No installation needed)**: Run with the extract-and-run flag:
  ```bash
  ./LAN-Drop-1.0.2-x86_64.AppImage --appimage-extract-and-run
  ```
- **Solution B (Install FUSE 2 library)**:
  - Fedora: `sudo dnf install fuse-libs`
  - Ubuntu/Debian: `sudo apt install libfuse2t64` (or `libfuse2`)

### 2. Missing Shared Library `libusrsctp.so.2` or `libdatachannel.so`
- **Cause**: Running unbundled native binaries directly outside of the AppImage environment.
- **Solution**: Set `LD_LIBRARY_PATH` to include the library install location:
  ```bash
  export LD_LIBRARY_PATH="${HOME}/.local/lib64:${HOME}/.local/lib:${LD_LIBRARY_PATH}"
  lan_drop
  ```

### 3. Signaling Error `ERR_RATE_LIMITED`
- **Cause**: Too many failed join attempts within a short interval.
- **Solution**: The rate limiter enforces a short cooldown period. Verify your room code format (`DROP-XXXX-XXXX`) and ensure the hosting client has not closed the session.

### 4. Room Not Found (`ERR_ROOM_NOT_FOUND`)
- **Cause**: The host closed the room or the initiator session timed out (rooms expire after 5 minutes of inactivity).
- **Solution**: Generate a fresh room code on the hosting client and join within 5 minutes.

### 5. Web Camera QR Scanner Not Starting
- **Cause**: Browser permissions or non-HTTPS environment.
- **Solution**: Modern browsers require a secure origin (`https://` or `localhost`) to access camera devices. Grant camera permissions when prompted, or use the file upload fallback to scan a saved QR code image.

---

## Development and Contributing

### Repository Structure

```
lan-drop/
├── backend/signaling/    # Go 1.23 WebSocket signaling daemon & REST API
├── docs/                 # Architectural specifications, decisions (ADR), and audits
├── include/landrop/      # C++20 header files organized by subsystem
│   ├── core/             # TransferEngine, WireProtocolV2, SessionCoordinator
│   ├── identity/         # IdentityClient and authentication models
│   ├── mobile/           # C-ABI FFI bridge and protocol stubs
│   ├── peers/            # LocalPeer, CloudPeer, NetworkPeer abstractions
│   ├── tasks/            # P2PTransferTask, HttpWebTransferTask
│   └── ui/               # MVP Presenters and Qt 6 Widget interfaces
├── packaging/            # Packaging specifications (Fedora .spec, Windows Inno Setup)
├── resources/            # Icons, stylesheets (QSS), desktop files, and AppStream XML
├── scripts/              # Build automation (build-appimage.sh, package-fedora.sh)
├── src/                  # C++20 implementation files
├── tests/                # CTest unit tests and Qt GUI tests
├── tools/diagnostics/    # Production network probing scripts
└── web/                  # TypeScript / Vite Web Client SPA
```

### Guidelines
1. Branch from `feature/unified-transfer-ecosystem` or create a descriptive feature branch (`feature/<feature-name>`).
2. Maintain `-std=c++20` standard compliance with zero compiler warnings (`-Wall -Wextra -Wpedantic`).
3. Ensure all tests pass (`ctest`, `npm test`, `go test ./...`) before submitting changes.
4. Open a pull request against `vladispot002/air-share`.

---

## Engineering Methodology and AI Disclosure

The development of LAN-Drop utilized AI-assisted engineering tools (large language models and multi-agent coordination frameworks) for architectural planning, code authoring, test generation, and documentation under human developer oversight.

Key technical boundaries:
- **Zero Runtime AI Dependencies**: The LAN-Drop runtime is 100% deterministic software compiled from C++20, Qt 6, Go 1.23, and TypeScript. It contains no neural networks, no machine learning runtimes, and makes no calls to AI APIs or cloud LLMs. Data transfers and cryptographic functions run locally on user machines.
- **Verification**: All generated or refactored code was verified with compiler warnings enabled (`-Wall -Wextra -Wpedantic`), automated test suites (32 test suites, 1,116 assertions), and container execution checks.

For the detailed disclosure on tooling, agent roles, and verification methods, see:  
[Disclosure: Artificial Intelligence in the Software Development Process](docs/AI_DEVELOPMENT_DISCLOSURE.md)

---

## License

LAN-Drop is licensed under the [MIT License](LICENSE).

```
Copyright (c) 2024-2026 LAN-Drop Contributors
```

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software.
