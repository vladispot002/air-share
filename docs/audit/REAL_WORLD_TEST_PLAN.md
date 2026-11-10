# LAN-Drop: Real-World Test Plan & Network Validation Specification

**Date**: 2026-11-10  
**Target**: LAN-Drop P2P Transfer Engine (Branch `audit/production-readiness`)  
**Lead Author**: Lead QA Engineer  
**Classification**: Quality Assurance & Network Verification Standard

---

## 1. Scope & Objective

Current automated testing for LAN-Drop relies almost exclusively on in-memory mock loops (`opts.mockLoopbackMode = true` in C++) and simulated duplex channels (`MockDataChannel` in TypeScript). Zero automated tests currently transmit files over real `libdatachannel` WebRTC peer connections or across distinct network subnets.

This document establishes a **reproducible, rigorous real-world test plan** to validate LAN-Drop under physical network conditions, real hardware devices, cellular networks, symmetric NAT topologies, and adversarial fault injection.

---

## 2. Hardware & Infrastructure Requirements

### 2.1 Physical Test Hardware Matrix

| Hardware ID | Device Description | Platform / OS | Network Interfaces | Role in Test Suite |
| :--- | :--- | :--- | :--- | :--- |
| **HW-LIN-01** | ThinkPad T14 (AMD Ryzen 7, 32GB RAM) | Fedora 41 (Kernel 6.11) | 1 Gbps Ethernet, Wi-Fi 6E | Primary Linux Desktop Sender / Receiver |
| **HW-WIN-01** | Dell XPS 15 (Intel Core i7, 16GB RAM) | Windows 11 Pro 24H2 | Wi-Fi 6, 2.5 Gbps USB-C | Windows Desktop Sender / Receiver (NTFS) |
| **HW-MAC-01** | MacBook Pro 14 (Apple M3 Pro, 18GB RAM)| macOS Sonoma 14.6 | Wi-Fi 6E, Thunderbolt | Web Browser Receiver (Safari 18, Chrome 130) |
| **HW-AND-01** | Google Pixel 8 (Tensor G3, 8GB RAM) | Android 15 | 4G LTE / 5G Sub-6, Wi-Fi 6 | Mobile Web Receiver / PWA Receiver |
| **HW-IOS-01** | iPhone 15 Pro (A17 Pro, 8GB RAM) | iOS 18.2 | 5G Ultra Wideband, Wi-Fi 6 | Mobile Web Receiver / Safari PWA Receiver |

### 2.2 Server Infrastructure Matrix

| Server ID | Public Hostname | Service Role | Hosting Location | IP Configuration |
| :--- | :--- | :--- | :--- | :--- |
| **SRV-SIG-01** | `signaling.lan-drop.test` | Go Signaling Daemon + Static Web SPA | Hetzner Cloud (Falkenstein) | Dedicated Public IPv4 + IPv6 |
| **SRV-TURN-01**| `turn.lan-drop.test` | Coturn STUN/TURN Relay Daemon | DigitalOcean (Frankfurt) | Dedicated Public IPv4 + IPv6 |
| **NET-ROUTER** | Dual-WAN Edge Gateway | pfSense 2.7.2 | Local Lab | Configurable NAT types (Full Cone, Symmetric, Port-Restricted) |

---

## 3. Network Topologies Under Test

```
[ TOPOLOGY 1: Unmanaged Local Subnet (Pure Zero-Cloud LAN) ]
  HW-LIN-01 (192.168.1.10) <====== Gigabit Switch ======> HW-WIN-01 (192.168.1.20)
  (Direct TCP 8848 & UDP Multicast Beacon 52637 — WAN Disconnected / Offline)

[ TOPOLOGY 2: Cross-Subnet Enterprise Network (VLAN Isolation) ]
  HW-LIN-01 (VLAN 10: 10.0.10.5) ---> [ Corporate Firewall ] ---> Internet
                                                │
                                         (SRV-TURN-01)
                                                │
  HW-WIN-01 (VLAN 20: 10.0.20.8) ---> [ Strict Firewall ] ------> Internet
  (Host Candidates Unreachable -> Must Resolve via Server Reflexive or Relay)

[ TOPOLOGY 3: Symmetric NAT to Carrier-Grade NAT (CGNAT) ]
  HW-LIN-01 (Symmetric NAT Router) <====== TURN Relay ======> HW-AND-01 (Cellular 4G/5G CGNAT)
  (Direct P2P Impossible -> Strict Traversal via Coturn SRV-TURN-01)

[ TOPOLOGY 4: Dual-Stack IPv4 / IPv6 Mobile Roaming ]
  HW-IOS-01 (IPv6-Only Cellular) <====== WebRTC P2P ======> HW-WIN-01 (IPv4 Home Broadband)
```

---

## 4. Test Scenarios, Procedures & Pass/Fail Criteria

### Scenario TC-RW-01: Direct Offline LAN Transfer (Desktop ↔ Desktop)
- **Topologies**: Topology 1 (Unmanaged Local Subnet, WAN disconnected).
- **Devices**: HW-LIN-01 (Sender), HW-WIN-01 (Receiver).
- **Payload**: 5 GB uncompressed video archive (`test_archive_5gb.tar`).
- **Procedure**:
  1. Disconnect upstream WAN gateway from the local switch.
  2. Launch `./build/lan_drop` on HW-WIN-01 in GUI mode.
  3. Launch `./build/lan_drop` on HW-LIN-01 in GUI mode.
  4. Verify peer detection via UDP multicast beacon radar within 3 seconds.
  5. Select peer, initiate 5 GB file transfer.
- **Pass Criteria**:
  - Transfer completes without internet connection.
  - Transfer throughput exceeds 85 MB/s on Gigabit Ethernet.
  - Final SHA-256 matches bit-for-bit on NTFS filesystem.
  - Zero memory growth in host processes (<50 MB RSS throughout transfer).

---

### Scenario TC-RW-02: Cross-Platform Web-to-Desktop P2P (WebRTC WireProtocolV2)
- **Topologies**: Topology 2 (Cross-Subnet Enterprise).
- **Devices**: HW-AND-01 (Android Chrome, Sender) to HW-LIN-01 (Linux Desktop, Receiver).
- **Payload**: 500 MB photo archive (`photos_500mb.zip`).
- **Pre-requisite**: Defect `DEF-ENG-01` must be remediated (WireProtocolV2 integrated into `TransferEngine`).
- **Procedure**:
  1. HW-LIN-01 runs `lan_drop p2p-receive DROP-WXYZ-7890 /home/user/Downloads`.
  2. HW-AND-01 opens `https://signaling.lan-drop.test/#DROP-WXYZ-7890`.
  3. Selects `photos_500mb.zip` in browser and clicks Send.
- **Pass Criteria**:
  - Desktop `TransferEngine` parses 16-byte `LNDP` binary header without length exception.
  - WebRTC DataChannel streams chunks with backpressure.
  - Transfer finishes with `TransferAck(Success)`.
  - Checksum matches source hash.

---

### Scenario TC-RW-03: Symmetric NAT Traversal via Coturn Relay
- **Topologies**: Topology 3 (Symmetric NAT to Cellular CGNAT).
- **Devices**: HW-LIN-01 (Behind pfSense Symmetric NAT) to HW-IOS-01 (5G Cellular).
- **Payload**: 1 GB binary file (`random_1gb.bin`).
- **Pre-requisite**: Defect `SEC-02` remediated (`TURN_SECRET` synchronized).
- **Procedure**:
  1. Configure pfSense outbound NAT rule to "Symmetric / Random Port Allocation" for HW-LIN-01.
  2. Initiate room creation on HW-LIN-01.
  3. Join room on HW-IOS-01 via optical QR scan.
  4. Inspect WebRTC `getStats()` report to verify candidate pair type.
- **Pass Criteria**:
  - STUN direct binding fails as expected; ICE successfully gathers `relay` candidates.
  - Active candidate pair shows `local-candidate: relay`, `remote-candidate: relay` (or `prflx`).
  - 1 GB file transfers through Coturn relay without drops.
  - Final SHA-256 verified.

---

### Scenario TC-RW-04: Network Interface Roaming (Wi-Fi to Cellular Handoff)
- **Topologies**: Topology 4 (Mobile Roaming).
- **Devices**: HW-LIN-01 (Sender) to HW-AND-01 (Receiver).
- **Payload**: 2 GB test file (`large_2gb.bin`).
- **Procedure**:
  1. Start transfer on HW-AND-01 connected to office Wi-Fi.
  2. At 50% transfer progress (1 GB transferred), disable Wi-Fi on HW-AND-01 to force cellular 4G/5G handoff.
  3. Observe socket transition and WebRTC connection state.
- **Current Behavior (FAIL)**:
  - `PeerConnection` state enters `Disconnected`, then `Failed`. Transfer aborts permanently.
- **Target Pass Criteria (Post-Remediation of `DEF-ICE-03`)**:
  - `onStateChange(Disconnected)` triggers `restartIce()`.
  - New ICE candidates gathered and relayed over signaling within 5 seconds.
  - WebRTC DataChannel recovers.
  - Stream resumes from the last byte offset via `ResumeRequestMessage`.
  - Transfer completes to 100% with zero data corruption.

---

### Scenario TC-RW-05: Signaling Server Crash / Interruption Midway
- **Topologies**: Topology 2.
- **Devices**: HW-LIN-01 to HW-WIN-01 over WebRTC DataChannel.
- **Payload**: 3 GB file (`video_3gb.mp4`).
- **Procedure**:
  1. Establish P2P room connection via `signaling.lan-drop.test`.
  2. Once WebRTC DataChannel status is `open` and transfer starts, execute `kill -9` on the Go signaling server process on `SRV-SIG-01`.
  3. Observe ongoing DataChannel transfer.
- **Pass Criteria**:
  - Established DataChannel transfer continues uninterrupted.
  - Transfer runs to 100% completion and validates SHA-256 checksum.
  - Only room discovery and new room establishment are impacted while signaling is down.

---

### Scenario TC-RW-06: Disk Full / Storage Boundary Fault Injection
- **Topologies**: Topology 1.
- **Devices**: HW-LIN-01 (Sender) to HW-WIN-01 (Receiver).
- **Payload**: 10 GB file (`massive_10gb.iso`).
- **Procedure**:
  1. Create a 4 GB virtual disk partition mounted on HW-WIN-01 as the save folder.
  2. Initiate sending the 10 GB file to HW-WIN-01.
  3. Observe receiver behavior when disk space runs out.
- **Current Behavior (FAIL)**:
  - Unchecked `outFile.write()` continues loop; reports `"Integrity verification failed: SHA-256 mismatch"`.
- **Target Pass Criteria (Post-Remediation of `DEF-IO-06`)**:
  - Receiver checks `outFile.fail()` or pre-checks `std::filesystem::space`.
  - Immediately aborts with explicit error: `"Disk write failed: No space left on device"`.
  - Incomplete `.part` file is deleted or kept cleanly with `.part.meta`.

---

### Scenario TC-RW-07: Process Force-Kill & Crash Resumption
- **Topologies**: Topology 1.
- **Devices**: HW-LIN-01 (Sender) to HW-WIN-01 (Receiver).
- **Payload**: 4 GB file (`dataset_4gb.dat`).
- **Procedure**:
  1. Start transfer from HW-LIN-01 to HW-WIN-01.
  2. At 2 GB transferred, execute `kill -9` on the receiving process on HW-WIN-01.
  3. Re-launch `./build/lan_drop` on HW-WIN-01.
  4. Sender re-initiates transfer of `dataset_4gb.dat`.
- **Current Behavior (FAIL)**:
  - `TransferEngine` executes `std::filesystem::remove(partPath)` and restarts from byte 0.
- **Target Pass Criteria (Post-Remediation of `DEF-RES-04`)**:
  - Receiver detects existing `.part` and `.part.meta`.
  - Emits `ResumeRequestMessage` with `bytesReceivedSoFar = 2GB` and `partialSha256`.
  - Sender validates partial hash and resumes streaming from 2 GB.
  - Transfer completes in half the time without re-sending the first 2 GB.

---

### Scenario TC-RW-08: Web Receiver Large File Heap Stress (>2 GB)
- **Topologies**: Topology 2.
- **Devices**: HW-LIN-01 (Sender) to HW-MAC-01 (Safari 18, Receiver).
- **Payload**: 3 GB test file (`archive_3gb.tar`).
- **Procedure**:
  1. Open Web receiver on HW-MAC-01 in Safari.
  2. Send 3 GB file from HW-LIN-01.
  3. Monitor browser process memory via macOS Activity Monitor.
- **Current Behavior (FAIL)**:
  - In-memory `chunks.push()` array exceeds 3 GB; Safari tab crashes with memory error.
- **Target Pass Criteria (Post-Remediation of `DEF-WEB-02`)**:
  - Web client uses File System Access API (`createWritable()`) or OPFS.
  - Chunks stream directly to disk; browser heap memory remains <150 MB.
  - File successfully saved.

---

### Scenario TC-RW-09: Adversarial Filename & Path Traversal Fuzzing
- **Topologies**: Topology 1.
- **Devices**: Custom adversarial Python test script sending to HW-WIN-01 (Windows Receiver).
- **Payload Set**:
  1. `../../../../Windows/System32/drivers/etc/hosts`
  2. `C:\boot.ini`
  3. `CON.txt`, `PRN.dat`, `AUX.zip`, `NUL.iso`
  4. `CONIN$.txt`, `CONOUT$.dat`
  5. `legit_document\u202Ecod.exe` (Unicode RTLO)
  6. `test\x00file.png` (Null byte injection)
  7. `file:stream:$DATA` (Windows Alternate Data Stream)
- **Procedure**:
  Send WireProtocol frames with each adversarial filename payload to the receiver.
- **Pass Criteria**:
  - 100% of adversarial payloads are rejected before disk file handles are opened.
  - Error logged: `"Invalid or unsafe filename rejected"`.
  - No directory traversal, crash, or Windows device hang.

---

## 5. Verification Schedule & Exit Criteria

```
+-------------------------------------------------------------------------------+
|                        PHYSICAL QA VALIDATION ROADMAP                         |
|                                                                               |
|  Phase 1: Code-Level Blocker Remediation                                      |
|    - WireProtocolV2 unified across C++ and TypeScript                         |
|    - TURN_SECRET environment sync                                             |
|    - File System Access API / OPFS in Web Client                              |
|                                                                               |
|  Phase 2: Laboratory Test Harness Execution                                  |
|    - Execute TC-RW-01 (LAN Direct), TC-RW-02 (Web-Desktop), TC-RW-03 (TURN)   |
|    - Execute TC-RW-06 (Disk Full), TC-RW-09 (Security Fuzzing)                |
|                                                                               |
|  Phase 3: WAN & Roaming Validation                                            |
|    - Execute TC-RW-04 (Cellular Roaming / ICE Restart)                        |
|    - Execute TC-RW-05 (Signaling Fault Injection)                             |
|    - Execute TC-RW-07 (Crash Recovery & Resumption)                           |
|    - Execute TC-RW-08 (Web 3GB Heap Stress)                                   |
+-------------------------------------------------------------------------------+
```

### Official Acceptance Gate:
To certify LAN-Drop for Production v1.0.1:
1. **Zero Severity 1 (Critical) or Severity 2 (High) defects open**.
2. **Scenarios TC-RW-01 through TC-RW-09 must all achieve PASS status**.
3. **Written log traces with cryptographic SHA-256 verification hashes archived**.
