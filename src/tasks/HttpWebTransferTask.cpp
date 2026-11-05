#include "landrop/tasks/HttpWebTransferTask.hpp"
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <utility>
#include <vector>
#include <cstdio>

namespace landrop {

static std::string generateSecureRandomPin() {
    uint32_t val = 0;
    if (RAND_bytes(reinterpret_cast<unsigned char*>(&val), sizeof(val)) == 1) {
        val = val % 1000000;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%06u", val);
        return std::string(buf);
    }
    return "000000";
}

static std::string generateSecureRandomToken(size_t numBytes = 16) {
    std::vector<unsigned char> buf(numBytes);
    if (RAND_bytes(buf.data(), static_cast<int>(numBytes)) == 1) {
        std::string hex;
        hex.reserve(numBytes * 2);
        static const char hexChars[] = "0123456789abcdef";
        for (unsigned char b : buf) {
            hex.push_back(hexChars[(b >> 4) & 0x0F]);
            hex.push_back(hexChars[b & 0x0F]);
        }
        return hex;
    }
    return "token-fallback";
}

HttpWebTransferTask::HttpWebTransferTask(
    std::string taskId,
    std::string fileName,
    uint64_t fileSize,
    TransferDirection direction,
    uint16_t httpPort,
    std::string webToken,
    uint32_t maxDownloadCount,
    std::string sessionPin
) : TransferTask(std::move(taskId), std::move(fileName), fileSize, direction),
    m_webToken(webToken.empty() ? generateSecureRandomToken() : std::move(webToken)),
    m_httpPort(httpPort),
    m_maxDownloadCount(maxDownloadCount),
    m_sessionPin(sessionPin.empty() ? generateSecureRandomPin() : std::move(sessionPin)) {}

bool HttpWebTransferTask::initialize() {
    m_state = TransferState::Transferring;
    m_currentDownloadCount = 0;
    return true;
}

void HttpWebTransferTask::processChunk(size_t /*chunkIndex*/, const std::vector<uint8_t>& data) {
    m_bytesTransferred += data.size();
    if (m_bytesTransferred >= m_fileSize) {
        m_state = TransferState::Completed;
    }
}

std::string HttpWebTransferTask::getTaskType() const {
    return "HttpWebTransferTask";
}

std::string HttpWebTransferTask::generateShareUrl(const std::string& hostIp) const {
    return "http://" + hostIp + ":" + std::to_string(m_httpPort) + "/download?token=" + m_webToken;
}

std::string HttpWebTransferTask::generateQrCodePayload(const std::string& hostIp) const {
    return "WIFI-DROP:URL=" + generateShareUrl(hostIp) + ";PIN=" + m_sessionPin;
}

bool HttpWebTransferTask::validateAccessToken(const std::string& token) const {
    if (m_webToken.empty() || m_webToken.size() != token.size()) {
        return false;
    }
    return CRYPTO_memcmp(m_webToken.data(), token.data(), m_webToken.size()) == 0;
}

bool HttpWebTransferTask::registerDownload() {
    if (isExpired()) {
        return false;
    }
    ++m_currentDownloadCount;
    if (m_currentDownloadCount >= m_maxDownloadCount) {
        m_state = TransferState::Completed;
    }
    return true;
}

bool HttpWebTransferTask::isExpired() const {
    return m_currentDownloadCount >= m_maxDownloadCount || m_state == TransferState::Cancelled;
}

}
