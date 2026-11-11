# LAN-Drop Diagnostic & Verification Tools

This directory contains standalone, non-destructive diagnostic tools for verifying networking, signaling, and TURN infrastructure in development and production environments.

## Tools Overview

### 1. `probe_prod_turn.mjs`
**Purpose**: Probes the production signaling service and Coturn TURN server to verify:
- WebSocket connection to `/ws/signaling`.
- Room creation and room token issuance.
- Authenticated retrieval of ICE configuration from `/api/v1/turn-config`.
- Dynamic HMAC credential generation for Coturn.
- Allocation of relay candidates strictly under `iceTransportPolicy: "relay"`.
- Verification of relay candidate type (`typ relay`), public IP (`92.5.49.103`), and configured port range (`49160-49200/udp`).

**Usage**:
```bash
node tools/diagnostics/probe_prod_turn.mjs
```

**Environment Variables (Optional)**:
- `SIGNALING_WS_URL`: defaults to `wss://landrop-server.duckdns.org/ws/signaling`
- `TURN_CONFIG_URL`: defaults to `https://landrop-server.duckdns.org/api/v1/turn-config`
- `EXPECTED_PUBLIC_IP`: defaults to `92.5.49.103`
- `CDP_PORT`: Chrome DevTools Protocol port (defaults to `9348`)

**Safety Guarantees**:
- Strictly enforces per-step timeouts (no indefinite hangs).
- Masks room tokens and HMAC credentials in console output.
- Tears down all child processes and cleans temporary browser directories on exit or failure.

### 2. `verify_prod_turn_relay_e2e.mjs`
**Purpose**: Performs full end-to-end WebRTC Data Relay verification between two headless browser instances strictly over Coturn relay:
- Forcibly enforces `iceTransportPolicy: "relay"` on both endpoints (prohibiting host/srflx P2P).
- Exclusively negotiates through production signaling (`wss://landrop-server.duckdns.org/ws/signaling`).
- Verifies that `RTCPeerConnection.getStats()` selected candidate pair is strictly `relay <-> relay` on public IP `92.5.49.103` in range `49160-49200`.
- Transfers binary file payloads bidirectionally (512 KiB A -> B, 256 KiB B -> A) over `RTCDataChannel`.
- Computes SHA-256 in real time on both endpoints, asserting bit-exact matches.
- Confirms actual bytes incremented on the selected TURN relay candidate pair.

**Usage**:
```bash
node tools/diagnostics/verify_prod_turn_relay_e2e.mjs
```

