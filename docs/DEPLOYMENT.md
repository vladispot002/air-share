# LAN-Drop Ecosystem — Production Operations & Deployment Guide

This document is the authoritative operations guide for deploying, running, and maintaining the production LAN-Drop ecosystem across web, desktop, and mobile infrastructure.

---

## 1. Ecosystem Architecture & Zero-Knowledge Storage Model

LAN-Drop follows a **Transfer-First, Account-Optional, and Zero File Content Storage** architectural model:

1. **Zero File Content Cloud Storage**: Files are transferred **directly peer-to-peer** between sender and recipient (via local TCP/TLS on LAN, or WebRTC DataChannels over the Internet). The backend signaling server and PostgreSQL database **never receive, buffer, or store file contents**.
2. **Account-Optional**: Immediate guest transfers function out of the box with zero registration. Registered accounts provide persistent user handles (`@username`), multi-device syncing, contact books, and transfer metadata history.
3. **Database Layer (PostgreSQL)**: Stores only user accounts, linked devices, contact pairings, and transfer session metadata (file names, sizes, cryptographic hashes).

```
                            ┌────────────────────────┐
                            │   Internet / Clients   │
                            │  (Web, Desktop, Mobile)│
                            └───────────┬────────────┘
                                        │
                           HTTPS / WSS (Port 443/TCP)
                                        ▼
               ┌────────────────────────────────────────────────┐
               │              Caddy Reverse Proxy               │
               │   - Automatic Let's Encrypt TLS (Zero-Touch)   │
               │   - HTTP/2 & HTTP/3 (QUIC) Transport           │
               │   - Strict Security Headers (CSP, HSTS)        │
               │   - WSS WebSocket Upgrade to Signaling         │
               └────────┬───────────────────────────────┬───────┘
                        │                               │
                /ws/signaling, /api/*, /        Direct STUN / TURN
                        ▼                       (3478 UDP/TCP, 49160-49200 UDP)
       ┌─────────────────────────────────┐              │
       │   LAN-Drop Signaling Service    │              ▼
       │   - Go 1.23 Minimal Runtime     │    ┌───────────────────┐
       │   - Unprivileged Container (UID)│    │   Coturn Server   │
       │   - In-Memory Fallback Option   │    │   - STUN / TURN   │
       │   - Mode A & Mode B Transfer Hub│    │   - Anti-SSRF     │
       │   - Auto-Migrates Schema on Boot│    │   - Ephemeral HMAC│
       └────────────────┬────────────────┘    └───────────────────┘
                        │
                Port 5432 (Internal)
                        ▼
       ┌─────────────────────────────────┐
       │      PostgreSQL 16 Alpine       │
       │   - Persistent Named Volume     │
       │   - Healthcheck: pg_isready     │
       │   - Atomic Claiming Locks       │
       └─────────────────────────────────┘
```

---

## 2. Global Connectivity Matrix

To avoid confusion between application software readiness and external hosting prerequisites, the following matrix details exact component boundaries:

| Capability | Software Implementation (In Repository) | External Infrastructure Requirement |
| :--- | :--- | :--- |
| **Local P2P Transfers** | 100% Implemented (TCP/TLS socket, multicast beacon, zero servers needed). | None. Operates fully offline on any local subnet or Wi-Fi. |
| **Direct Internet P2P** | 100% Implemented (WebRTC DataChannel, STUN NAT discovery). | Public STUN server (default Google/Twilio or self-hosted Coturn). |
| **Symmetric NAT Relay** | 100% Implemented (Coturn TURN integration, ephemeral HMAC credentials). | Publicly routable IPv4/IPv6, firewall ports open (3478, 49160-49200). |
| **Secure Signaling (WSS)**| 100% Implemented (Go WebSocket hub, Crockford Base32 rooms, Glare prevention). | Registered DNS domain pointing to host IP, Caddy port 80/443 open. |
| **Identity & Accounts** | 100% Implemented (`PostgresStore`, Magic Link, OAuth verification, Mode B claim). | PostgreSQL 16 container (provided via `docker-compose.yml`). |
| **Web Client SPA** | 100% Implemented (Vite production bundle embedded or served via Caddy). | Web browser with WebRTC & WebCrypto support (Chrome, Firefox, Safari, Edge). |

---

## 3. Host System Requirements & Port Matrix

### Recommended Specifications
- **CPU**: 1 vCPU (x86_64 or ARM64)
- **RAM**: 1 GB minimum (2 GB recommended for high concurrent relays)
- **Disk**: 20 GB SSD
- **OS**: Linux (Debian 12+, Ubuntu 22.04+, RHEL 9+, Alpine 3.19+)
- **Software**: Docker Engine 24+ and Docker Compose v2.20+

### Inbound Firewall Configuration

```bash
# UFW Example (Ubuntu / Debian)
sudo ufw allow 80/tcp comment "Caddy HTTP ACME Challenge"
sudo ufw allow 443/tcp comment "Caddy HTTPS & WSS"
sudo ufw allow 443/udp comment "Caddy HTTP/3 QUIC"
sudo ufw allow 3478/tcp comment "Coturn STUN/TURN Control"
sudo ufw allow 3478/udp comment "Coturn STUN/TURN Discovery"
sudo ufw allow 49160:49200/udp comment "Coturn WebRTC Relay Range"
sudo ufw reload
```

---

## 4. Production Deployment Procedure

### Step 1: Clone Repository & Prepare Environment
```bash
git clone https://github.com/vladispot002/air-share.git /opt/lan-drop
cd /opt/lan-drop
cp .env.example .env
chmod 600 .env
```

### Step 2: Generate Cryptographic Secrets
```bash
# Generate 256-bit TURN HMAC Secret:
openssl rand -hex 32

# Generate Strong PostgreSQL Password:
openssl rand -base64 24
```

### Step 3: Populate `.env`
Edit `/opt/lan-drop/.env`:
```ini
DOMAIN_NAME=drop.yourdomain.com
EXTERNAL_IP=203.0.113.10
TURN_SECRET=<OUTPUT_OF_OPENSSL_RAND_HEX_32>
POSTGRES_USER=landrop
POSTGRES_PASSWORD=<OUTPUT_OF_OPENSSL_RAND_BASE64_24>
POSTGRES_DB=landrop
DATABASE_URL=postgres://landrop:<OUTPUT_OF_OPENSSL_RAND_BASE64_24>@postgres:5432/landrop?sslmode=disable
STUN_SERVERS=stun:drop.yourdomain.com:3478,stun:stun.l.google.com:19302
TURN_SERVERS=turn:drop.yourdomain.com:3478?transport=udp,turn:drop.yourdomain.com:3478?transport=tcp
ENV=production
TRUST_PROXY=true
TRUSTED_PROXIES=127.0.0.1/8,10.0.0.0/8,172.16.0.0/12,192.168.0.0/16
```

### Step 4: Validate Configuration & Launch
```bash
# Validate Docker Compose configuration
docker compose config

# Build and start services in detached mode
docker compose up -d --build
```

### Step 5: Verify Deployment Health
```bash
# Check all container states
docker compose ps

# Check Signaling & Database Healthcheck endpoint
curl -s https://drop.yourdomain.com/healthz | jq .
```
Expected output:
```json
{
  "status": "ok",
  "database": "healthy",
  "version": "2.0.0",
  "active_rooms": 0
}
```

---

## 5. PostgreSQL Schema Migrations, Maintenance & Disaster Recovery

### Automated Migrations
On startup, `landrop-signaling` connects to PostgreSQL and applies `internal/identity/schema.sql` idempotently using `CREATE TABLE IF NOT EXISTS` and `CREATE INDEX IF NOT EXISTS`. No manual migration scripts or third-party migration binaries are required.

### Database Backup (`pg_dump`)
Run scheduled backups using cron:
```bash
#!/bin/bash
BACKUP_DIR="/var/backups/landrop"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
mkdir -p "$BACKUP_DIR"

docker compose exec -T postgres pg_dump -U landrop -F c landrop > "$BACKUP_DIR/landrop_$TIMESTAMP.dump"

# Retain backups for 14 days
find "$BACKUP_DIR" -type f -name "*.dump" -mtime +14 -delete
```

### Disaster Recovery Restore (`pg_restore`)
To restore a snapshot into a clean instance:
```bash
# Stop signaling service to prevent active connections
docker compose stop signaling

# Drop and recreate database
docker compose exec -T postgres psql -U landrop -d postgres -c "DROP DATABASE landrop;"
docker compose exec -T postgres psql -U landrop -d postgres -c "CREATE DATABASE landrop OWNER landrop;"

# Restore dump
cat /var/backups/landrop/landrop_YYYYMMDD_HHMMSS.dump | \
  docker compose exec -T postgres pg_restore -U landrop -d landrop --clean --if-exists

# Restart signaling service (will verify schema on startup)
docker compose start signaling
```

---

## 6. Security Hardening & Isolation Controls

1. **Unprivileged Containers**:
   - `landrop-signaling` runs as unprivileged user `10001:10001` with `read_only: true` and `cap_drop: ALL`.
   - `landrop-coturn` runs unprivileged with `cap_drop: ALL` and retains only `NET_BIND_SERVICE`.
2. **SSRF Mitigation**:
   - Coturn strictly denies all RFC 1918, RFC 3927, RFC 4193, loopback, and IPv4-mapped IPv6 subnets (`--denied-peer-ip`), blocking malicious actors from relaying traffic into internal networks or cloud metadata APIs (e.g. `169.254.169.254`).
3. **Caddy Reverse Proxy**:
   - Automated ACME certificate issuance with TLS 1.3 enforcement and HSTS (`max-age=31536000; includeSubDomains; preload`).
   - Content Security Policy (CSP) blocking unauthorized scripts and cross-site injections.
4. **Rate Limiting & Glare Protection**:
   - In-memory token bucket rate limiter isolating requests by client IP.
   - Dual-state glare lock preventing WebRTC SDP negotiation collisions.
