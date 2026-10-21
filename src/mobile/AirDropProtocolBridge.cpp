#include "landrop/mobile/AirDropProtocolBridge.hpp"
#include <sstream>

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
    std::ostringstream oss;
    oss << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: application/json\r\n\r\n"
        << "{\"receiver-id\":\"" << m_deviceId << "\","
        << "\"name\":\"" << m_displayName << "\","
        << "\"capabilities\":[\"chunk-transfer\",\"tls\"]}";
    return oss.str();
}

std::string AirDropProtocolBridge::buildAskRequest(const AirDropAsset& asset) const {
    std::ostringstream oss;
    oss << "POST /Discover HTTP/1.1\r\n"
        << "Content-Type: application/json\r\n\r\n"
        << "{\"SenderID\":\"" << m_deviceId << "\","
        << "\"SenderName\":\"" << m_displayName << "\","
        << "\"Files\":[{\"FileName\":\"" << asset.fileName << "\","
        << "\"FileSize\":" << asset.fileSize << ","
        << "\"FileType\":\"" << asset.mimeType << "\","
        << "\"FileHash\":\"" << asset.fileSha256 << "\"}]}";
    return oss.str();
}

bool AirDropProtocolBridge::handleIncomingAsk(const std::string& requestPayload, AirDropAsset& outAsset) {
    auto posName = requestPayload.find("\"FileName\":\"");
    if (posName == std::string::npos) {
        return false;
    }
    posName += 12;
    auto endName = requestPayload.find('\"', posName);
    if (endName == std::string::npos) {
        return false;
    }
    outAsset.fileName = requestPayload.substr(posName, endName - posName);

    auto posSize = requestPayload.find("\"FileSize\":");
    if (posSize != std::string::npos) {
        posSize += 11;
        auto endSize = requestPayload.find_first_of(",}\r\n", posSize);
        if (endSize != std::string::npos) {
            outAsset.fileSize = static_cast<uint64_t>(std::stoull(requestPayload.substr(posSize, endSize - posSize)));
        }
    }

    outAsset.mimeType = "application/octet-stream";
    m_pendingAsset = outAsset;
    m_state = AirDropState::PromptingUser;
    return true;
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
