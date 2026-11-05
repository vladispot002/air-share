#include "landrop/core/WireProtocolV2.hpp"
#include <array>
#include <stdexcept>

namespace landrop::protocol::v2 {

// CRC-32 table generator (IEEE 802.3)
static const std::array<uint32_t, 256>& getCrc32Table() {
    static const auto table = []() {
        std::array<uint32_t, 256> tbl{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (size_t j = 0; j < 8; ++j) {
                c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
            }
            tbl[i] = c;
        }
        return tbl;
    }();
    return table;
}

uint32_t computeCrc32(const uint8_t* data, size_t length) {
    if (!data || length == 0) return 0;
    const auto& table = getCrc32Table();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

void FrameHeader::serialize(uint8_t out[FRAME_HEADER_SIZE]) const {
    std::memcpy(out, magic, 4);
    uint16_t beVersion = toBigEndian16(version);
    std::memcpy(out + 4, &beVersion, sizeof(uint16_t));
    uint16_t beType = toBigEndian16(static_cast<uint16_t>(msgType));
    std::memcpy(out + 6, &beType, sizeof(uint16_t));
    uint32_t beLen = toBigEndian32(payloadLength);
    std::memcpy(out + 8, &beLen, sizeof(uint32_t));
    uint32_t beCrc = toBigEndian32(crc32OrSeq);
    std::memcpy(out + 12, &beCrc, sizeof(uint32_t));
}

bool FrameHeader::deserialize(const uint8_t in[FRAME_HEADER_SIZE], FrameHeader& outHdr, std::string* outError) {
    if (!isProtocolV2(in)) {
        if (outError) *outError = "Invalid Protocol v2 magic header";
        return false;
    }
    std::memcpy(outHdr.magic, in, 4);

    uint16_t beVersion = 0;
    std::memcpy(&beVersion, in + 4, sizeof(uint16_t));
    outHdr.version = fromBigEndian16(beVersion);
    if (outHdr.version != PROTOCOL_VERSION) {
        if (outError) *outError = "Unsupported protocol version: " + std::to_string(outHdr.version);
        return false;
    }

    uint16_t beType = 0;
    std::memcpy(&beType, in + 6, sizeof(uint16_t));
    outHdr.msgType = static_cast<MessageType>(fromBigEndian16(beType));

    uint32_t beLen = 0;
    std::memcpy(&beLen, in + 8, sizeof(uint32_t));
    outHdr.payloadLength = fromBigEndian32(beLen);
    if (outHdr.payloadLength > MAX_PAYLOAD_SIZE) {
        if (outError) *outError = "Payload exceeds maximum allowed frame size: " + std::to_string(outHdr.payloadLength);
        return false;
    }

    uint32_t beCrc = 0;
    std::memcpy(&beCrc, in + 12, sizeof(uint32_t));
    outHdr.crc32OrSeq = fromBigEndian32(beCrc);

    return true;
}

// HandshakeMessage
std::vector<uint8_t> HandshakeMessage::serialize() const {
    size_t total = sizeof(uint32_t) +
                   sizeof(uint16_t) + deviceId.size() +
                   sizeof(uint16_t) + deviceName.size() +
                   sizeof(uint16_t) + clientVersion.size();
    std::vector<uint8_t> buf(total);
    uint8_t* ptr = buf.data();

    uint32_t beCaps = toBigEndian32(capabilitiesFlags);
    std::memcpy(ptr, &beCaps, sizeof(uint32_t)); ptr += sizeof(uint32_t);

    uint16_t idLen = toBigEndian16(static_cast<uint16_t>(deviceId.size()));
    std::memcpy(ptr, &idLen, sizeof(uint16_t)); ptr += sizeof(uint16_t);
    std::memcpy(ptr, deviceId.data(), deviceId.size()); ptr += deviceId.size();

    uint16_t nameLen = toBigEndian16(static_cast<uint16_t>(deviceName.size()));
    std::memcpy(ptr, &nameLen, sizeof(uint16_t)); ptr += sizeof(uint16_t);
    std::memcpy(ptr, deviceName.data(), deviceName.size()); ptr += deviceName.size();

    uint16_t verLen = toBigEndian16(static_cast<uint16_t>(clientVersion.size()));
    std::memcpy(ptr, &verLen, sizeof(uint16_t)); ptr += sizeof(uint16_t);
    std::memcpy(ptr, clientVersion.data(), clientVersion.size());

    return buf;
}

bool HandshakeMessage::deserialize(const uint8_t* data, size_t size, HandshakeMessage& outMsg, std::string* outError) {
    if (!data || size < sizeof(uint32_t) + 3 * sizeof(uint16_t)) {
        if (outError) *outError = "Buffer underflow parsing HandshakeMessage";
        return false;
    }
    const uint8_t* ptr = data;
    const uint8_t* end = data + size;

    uint32_t beCaps = 0;
    std::memcpy(&beCaps, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);
    outMsg.capabilitiesFlags = fromBigEndian32(beCaps);

    auto readString = [&](std::string& str) -> bool {
        if (ptr + sizeof(uint16_t) > end) return false;
        uint16_t beLen = 0;
        std::memcpy(&beLen, ptr, sizeof(uint16_t)); ptr += sizeof(uint16_t);
        uint16_t len = fromBigEndian16(beLen);
        if (ptr + len > end) return false;
        str.assign(reinterpret_cast<const char*>(ptr), len);
        ptr += len;
        return true;
    };

    if (!readString(outMsg.deviceId) ||
        !readString(outMsg.deviceName) ||
        !readString(outMsg.clientVersion)) {
        if (outError) *outError = "Truncated string field in HandshakeMessage";
        return false;
    }

    return true;
}

// FileHeaderMessage
std::vector<uint8_t> FileHeaderMessage::serialize() const {
    size_t total = sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint16_t) +
                   sizeof(uint16_t) + fileName.size() +
                   64; // SHA-256 hex string
    std::vector<uint8_t> buf(total);
    uint8_t* ptr = buf.data();

    uint64_t beSize = toBigEndian64(fileSize);
    std::memcpy(ptr, &beSize, sizeof(uint64_t)); ptr += sizeof(uint64_t);

    uint32_t beChunk = toBigEndian32(chunkSize);
    std::memcpy(ptr, &beChunk, sizeof(uint32_t)); ptr += sizeof(uint32_t);

    uint16_t beFlags = toBigEndian16(flags);
    std::memcpy(ptr, &beFlags, sizeof(uint16_t)); ptr += sizeof(uint16_t);

    uint16_t nameLen = static_cast<uint16_t>(fileName.size());
    uint16_t beNameLen = toBigEndian16(nameLen);
    std::memcpy(ptr, &beNameLen, sizeof(uint16_t)); ptr += sizeof(uint16_t);
    std::memcpy(ptr, fileName.data(), nameLen); ptr += nameLen;

    std::string sha = sha256Hex;
    sha.resize(64, '0');
    std::memcpy(ptr, sha.data(), 64);

    return buf;
}

bool FileHeaderMessage::deserialize(const uint8_t* data, size_t size, FileHeaderMessage& outMsg, std::string* outError) {
    if (!data || size < sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t) + 64) {
        if (outError) *outError = "Buffer underflow parsing FileHeaderMessage";
        return false;
    }
    const uint8_t* ptr = data;
    const uint8_t* end = data + size;

    uint64_t beSize = 0;
    std::memcpy(&beSize, ptr, sizeof(uint64_t)); ptr += sizeof(uint64_t);
    outMsg.fileSize = fromBigEndian64(beSize);

    uint32_t beChunk = 0;
    std::memcpy(&beChunk, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);
    outMsg.chunkSize = fromBigEndian32(beChunk);

    uint16_t beFlags = 0;
    std::memcpy(&beFlags, ptr, sizeof(uint16_t)); ptr += sizeof(uint16_t);
    outMsg.flags = fromBigEndian16(beFlags);

    uint16_t beNameLen = 0;
    std::memcpy(&beNameLen, ptr, sizeof(uint16_t)); ptr += sizeof(uint16_t);
    uint16_t nameLen = fromBigEndian16(beNameLen);

    if (nameLen == 0 || nameLen > MAX_FILENAME_LENGTH) {
        if (outError) *outError = "Invalid filename length in FileHeaderMessage: " + std::to_string(nameLen);
        return false;
    }

    if (ptr + nameLen + 64 > end) {
        if (outError) *outError = "Truncated payload in FileHeaderMessage";
        return false;
    }

    outMsg.fileName.assign(reinterpret_cast<const char*>(ptr), nameLen);
    ptr += nameLen;

    outMsg.sha256Hex.assign(reinterpret_cast<const char*>(ptr), 64);

    return true;
}

// ChunkDataMessage
std::vector<uint8_t> ChunkDataMessage::serialize() const {
    size_t total = sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint32_t) + data.size();
    std::vector<uint8_t> buf(total);
    uint8_t* ptr = buf.data();

    uint64_t beOffset = toBigEndian64(offset);
    std::memcpy(ptr, &beOffset, sizeof(uint64_t)); ptr += sizeof(uint64_t);

    uint32_t beIndex = toBigEndian32(chunkIndex);
    std::memcpy(ptr, &beIndex, sizeof(uint32_t)); ptr += sizeof(uint32_t);

    uint32_t crc = chunkCrc32 != 0 ? chunkCrc32 : computeCrc32(data.data(), data.size());
    uint32_t beCrc = toBigEndian32(crc);
    std::memcpy(ptr, &beCrc, sizeof(uint32_t)); ptr += sizeof(uint32_t);

    if (!data.empty()) {
        std::memcpy(ptr, data.data(), data.size());
    }

    return buf;
}

bool ChunkDataMessage::deserialize(const uint8_t* data, size_t size, ChunkDataMessage& outMsg, std::string* outError) {
    if (!data || size < sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint32_t)) {
        if (outError) *outError = "Buffer underflow parsing ChunkDataMessage";
        return false;
    }
    const uint8_t* ptr = data;

    uint64_t beOffset = 0;
    std::memcpy(&beOffset, ptr, sizeof(uint64_t)); ptr += sizeof(uint64_t);
    outMsg.offset = fromBigEndian64(beOffset);

    uint32_t beIndex = 0;
    std::memcpy(&beIndex, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);
    outMsg.chunkIndex = fromBigEndian32(beIndex);

    uint32_t beCrc = 0;
    std::memcpy(&beCrc, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);
    outMsg.chunkCrc32 = fromBigEndian32(beCrc);

    size_t dataLen = size - (sizeof(uint64_t) + 2 * sizeof(uint32_t));
    outMsg.data.resize(dataLen);
    if (dataLen > 0) {
        std::memcpy(outMsg.data.data(), ptr, dataLen);
        uint32_t actualCrc = computeCrc32(outMsg.data.data(), dataLen);
        if (outMsg.chunkCrc32 != 0 && actualCrc != outMsg.chunkCrc32) {
            if (outError) *outError = "Chunk CRC-32 checksum mismatch: expected " +
                                      std::to_string(outMsg.chunkCrc32) + ", got " + std::to_string(actualCrc);
            return false;
        }
    }

    return true;
}

// TransferAckMessage
std::vector<uint8_t> TransferAckMessage::serialize() const {
    size_t total = sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint16_t) + message.size();
    std::vector<uint8_t> buf(total);
    uint8_t* ptr = buf.data();

    *ptr = static_cast<uint8_t>(status); ptr += sizeof(uint8_t);

    uint64_t beReceived = toBigEndian64(receivedBytes);
    std::memcpy(ptr, &beReceived, sizeof(uint64_t)); ptr += sizeof(uint64_t);

    uint16_t beMsgLen = toBigEndian16(static_cast<uint16_t>(message.size()));
    std::memcpy(ptr, &beMsgLen, sizeof(uint16_t)); ptr += sizeof(uint16_t);

    if (!message.empty()) {
        std::memcpy(ptr, message.data(), message.size());
    }

    return buf;
}

bool TransferAckMessage::deserialize(const uint8_t* data, size_t size, TransferAckMessage& outMsg, std::string* outError) {
    if (!data || size < sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint16_t)) {
        if (outError) *outError = "Buffer underflow parsing TransferAckMessage";
        return false;
    }
    const uint8_t* ptr = data;
    const uint8_t* end = data + size;

    outMsg.status = static_cast<AckStatus>(*ptr); ptr += sizeof(uint8_t);

    uint64_t beReceived = 0;
    std::memcpy(&beReceived, ptr, sizeof(uint64_t)); ptr += sizeof(uint64_t);
    outMsg.receivedBytes = fromBigEndian64(beReceived);

    uint16_t beMsgLen = 0;
    std::memcpy(&beMsgLen, ptr, sizeof(uint16_t)); ptr += sizeof(uint16_t);
    uint16_t msgLen = fromBigEndian16(beMsgLen);

    if (ptr + msgLen > end) {
        if (outError) *outError = "Truncated message in TransferAckMessage";
        return false;
    }

    outMsg.message.assign(reinterpret_cast<const char*>(ptr), msgLen);
    return true;
}

// TransferCancelMessage
std::vector<uint8_t> TransferCancelMessage::serialize() const {
    size_t total = sizeof(uint32_t) + sizeof(uint16_t) + reason.size();
    std::vector<uint8_t> buf(total);
    uint8_t* ptr = buf.data();

    uint32_t beErr = toBigEndian32(errorCode);
    std::memcpy(ptr, &beErr, sizeof(uint32_t)); ptr += sizeof(uint32_t);

    uint16_t beLen = toBigEndian16(static_cast<uint16_t>(reason.size()));
    std::memcpy(ptr, &beLen, sizeof(uint16_t)); ptr += sizeof(uint16_t);

    if (!reason.empty()) {
        std::memcpy(ptr, reason.data(), reason.size());
    }

    return buf;
}

bool TransferCancelMessage::deserialize(const uint8_t* data, size_t size, TransferCancelMessage& outMsg, std::string* outError) {
    if (!data || size < sizeof(uint32_t) + sizeof(uint16_t)) {
        if (outError) *outError = "Buffer underflow parsing TransferCancelMessage";
        return false;
    }
    const uint8_t* ptr = data;
    const uint8_t* end = data + size;

    uint32_t beErr = 0;
    std::memcpy(&beErr, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);
    outMsg.errorCode = fromBigEndian32(beErr);

    uint16_t beLen = 0;
    std::memcpy(&beLen, ptr, sizeof(uint16_t)); ptr += sizeof(uint16_t);
    uint16_t len = fromBigEndian16(beLen);

    if (ptr + len > end) {
        if (outError) *outError = "Truncated reason in TransferCancelMessage";
        return false;
    }

    outMsg.reason.assign(reinterpret_cast<const char*>(ptr), len);
    return true;
}

std::vector<uint8_t> buildFrame(MessageType type, const std::vector<uint8_t>& payload, uint32_t crcOrSeq) {
    FrameHeader hdr;
    hdr.version = PROTOCOL_VERSION;
    hdr.msgType = type;
    hdr.payloadLength = static_cast<uint32_t>(payload.size());
    hdr.crc32OrSeq = crcOrSeq != 0 ? crcOrSeq : computeCrc32(payload.data(), payload.size());

    std::vector<uint8_t> frame(FRAME_HEADER_SIZE + payload.size());
    hdr.serialize(frame.data());
    if (!payload.empty()) {
        std::memcpy(frame.data() + FRAME_HEADER_SIZE, payload.data(), payload.size());
    }
    return frame;
}

} // namespace landrop::protocol::v2
