#include "landrop/mobile/NfcPairingSession.hpp"
#include "landrop/utils/CryptoFallback.hpp"
#include <sstream>
#include <iomanip>
#include <random>

namespace landrop::mobile {

NfcPairingSession::NfcPairingSession(std::string localDeviceId, std::string localDeviceName)
    : m_localDeviceId(std::move(localDeviceId)),
      m_localDeviceName(std::move(localDeviceName)) {
    m_activePin = generateRandomHex(2);
    m_ephemeralKey = generateRandomHex(16);
}

std::string NfcPairingSession::generateRandomHex(size_t bytes) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, 255);

    std::ostringstream oss;
    for (size_t i = 0; i < bytes; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << dis(gen);
    }
    return oss.str();
}

std::string NfcPairingSession::generateNdefPayload(const std::string& localIp, uint16_t port) {
    std::ostringstream oss;
    oss << "LANDROP:NFC:v1;"
        << "id=" << m_localDeviceId << ";"
        << "name=" << m_localDeviceName << ";"
        << "ip=" << localIp << ";"
        << "port=" << port << ";"
        << "pin=" << m_activePin << ";"
        << "key=" << m_ephemeralKey;
    return oss.str();
}

std::optional<NfcPairingData> NfcPairingSession::parseNdefPayload(const std::string& rawPayload) {
    if (rawPayload.rfind("LANDROP:NFC:v1;", 0) != 0) {
        return std::nullopt;
    }

    NfcPairingData data;
    std::string content = rawPayload.substr(15);
    std::stringstream ss(content);
    std::string token;

    while (std::getline(ss, token, ';')) {
        auto eq = token.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = token.substr(0, eq);
        std::string val = token.substr(eq + 1);

        if (key == "id") data.deviceId = val;
        else if (key == "name") data.deviceName = val;
        else if (key == "ip") data.ipAddress = val;
        else if (key == "port") data.port = static_cast<uint16_t>(std::stoi(val));
        else if (key == "pin") data.sessionPin = val;
        else if (key == "key") data.ephemeralPublicKey = val;
    }

    if (data.deviceId.empty() || data.ipAddress.empty() || data.port == 0) {
        return std::nullopt;
    }

    m_isPaired = true;
    return data;
}

bool NfcPairingSession::verifyHandshake(const std::string& candidatePin) const {
    return candidatePin == m_activePin;
}

void NfcPairingSession::reset() {
    m_activePin = generateRandomHex(2);
    m_ephemeralKey = generateRandomHex(16);
    m_isPaired = false;
}

}
