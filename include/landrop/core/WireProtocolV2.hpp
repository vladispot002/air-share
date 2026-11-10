#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cstring>
#include "landrop/utils/SocketCompat.hpp"

namespace landrop::protocol::v2 {

constexpr uint8_t MAGIC[4] = {'L', 'N', 'D', 'P'};
constexpr uint32_t MAGIC_U32 = 0x4C4E4450; // Big-endian 'LNDP'
constexpr uint16_t PROTOCOL_VERSION = 2;
constexpr size_t FRAME_HEADER_SIZE = 16;
constexpr size_t MAX_PAYLOAD_SIZE = 16 * 1024 * 1024; // 16 MB boundary protection
constexpr size_t MAX_FILENAME_LENGTH = 255;

enum class MessageType : uint16_t {
    Handshake      = 0x0001,
    FileHeader     = 0x0002,
    ChunkData      = 0x0003,
    TransferAck    = 0x0004,
    TransferCancel = 0x0005,
    Ping           = 0x0006,
    Pong           = 0x0007,
    ResumeRequest  = 0x0008
};

enum class AckStatus : uint8_t {
    Success        = 0x00,
    HashMismatch   = 0x01,
    DiskFull       = 0x02,
    Rejected       = 0x03,
    PathUnsafe     = 0x04,
    GenericError   = 0x05,
    ResumeAccepted = 0x06,
    ResumeRejected = 0x07
};

namespace FileHeaderFlags {
    constexpr uint16_t NONE             = 0x0000;
    constexpr uint16_t RESUME_SUPPORTED = 0x0001;
}

// Endian conversion utilities
inline uint16_t toBigEndian16(uint16_t val) {
    return htons(val);
}

inline uint16_t fromBigEndian16(uint16_t val) {
    return ntohs(val);
}

inline uint32_t toBigEndian32(uint32_t val) {
    return htonl(val);
}

inline uint32_t fromBigEndian32(uint32_t val) {
    return ntohl(val);
}

inline uint64_t toBigEndian64(uint64_t val) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return val;
#else
    return ((static_cast<uint64_t>(htonl(static_cast<uint32_t>(val & 0xFFFFFFFFULL)))) << 32) |
           htonl(static_cast<uint32_t>(val >> 32));
#endif
}

inline uint64_t fromBigEndian64(uint64_t val) {
    return toBigEndian64(val);
}

// IEEE 802.3 CRC-32 computation
uint32_t computeCrc32(const uint8_t* data, size_t length);

// Check if first 4 incoming bytes match 'LNDP'
inline bool isProtocolV2(const uint8_t first4Bytes[4]) {
    return first4Bytes[0] == MAGIC[0] &&
           first4Bytes[1] == MAGIC[1] &&
           first4Bytes[2] == MAGIC[2] &&
           first4Bytes[3] == MAGIC[3];
}

// Fixed 16-byte frame header
struct FrameHeader {
    uint8_t magic[4]{'L', 'N', 'D', 'P'};
    uint16_t version{PROTOCOL_VERSION};
    MessageType msgType{MessageType::Ping};
    uint32_t payloadLength{0};
    uint32_t crc32OrSeq{0};

    void serialize(uint8_t out[FRAME_HEADER_SIZE]) const;
    static bool deserialize(const uint8_t in[FRAME_HEADER_SIZE], FrameHeader& outHdr, std::string* outError = nullptr);
};

// Handshake payload (0x0001)
struct HandshakeMessage {
    uint32_t capabilitiesFlags{0};
    std::string deviceId;
    std::string deviceName;
    std::string clientVersion;

    std::vector<uint8_t> serialize() const;
    static bool deserialize(const uint8_t* data, size_t size, HandshakeMessage& outMsg, std::string* outError = nullptr);
};

// File Header payload (0x0002)
struct FileHeaderMessage {
    uint64_t fileSize{0};
    uint32_t chunkSize{64 * 1024};
    uint16_t flags{0};
    std::string fileName;
    std::string sha256Hex; // 64-character hex digest

    std::vector<uint8_t> serialize() const;
    static bool deserialize(const uint8_t* data, size_t size, FileHeaderMessage& outMsg, std::string* outError = nullptr);
};

// Chunk Data payload (0x0003)
struct ChunkDataMessage {
    uint64_t offset{0};
    uint32_t chunkIndex{0};
    uint32_t chunkCrc32{0};
    std::vector<uint8_t> data;

    std::vector<uint8_t> serialize() const;
    static bool deserialize(const uint8_t* data, size_t size, ChunkDataMessage& outMsg, std::string* outError = nullptr);
};

// Transfer Ack payload (0x0004)
struct TransferAckMessage {
    AckStatus status{AckStatus::Success};
    uint64_t receivedBytes{0};
    std::string message;

    std::vector<uint8_t> serialize() const;
    static bool deserialize(const uint8_t* data, size_t size, TransferAckMessage& outMsg, std::string* outError = nullptr);
};

// Transfer Cancel payload (0x0005)
struct TransferCancelMessage {
    uint32_t errorCode{0};
    std::string reason;

    std::vector<uint8_t> serialize() const;
    static bool deserialize(const uint8_t* data, size_t size, TransferCancelMessage& outMsg, std::string* outError = nullptr);
};

// Resume Request payload (0x0008, fixed 136 bytes)
struct ResumeRequestMessage {
    std::string fileHash;           // 64-char SHA-256 hex digest
    uint64_t bytesReceivedSoFar{0}; // Verified byte offset
    std::string partialSha256;      // SHA-256 hex digest of [0 .. bytesReceivedSoFar)

    std::vector<uint8_t> serialize() const;
    static bool deserialize(const uint8_t* data, size_t size, ResumeRequestMessage& outMsg, std::string* outError = nullptr);
};

// Helper to construct a complete wire frame (header + payload)
std::vector<uint8_t> buildFrame(MessageType type, const std::vector<uint8_t>& payload, uint32_t crcOrSeq = 0);

} // namespace landrop::protocol::v2
