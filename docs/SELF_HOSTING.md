# LAN-Drop Production Self-Hosting Guide

This guide describes how to deploy and operate a private, production-ready LAN-Drop infrastructure with automated TLS, Coturn TURN/STUN relay, and high-performance Go WebSocket signaling.

---

## 1. Architectural Overview

LAN-Drop is built around the **Transfer-First and Zero-Telemetry principle**:
- **Local Network (LAN)**: When devices reside on the same Wi-Fi or subnet, transfers execute **100% peer-to-peer, offline, and serverless** over local TCP/TLS and multicast beacon discovery. No cloud or signaling servers are contacted.
- **Global / Internet P2P**: When peers are on separate networks behind restrictive NATs or cellular firewalls, the self-hosted stack orchestrates ephemeral WebRTC signaling (SDP offer/answer and ICE candidate exchange) and media relay fallback via Coturn.

```
                          ┌────────────────────────┐
                          │   Internet / Clients   │
                          └───────────┬────────────┘
                                      │
                         HTTPS / WSS (443/TCP)
                                      ▼
             ┌────────────────────────────────────────────────┐
             │            Caddy Reverse Proxy                 │
             │   - Automatic Let's Encrypt TLS                │
             │   - Security Headers (HSTS, CSP, nosniff)      │
             │   - WebSocket Upgrades & Asset Caching         │
             └───────┬────────────────────────────────┬───────┘
                     │                                │
             /ws/signaling, /api/*, /               Direct STUN/TURN
                     ▼                                │ (3478/UDP, 49160-49200/UDP)
    ┌─────────────────────────────────┐               │
    │   LAN-Drop Signaling Service    │               ▼
    │   - Go 1.23 minimal runtime     │     ┌───────────────────┐
    │   - Unprivileged non-root user  │     │   Coturn Server   │
    │   - Static Web SPA bundle       │     │   - STUN / TURN   │
    │   - Ephemeral in-memory rooms   │     │   - Anti-SSRF     │
    │   - Zero telemetry & logging    │     │   - UDP Relay     │
    └─────────────────────────────────┘     └───────────────────┘
```

---

## 2. System Requirements & Port Matrix

### Recommended Host Specs
- **CPU**: 1 vCPU (x86_64 or ARM64)
- **RAM**: 512 MB minimum (1 GB recommended)
- **Disk**: 10 GB SSD
- **OS**: Any modern Linux distribution with Docker 24+ and Docker Compose v2+

### Network Firewall Rules (Inbound)

| Port | Protocol | Service | Description |
| :--- | :--- | :--- | :--- |
| `80` | TCP | Caddy | HTTP (ACME challenge & automatic redirect to HTTPS) |
| `443` | TCP | Caddy | HTTPS (Web SPA client & WSS signaling router) |
| `3478` | UDP / TCP | Coturn | STUN NAT discovery & TURN control channel |
| `49160-49200` | UDP | Coturn | Dynamic WebRTC media relay port range |

---

## 3. Step-by-Step Deployment

### Step 1: Clone Repository & Staging Directory
```bash
git clone https://github.com/vladispot002/air-share.git /opt/lan-drop
cd /opt/lan-drop
```

### Step 2: Generate Cryptographic Secrets
Generate strong random keys for TURN authentication and signaling API tokens:
```bash
# 256-bit hex secret for Coturn ephemeral credentials
openssl rand -hex 32

# 128-bit hex secret for signaling API bearer token
openssl rand -hex 16
```

### Step 3: Configure Environment Variables
Copy `.env.example` to `.env`:
```bash
cp .env.example .env
chmod 600 .env
```
Edit `.env` with your domain, secrets, and admin email:
```ini
DOMAIN=drop.example.com
LETSENCRYPT_EMAIL=admin@example.com

TURN_SECRET=your_generated_32_byte_turn_secret_here
TURN_REALM=drop.example.com
TURN_EXTERNAL_IP=203.0.113.10

SIGNALING_AUTH_TOKEN=your_generated_16_byte_auth_token_here
TURN_TTL_SECONDS=86400
LOG_LEVEL=info
```

### Step 4: Launch Container Stack
```bash
docker compose up -d
```

### Step 5: Verify Deployment Status
```bash
docker compose ps
docker compose logs -f signaling
```

---

## 4. Production Security Hardening

### Non-Root Minimal Container
The `signaling` container is built with a 3-stage minimal Dockerfile:
- Stripped static Go binary (`-s -w`).
- Dedicated unprivileged non-root user (`landrop:10001`).
- `read_only: true` root filesystem.
- `cap_drop: [ALL]` with `no-new-privileges: true`.

### Anti-SSRF Coturn Protections
To prevent malicious relay loops or intranet scanning from untrusted WebRTC peers, Coturn is configured to drop connections targeting private IP blocks:
- `10.0.0.0/8`
- `172.16.0.0/12`
- `192.168.0.0/16`
- `127.0.0.0/8`
- `fc00::/7`, `::1/128`, `fe80::/10`

### Zero-Telemetry & In-Memory Ephemeral Storage
- All WebRTC rooms and signaling codes expire after 10 minutes or upon peer disconnection.
- Signaling messages are forwarded in memory and immediately discarded.
- No database or persistent storage is mounted or required.

---

## 5. Health Checks & Automated Verification

### HTTP Health Probe
The signaling server exposes a lightweight health check at `/healthz`:
```bash
curl -I https://drop.example.com/healthz
# HTTP/2 200
# content-type: text/plain; charset=utf-8
```

### Built-In CLI Diagnostic Engine (`lan_drop doctor`)
Verify your self-hosted instance from any desktop or server client:
```bash
lan_drop doctor --signaling https://drop.example.com/healthz --stun drop.example.com:3478
```

Example diagnostic report:
```
========================================================
  LAN-Drop System & Network Diagnostics (Doctor)
========================================================

[1] Network Interfaces:
  [PASS] eth0: 192.168.1.100 [multicast] [up]

[2] Transport & Discovery Ports:
  [PASS] TCP port 8848 is Available for binding
  [PASS] UDP port 52637 is Available for binding

[3] Signaling Server Reachability:
  [PASS] https://drop.example.com/healthz -> Operational (HTTP 200) (RTT 14 ms)

[4] NAT Reflexive Probe (RFC 5389 STUN):
  [PASS] drop.example.com:3478 -> Public reflexive endpoint: 203.0.113.50:54120 (RTT 22 ms)

--------------------------------------------------------
  Overall Diagnostic Assessment: HEALTHY - ALL SYSTEMS GO
========================================================
```

For programmatic monitoring / alert scripts:
```bash
lan_drop doctor --json | jq .overall_status
# "PASS"
```

---

## 6. Maintenance & Updates

### Updating to the Latest Release
```bash
cd /opt/lan-drop
git pull origin main
docker compose build --pull
docker compose up -d
```

### Inspecting Container Metrics
```bash
docker stats --no-stream
```
Typical resource utilization:
- `signaling`: ~12 MB RAM, < 0.5% CPU
- `caddy`: ~25 MB RAM, < 0.2% CPU
- `coturn`: ~15 MB RAM, < 0.5% CPU (idle)
