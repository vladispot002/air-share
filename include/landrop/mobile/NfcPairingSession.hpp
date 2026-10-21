#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace landrop::mobile {

struct NfcPairingData {
    std::string deviceId;
    std::string deviceName;
    std::string ipAddress;
    uint16_t port{8848};
    std::string sessionPin;
    std::string ephemeralPublicKey;
    uint32_t protocolVersion{1};
};

class NfcPairingSession {
public:
    explicit NfcPairingSession(std::string localDeviceId = "mobile-dev-01", std::string localDeviceName = "Mobile Phone");
    ~NfcPairingSession() = default;

    std::string generateNdefPayload(const std::string& localIp, uint16_t port);
    std::optional<NfcPairingData> parseNdefPayload(const std::string& rawPayload);

    bool verifyHandshake(const std::string& candidatePin) const;
    const std::string& getActivePin() const { return m_activePin; }
    bool isPaired() const { return m_isPaired; }
    void reset();

private:
    std::string m_localDeviceId;
    std::string m_localDeviceName;
    std::string m_activePin;
    std::string m_ephemeralKey;
    bool m_isPaired{false};

    std::string generateRandomHex(size_t bytes);
};

}
