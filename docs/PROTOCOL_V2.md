# LAN-Drop Wire Protocol v2 Specification

## 1. Overview & Objectives

LAN-Drop Wire Protocol v2 is a binary, frame-based application-layer transport protocol designed to run over reliable bidirectional stream transports:
- Direct TCP Sockets (with optional TLS 1.3)
- WebRTC DataChannels (over SCTP/DTLS)
- Cloud Relay WebSockets / HTTP streams

### Primary Goals
1. **Explicit Framing**: Eliminate framing ambiguities by adopting a fixed 16-byte message frame header.
2. **Network Byte Order**: All multi-byte integer fields are encoded strictly in Big-Endian (Network Byte Order).
3. **Boundary & Oversize Protection**: Strict payload limits (maximum 16 MB per frame) and buffer validation to prevent memory exhaustion / DoS attacks.
4. **Data Integrity**: Per-frame CRC32 validation to detect transport-level corruption before passing buffers to application layers.
5. **Seamless Backward Compatibility**: Distinctive magic bytes (`LNDP`) enable automatic negotiation and transparent fallback to legacy v1 protocol.

---

## 2. Frame Architecture

Every transmission in Protocol v2 consists of a **16-byte fixed frame header** immediately followed by a variable-length **payload**.

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Magic ('LNDP')                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|        Version (0x0002)       |       Message Type (u16)      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      Payload Length (u32)                     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   Payload CRC32 / Sequence                    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                       Payload (0 .. N bytes)                  |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### 2.1 Header Fields

| Field | Size | Type | Endianness | Description |
|---|---|---|---|---|
| `magic` | 4 bytes | `uint8_t[4]` | N/A | Must be ASCII `'L', 'N', 'D', 'P'` (`0x4C4E4450`). |
| `version` | 2 bytes | `uint16_t` | Big-Endian | Protocol major/minor version. Currently `0x0002`. |
| `msgType` | 2 bytes | `uint16_t` | Big-Endian | Message identifier (see Section 3). |
| `payloadLength` | 4 bytes | `uint32_t` | Big-Endian | Size of subsequent payload in bytes (`0 <= N <= 16,777,216`). |
| `crc32OrSeq` | 4 bytes | `uint32_t` | Big-Endian | IEEE 802.3 CRC32 of payload (or frame sequence number). |

> [!NOTE]
> **Transport vs Cryptographic Integrity**:
> CRC-32 is utilized strictly as a lightweight transport integrity check to catch transmission bit-flips, network truncation, and framing errors before deserializing buffers. **CRC-32 is NOT a cryptographic checksum and does not replace authentication**. Cryptographic data integrity and non-repudiation are guaranteed by SHA-256 digests in `FILE_HEADER` and authenticated encryption provided by TLS 1.3 / DTLS 1.3 session keys.

---

## 3. Message Types

| ID | Name | Direction | Purpose |
|---|---|---|---|
| `0x0001` | `HANDSHAKE` | Bidirectional | Device exchange of capabilities, IDs, and app versions. |
| `0x0002` | `FILE_HEADER` | Sender -> Receiver | Initiates a file transfer with metadata and SHA-256 hash. |
| `0x0003` | `CHUNK_DATA` | Sender -> Receiver | Transmits a sliced chunk of file data. |
| `0x0004` | `TRANSFER_ACK` | Receiver -> Sender | Signals transfer acceptance, chunk receipt, or completion. |
| `0x0005` | `TRANSFER_CANCEL` | Either | Aborts an in-progress transfer with an error code and reason. |
| `0x0006` | `PING` | Either | Keep-alive heartbeat probe. |
| `0x0007` | `PONG` | Either | Response to keep-alive heartbeat probe. |

---

## 4. Message Payload Specifications

### 4.1 HANDSHAKE (`0x0001`)

Exchanged upon channel establishment:
- `flags` (`uint32_t`, BE): Capability bitmask (e.g. `0x01` = TLS, `0x02` = Compression, `0x04` = CloudRelay).
- `deviceIdLen` (`uint16_t`, BE) + `deviceId` (UTF-8).
- `deviceNameLen` (`uint16_t`, BE) + `deviceName` (UTF-8).
- `clientVersionLen` (`uint16_t`, BE) + `clientVersion` (UTF-8).

### 4.2 FILE_HEADER (`0x0002`)

Sent by initiator before streaming data:
- `fileSize` (`uint64_t`, BE): Total byte size of the target file.
- `chunkSize` (`uint32_t`, BE): Recommended chunk size (typically 64KB - 1MB).
- `flags` (`uint16_t`, BE): Optional transfer flags (e.g., encryption, compression).
- `fileNameLen` (`uint16_t`, BE) + `fileName` (UTF-8 string, max 255 bytes).
- `sha256Hex` (64 bytes ASCII): Expected SHA-256 digest of the entire file.

### 4.3 CHUNK_DATA (`0x0003`)

Streams file pieces sequentially or out-of-order:
- `offset` (`uint64_t`, BE): Starting byte position in target file.
- `chunkIndex` (`uint32_t`, BE): Monotonically increasing zero-indexed chunk counter.
- `chunkCrc32` (`uint32_t`, BE): IEEE 802.3 CRC32 of chunk payload bytes.
- `data` (`uint8_t[]`): Raw payload bytes of length `payloadLength - 16`.

### 4.4 TRANSFER_ACK (`0x0004`)

Sent by receiver to confirm status or completion:
- `status` (`uint8_t`):
  - `0x00` = `Success` (File accepted and verified).
  - `0x01` = `HashMismatch` (Integrity check failed).
  - `0x02` = `DiskFull` (Insufficient storage).
  - `0x03` = `Rejected` (User declined file).
  - `0x04` = `PathUnsafe` (Security filter rejected filename).
  - `0x05` = `GenericError`.
- `receivedBytes` (`uint64_t`, BE): Cumulative bytes received so far.
- `msgLen` (`uint16_t`, BE) + `message` (UTF-8 status/error description).

### 4.5 TRANSFER_CANCEL (`0x0005`)

Immediate termination of session:
- `errorCode` (`uint32_t`, BE): Numeric error identifier.
- `reasonLen` (`uint16_t`, BE) + `reason` (UTF-8 cancellation cause).

---

## 5. Backward Compatibility & Negotiation

LAN-Drop v1 transfers begin directly with `nameLen` as a 4-byte unsigned integer. Because filenames are constrained to 255 bytes, on little-endian systems (x86_64/ARM64) the first 4 bytes of a v1 connection are `[len, 0x00, 0x00, 0x00]`.

In Protocol v2:
- The first 4 bytes are `'L', 'N', 'D', 'P'` (`0x4C, 0x4E, 0x44, 0x50`).
- If read by a v1 parser as a 32-bit little-endian integer, `'LNDP'` evaluates to `1,346,653,772 > 255`, causing an immediate, safe rejection rather than memory corruption.
- When a v2 receiver accepts an incoming connection, it peeks or reads the first 4 bytes:
  1. If `magic == "LNDP"`, it proceeds with the v2 state machine.
  2. If `magic != "LNDP"` and `uint32_t(first 4 bytes) <= 255`, the receiver transparently routes the stream to the legacy v1 transfer parser.

This ensures existing v1.0.0 and v1.0.1 installations continue to function with newer v2 servers.

### 5.1 Downgrade Attack Prevention Policy
To prevent a malicious adversary on the local network from forcing peers to downgrade from authenticated Protocol v2 to legacy v1:
1. **Capability Pinning**: During multicast/mDNS beacon discovery, v2-capable peers publish a capability flag (`CAP_PROTOCOL_V2 = 0x0001`). When connecting to a known v2-advertising peer, clients enforce `requireProtocolV2 = true`. If the handshake does not supply `'LNDP'`, the transfer is aborted immediately with `TRANSFER_CANCEL (ERR_DOWNGRADE_ATTACK_REJECTED)`.
2. **Strict Mode Configuration**: Endpoints operating in hardened environments or with `useTls = true` can set `allowLegacyV1Fallback = false`. In strict mode, any connection lacking the `'LNDP'` header is rejected without processing.
