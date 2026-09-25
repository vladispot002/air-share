#include "landrop/tasks/HttpWebTransferTask.hpp"
#include <utility>

namespace landrop {

HttpWebTransferTask::HttpWebTransferTask(
    std::string taskId,
    std::string fileName,
    uint64_t fileSize,
    TransferDirection direction,
    uint16_t httpPort,
    std::string webToken,
    uint32_t maxDownloadCount
) : TransferTask(std::move(taskId), std::move(fileName), fileSize, direction),
    m_webToken(std::move(webToken)),
    m_httpPort(httpPort),
    m_maxDownloadCount(maxDownloadCount) {}

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
    return m_webToken == token;
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
