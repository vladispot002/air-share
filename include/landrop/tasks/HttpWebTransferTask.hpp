#pragma once

#include "TransferTask.hpp"

namespace landrop {

class HttpWebTransferTask : public TransferTask {
public:
    HttpWebTransferTask(
        std::string taskId,
        std::string fileName,
        uint64_t fileSize,
        TransferDirection direction,
        uint16_t httpPort = 8080,
        std::string webToken = "landrop-token-auto",
        uint32_t maxDownloadCount = 5
    );

    ~HttpWebTransferTask() override = default;

    bool initialize() override;
    void processChunk(size_t chunkIndex, const std::vector<uint8_t>& data) override;
    std::string getTaskType() const override;

    std::string generateShareUrl(const std::string& hostIp) const;
    std::string generateQrCodePayload(const std::string& hostIp) const;

    bool validateAccessToken(const std::string& token) const;
    bool registerDownload();

    uint16_t getHttpPort() const { return m_httpPort; }
    const std::string& getWebToken() const { return m_webToken; }
    
    uint32_t getCurrentDownloads() const { return m_currentDownloadCount; }
    uint32_t getMaxDownloads() const { return m_maxDownloadCount; }
    bool isExpired() const;

    void setSessionPin(std::string pin) { m_sessionPin = std::move(pin); }
    const std::string& getSessionPin() const { return m_sessionPin; }

private:
    std::string m_webToken;
    uint16_t m_httpPort;
    uint32_t m_maxDownloadCount;
    uint32_t m_currentDownloadCount{0};
    std::string m_sessionPin{"4242"};
};

}
