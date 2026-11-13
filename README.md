<div align="center">

# LAN-Drop

### Unified Peer-to-Peer & Cross-Network File Transfer Ecosystem

[![Version](https://img.shields.io/badge/version-1.0.2-blue.svg)](https://github.com/vladispot002/air-share)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/standard-C%2B%2B20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Qt 6](https://img.shields.io/badge/GUI-Qt%206-green.svg)](https://www.qt.io/)
[![Go](https://img.shields.io/badge/signaling-Go%201.23-00ADD8.svg)](https://go.dev/)
[![TypeScript](https://img.shields.io/badge/web-TypeScript%205.7-blue.svg)](https://www.typescriptlang.org/)
[![WebRTC](https://img.shields.io/badge/transport-WebRTC%20DataChannel-orange.svg)](https://webrtc.org/)

**LAN-Drop** is an ultra-fast, secure, cross-platform file transfer system designed for seamless data exchange across local networks (LAN/Wi-Fi) and the public Internet. It bridges desktop applications and web browsers with zero external cloud file storage, end-to-end cryptographic integrity, and account-optional instant guest pairing.

[Features](#-key-features) • [Architecture](#-architecture-overview) • [Installation](#-installation) • [Building from Source](#-building-from-source) • [Quick Start](#-quick-start--usage) • [Configuration](#-configuration) • [Testing](#-testing) • [Troubleshooting](#-troubleshooting)

</div>

---

## 🚀 Overview

LAN-Drop follows a strict **Transfer-First, Account-Optional, and Zero File Content Storage** architectural model:

1. **Direct Peer-to-Peer**: File data streams directly between sender and recipient over high-speed POSIX TCP sockets (on local networks) or encrypted WebRTC DataChannels (across the Internet).
2. **Zero Cloud File Storage**: The signaling server and database never receive, buffer, or store file contents. Only ephemeral session coordination (SDP offer/answer and ICE candidate exchange) passes through the signaling service.
3. **Account-Optional**: Instant guest transfers work out of the box with zero registration. Optional accounts provide persistent identity handles (`@username`), multi-device syncing, and transfer history.
4. **Resilient Streaming**: Built on Wire Protocol v2 with 16-byte binary framing, CRC-32 transport validation, full-file SHA-256 cryptographic verification, and byte-offset resumption for interrupted transfers.

---

## ✨ Key Features

### Implemented and Verified (v1.0.2)

- **Dual-Engine Desktop Application (Qt 6 & GTK3)**:
  - **Qt 6 GUI (`lan_drop`)**: Primary desktop interface featuring Radar Discovery, Transfers Dashboard with live progress meters, Shared Storage with PIN protection, and Settings with Light/Dark themes.
  - **GTK+ 3.0 GUI (`lan_drop_gtk`)**: Alternate interface adhering to macOS Human Interface Guidelines (HIG) with desktop and mobile (390×812) viewports.
- **Modern Web Client SPA (TypeScript & Vite)**:
  - Zero-install web application running in any modern browser (Chrome, Firefox, Safari, Brave, Edge).
  - WebRTC DataChannel streaming with backpressure flow control.
  - Interactive HTML5 camera QR scanner (`jsQR`) for instant mobile-to-desktop pairing.
- **Universal Transport Layer**:
  - **Local LAN**: Direct TCP/TLS socket streaming with 64 KB – 4 MB chunking and zero cloud reliance.
  - **Global P2P**: WebRTC DataChannels with STUN NAT traversal (`stun.l.google.com` or self-hosted Coturn).
  - **Symmetric NAT Traversal**: Automated TURN relay fallback with ephemeral HMAC credentials.
- **Optical & Room Code Pairing**:
  - Zero-dependency native ISO/IEC 18004 QR code generation in both C++ and TypeScript.
  - Human-friendly 8-character Crockford Base32 room codes (`DROP-XXXX-XXXX`) with optical confusion disambiguation (`O` → `0`, `I`/`L` → `1`).
- **Resilient Wire Protocol v2**:
  - 16-byte fixed binary header (`LNDP` magic, version, message type, payload length, CRC-32).
  - Byte-offset resumption (`ResumeRequest` opcode `0x0008`) for interrupted transfers.
  - In-flight `.part` file assembly with atomic rename only after complete SHA-256 checksum match.
  - Path traversal protection (`isSafeFileName`), symlink boundary validation, and disk quota enforcement.
- **Optional Identity & Device Subsystem**:
  - Go 1.23 signaling server backed by PostgreSQL 16.
  - Secure password registration with bcrypt (salt cost 10).
  - JWT bearer token authentication and device management.
- **Built-in System Diagnostics (`lan_drop doctor`)**:
  - Headless network audit inspecting local interfaces, multicast readiness, port availability, signaling health (`/healthz`), and STUN reflexive endpoints.

### Developer Preview / Simulated

- **AirDrop Protocol Bridge (`AirDropProtocolBridge`)**: Formats Bonjour/HTTP `/Discover` and `/Ask` protocol payloads in memory; hardware AWDL/BLE integration requires native OS kernel extensions.
- **NFC Fast Pairing (`NfcPairingSession`)**: Formats and parses NDEF text payloads (`LANDROP:NFC:v1;...`); physical NFC exchange requires external hardware controller integration.

### Planned Capabilities (Roadmap)

- **Native Mobile Applications**: Dedicated Flutter/Swift/Kotlin apps utilizing the C-ABI FFI bridge (`LandropMobileBridge`).
- **OPFS Direct-to-Disk Web Streaming**: Utilizing the Origin Private File System API for multi-gigabyte browser transfers without RAM buffering.

---

## 🏗 Architecture Overview

The system strictly decouples the signaling control plane from the actual peer-to-peer data plane:

```mermaid
graph TD
    subgraph Clients["LAN-Drop Clients"]
        Desktop["Desktop Application (C++20 / Qt 6)<br/>• TransferEngine<br/>• WebRtcSessionCoordinator<br/>• IdentityClient"]
        Web["Web Client (TypeScript / Vite)<br/>• WebRtcCoordinator<br/>• FileStreamSender / Receiver<br/>• Camera QR Scanner"]
    end

    subgraph Infrastructure["Production Cloud Infrastructure (Oracle Cloud VPS)"]
        Caddy["Caddy Reverse Proxy<br/>• Automatic TLS (Let's Encrypt)<br/>• HTTPS / WSS :443<br/>• Static Asset Serving"]
        Signaling["Go 1.23 Signaling Service (:8080)<br/>• Ephemeral Base32 Rooms<br/>• IP Rate Limiting (Token Bucket)<br/>• REST Auth & TurnConfig API"]
        Postgres["PostgreSQL 16 Database<br/>• Users & Passwords (bcrypt)<br/>• Linked Devices<br/>• Transfer Metadata"]
        Coturn["Coturn TURN/STUN Server<br/>• STUN :3478 UDP/TCP<br/>• TURN Relay :49160-49200 UDP<br/>• Ephemeral HMAC Auth"]
    end

    %% Signaling Connections
    Desktop -.->|"WSS Signaling & Auth"| Caddy
    Web -.->|"WSS Signaling & Auth"| Caddy
    Caddy -->|"/ws/signaling & /api/*"| Signaling
    Signaling -->|"TCP :5432"| Postgres

    %% Direct Peer-to-Peer Data Paths
    Desktop ===|"Direct LAN (TCP/TLS :8848)<br/>Zero Server Reliance"| Desktop
    Desktop ===|"Direct Internet P2P<br/>(WebRTC DataChannel / DTLS 1.3)"| Web
    Web ===|"Direct Web-to-Web P2P<br/>(WebRTC DataChannel / DTLS 1.3)"| Web
    Desktop ===|"Direct Desktop-to-Desktop P2P<br/>(WebRTC DataChannel / DTLS 1.3)"| Desktop

    %% TURN Relay Fallback
    Desktop -.-|"Symmetric NAT Relay"| Coturn
    Web -.-|"Symmetric NAT Relay"| Coturn
```

### Protocol & Data Path Highlights

- **Control Path (Signaling)**: Ephemeral WebSocket communication to register rooms, exchange SDP offers/answers, and trickle ICE candidates. Once connected, signaling activity ceases.
- **Data Path (P2P)**: Encrypted file chunk transmission directly between endpoints over DTLS 1.3/SCTP or TLS 1.3/TCP. The signaling server never touches file data.

---

## 📦 Installation

### 1. Fedora Linux (Tested & Verified RPM)

LAN-Drop provides an official RPM package for Fedora Linux (tested on Fedora 40, 41, and 44):

#### Install via DNF
```bash
# Install the official release RPM (resolves all Qt 6 and OpenSSL dependencies automatically)
sudo dnf install ./dist/rpm/landrop-1.0.2-1.fc44.x86_64.rpm
```

#### What Gets Installed
- **Binary**: `/usr/bin/lan_drop`
- **Desktop Entry**: `/usr/share/applications/lan-drop.desktop`
- **Application Icon**: `/usr/share/icons/hicolor/scalable/apps/lan-drop.svg`
- **Theme Stylesheet**: `/usr/share/landrop/styles.qss`
- **AppStream Metadata**: `/usr/share/metainfo/io.landrop.LAN-Drop.metainfo.xml`

#### Run the Application
Launch from your application menu or directly from terminal:
```bash
lan_drop
```

#### Uninstall
```bash
sudo dnf remove landrop
```

---

### 2. Universal Linux (AppImage)

The AppImage is a self-contained bundle containing the Qt 6 application, plugins, and bundled WebRTC/SCTP libraries (`libdatachannel`, `libusrsctp`). It runs on virtually any modern 64-bit Linux distribution (Fedora, Ubuntu, Debian, Arch, openSUSE, Mint).

#### Step 1: Set Executable Permissions
```bash
chmod +x dist/LAN-Drop-1.0.2-x86_64.AppImage
```

#### Step 2: Run the AppImage
```bash
# If your distribution has FUSE 2 installed:
./dist/LAN-Drop-1.0.2-x86_64.AppImage

# On modern distributions without legacy FUSE 2 (e.g., Fedora 40+, Ubuntu 24.04):
./dist/LAN-Drop-1.0.2-x86_64.AppImage --appimage-extract-and-run
```

> [!TIP]
> A convenient wrapper launcher script is provided. It automatically detects whether `libfuse.so.2` is present on your system and transparently applies `--appimage-extract-and-run` if necessary.

---

### 3. Ubuntu & Debian Linux

- **Recommended**: Use the Universal **[AppImage](#2-universal-linux-appimage)** or **[Build from Source](#-building-from-source)**.
- **Debian Package (`.deb`)**: A native `.deb` package (`dist/release-v1.0.2/landrop_1.0.2_amd64.deb`) is generated via CPack. Note that builds with `-DLANDROP_ENABLE_WEBRTC=ON` require `libdatachannel` and `libusrsctp`, which must be present on the host or installed via AppImage.

```bash
# Install required runtime dependencies for Qt 6 Desktop
sudo apt-get update
sudo apt-get install -y libqt6widgets6 libqt6network6 libqt6concurrent6t64 libssl3 hicolor-icon-theme
```

---

### 4. Windows & macOS

- **Web Client**: Access the fully featured Web client instantly without installation at **`https://landrop-server.duckdns.org/`**.
- **Windows Desktop**: An Inno Setup installer script is provided at `packaging/windows/landrop.iss`. It can be compiled using PowerShell on Windows (`.\scripts\package-windows.ps1`).

---

## 🛠 Building from Source

### Prerequisites

| Component | Minimum Version | Package Name (Fedora) | Package Name (Ubuntu/Debian) |
|---|---|---|---|
| **C++ Compiler** | GCC 11+ or Clang 13+ | `gcc-c++` | `g++` |
| **CMake** | 3.20+ | `cmake` | `cmake` |
| **Qt 6** | 6.4+ (Widgets, Network, Concurrent, Test) | `qt6-qtbase-devel` | `qt6-base-dev` |
| **OpenSSL** | 3.0+ | `openssl-devel` | `libssl-dev` |
| **Go** (Signaling) | 1.23+ | `golang` | `golang-go` |
| **Node.js** (Web) | 22+ (LTS) & npm | `nodejs` `npm` | `nodejs` `npm` |

---

### 1. Build the Desktop Application (C++20 / Qt 6)

```bash
# 1. Clone repository
git clone https://github.com/vladispot002/air-share.git lan-drop
cd lan-drop

# 2. Configure build with testing enabled
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON

# 3. Compile all targets
cmake --build build --parallel $(nproc)

# 4. Run automated test suites
ctest --test-dir build --output-on-failure

# 5. Launch the desktop GUI
./build/lan_drop
```

---

### 2. Build the Web Client SPA (TypeScript & Vite)

```bash
cd web

# Install dependencies
npm ci

# Run test suite (86 unit and integration tests)
npm test

# Build production bundle (outputs to web/dist)
npm run build

# Run local development server
npm run dev
```

---

### 3. Build the Go Signaling Server

```bash
cd backend/signaling

# Run unit and integration tests
go test -v ./...

# Compile standalone static binary
CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o bin/signaling-server ./cmd/server

# Run signaling server locally
./bin/signaling-server
```

---

## 📖 Quick Start & Usage

### 1. Launching the Desktop Application
Run `lan_drop` from the terminal or application launcher. The main window opens with the **Radar Discovery** view:

```bash
# Launch Qt 6 GUI
lan_drop

# Or run system diagnostics
lan_drop doctor
```

### 2. Guest Mode vs. Registered Account
- **Guest Mode (Default)**: Click `"📷 Створити кімнату"` or `"⌨️ Приєднатися до кімнати"` to start sharing files instantly with zero account setup.
- **Account Mode**: Switch to the **Settings** tab to register or sign in. Once signed in, your device handle is registered and paired across your ecosystem.

### 3. Cross-Platform Transfer Scenarios

#### Scenario A: Desktop Host → Web Receiver
1. On Desktop: Click **"🔗 Підключити пристрій (Web / Desktop)"** → **"📷 Створити кімнату"**.
2. Select the file you want to send. A dialog displays the 8-character code (e.g. `DROP-VGY4-FHA6`) and an optical QR code.
3. On Web (`https://landrop-server.duckdns.org`): Either scan the QR code with your camera or enter the room code in the **Room Code** input and click **Join Room**.
4. The WebRTC DataChannel connects instantly, streams the file, validates the SHA-256 hash, and saves the file.

#### Scenario B: Web Host → Desktop Receiver
1. On Web: Click **Choose File** to stage a file, then click **Host Room**. The web client displays the room code (e.g. `DROP-5X1H-9TEA`).
2. On Desktop: Click **"🔗 Підключити пристрій"** → **"⌨️ Приєднатися (очікувати файл)"** and enter the code.
3. The desktop connects, receives the stream, validates the file, and stores it in your `Downloads` directory.

#### Scenario C: Desktop → Desktop
1. On Device 1: Host a room with a file or in standby receive mode.
2. On Device 2: Enter the room code or click **"📤 Приєднатися та надіслати файл"** to stream directly.
3. Transfers execute peer-to-peer at full line rate with progress updates.

#### Scenario D: Web → Web
1. Open `https://landrop-server.duckdns.org` on both devices.
2. Device 1 clicks **Host Room**. Device 2 enters the code and clicks **Join Room**.
3. Files stream directly between the two browser tabs.

#### Scenario E: Local LAN Offline Transfers (CLI)
When operating in completely air-gapped or offline networks:
```bash
# On Receiver (listening on port 8848):
lan_drop receive 8848 /path/to/downloads --tls

# On Sender:
lan_drop send document.pdf 192.168.1.50 8848 --tls
```

---

## ⚙️ Configuration

LAN-Drop supports environment variables for configuring signaling endpoints, networking, and security.

### Desktop & CLI Environment Variables

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

## 🧪 Testing

LAN-Drop includes exhaustive automated test suites across all layers of the stack:

```bash
# 1. C++ Desktop Core & Network Suites (34 suites, 1232 assertions)
ctest --test-dir build --output-on-failure

# 2. C++ Qt 6 GUI Suites (20 automated GUI tests)
./build/lan_drop_gui_tests

# 3. Web Client Unit & Integration Tests (86 tests)
cd web && npm test

# 4. Go Signaling Backend Tests (All packages)
cd backend/signaling && go test -v ./...

# 5. Live Network Diagnostics (probes interfaces, multicast, signaling, and STUN)
lan_drop doctor --json
```

### What the Test Suites Verify
- **Protocol Framing & CRC-32**: Byte-exact validation of 16-byte `LNDP` framing and Big-Endian integer conversions.
- **Resilient Resumption**: Verification of `ResumeRequest` (opcode `0x0008`) and byte-offset seeking.
- **Security & Path Traversal**: Rejection of directory traversal (`../../`), null bytes, and malicious filenames.
- **PIN Cryptography**: Validation of PBKDF2-HMAC-SHA256 hashing (100,000 iterations) and `CRYPTO_memcmp`.
- **E2E WebRTC Transfers**: Verification of DataChannel streaming, in-flight backpressure, and SHA-256 verification.

---

## 🔧 Troubleshooting

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
- **Solution**: The rate limiter enforces a short 30-second cooldown period. Verify your room code format (`DROP-XXXX-XXXX`) and ensure the hosting client has not closed the session.

### 4. Room Not Found (`ERR_ROOM_NOT_FOUND`)
- **Cause**: The host closed the room or the initiator session timed out (rooms automatically expire after 5 minutes of inactivity).
- **Solution**: Generate a fresh room code on the hosting client and join within 5 minutes.

### 5. Web Camera QR Scanner Not Starting
- **Cause**: Browser permissions or non-HTTPS environment.
- **Solution**: Modern browsers require a secure origin (`https://` or `localhost`) to access camera devices. Ensure you grant camera permissions when prompted, or use the file upload fallback to scan a saved QR code image.

---

## 👥 Development & Contribution

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

### Contribution Guidelines
1. Branch from `feature/unified-transfer-ecosystem` or create a descriptive feature branch (`feature/<feature-name>`).
2. Maintain strict `-std=c++20` standard compliance with zero compiler warnings (`-Wall -Wextra -Wpedantic`).
3. Ensure all tests pass (`ctest`, `npm test`, `go test ./...`) before submitting changes.
4. Open a pull request against `vladispot002/air-share`.

---

## 📄 License & Metadata

LAN-Drop is open-source software licensed under the **[MIT License](LICENSE)**.

```
Copyright (c) 2026 LAN-Drop Team
```

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software.
