#include "landrop/mobile/AirDropProtocolBridge.hpp"
#include <sstream>
#include <nlohmann/json.hpp>

namespace landrop::mobile {

AirDropProtocolBridge::AirDropProtocolBridge(std::string deviceId, std::string displayName)
    : m_deviceId(std::move(deviceId)),
      m_displayName(std::move(displayName)) {}

bool AirDropProtocolBridge::startAdvertising(uint16_t port) {
    m_port = port;
    m_state = AirDropState::Advertising;
    return true;
}

void AirDropProtocolBridge::stopAdvertising() {
    m_state = AirDropState::Disabled;
}

std::string AirDropProtocolBridge::generateBonjourRecord() const {
    std::ostringstream oss;
    oss << "_airdrop._tcp.local. TXT "
        << "flags=0x01,"
        << "phsh=1,"
        << "node=" << m_deviceId << ","
        << "model=MacBookPro18,1,"
        << "name=" << m_displayName << ","
        << "port=" << m_port;
    return oss.str();
}

std::string AirDropProtocolBridge::buildDiscoverResponse() const {
    nlohmann::json j;
    j["receiver-id"] = m_deviceId;
    j["name"] = m_displayName;
    j["capabilities"] = nlohmann::json::array({"chunk-transfer", "tls"});

    std::ostringstream oss;
    oss << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: application/json\r\n\r\n"
        << j.dump();
    return oss.str();
}

std::string AirDropProtocolBridge::buildAskRequest(const AirDropAsset& asset) const {
    nlohmann::json fileObj;
    fileObj["FileName"] = asset.fileName;
    fileObj["FileSize"] = asset.fileSize;
    fileObj["FileType"] = asset.mimeType;
    fileObj["FileHash"] = asset.fileSha256;

    nlohmann::json j;
    j["SenderID"] = m_deviceId;
    j["SenderName"] = m_displayName;
    j["Files"] = nlohmann::json::array({fileObj});

    std::ostringstream oss;
    oss << "POST /Discover HTTP/1.1\r\n"
        << "Content-Type: application/json\r\n\r\n"
        << j.dump();
    return oss.str();
}

bool AirDropProtocolBridge::handleIncomingAsk(const std::string& requestPayload, AirDropAsset& outAsset) {
    auto jsonStart = requestPayload.find('{');
    if (jsonStart == std::string::npos) {
        return false;
    }

    try {
        auto j = nlohmann::json::parse(requestPayload.substr(jsonStart));
        if (j.contains("Files") && j["Files"].is_array() && !j["Files"].empty()) {
            const auto& firstFile = j["Files"][0];
            outAsset.fileName = firstFile.value("FileName", "");
            outAsset.fileSize = firstFile.value("FileSize", 0ULL);
            outAsset.mimeType = firstFile.value("FileType", "application/octet-stream");
            outAsset.fileSha256 = firstFile.value("FileHash", "");

            if (outAsset.fileName.empty()) {
                return false;
            }

            m_pendingAsset = outAsset;
            m_state = AirDropState::PromptingUser;
            return true;
        }
    } catch (const std::exception&) {
        return false;
    }

    return false;
}

bool AirDropProtocolBridge::acceptIncomingTransfer() {
    if (m_state == AirDropState::PromptingUser) {
        m_state = AirDropState::Transferring;
        return true;
    }
    return false;
}

void AirDropProtocolBridge::rejectIncomingTransfer() {
    m_state = AirDropState::Advertising;
}

}
