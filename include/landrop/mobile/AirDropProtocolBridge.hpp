#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include "landrop/utils/Types.hpp"

namespace landrop::mobile {

enum class AirDropState {
    Disabled,
    Advertising,
    Discovering,
    PromptingUser,
    Transferring,
    Completed,
    Failed
};

struct AirDropAsset {
    std::string fileName;
    uint64_t fileSize{0};
    std::string mimeType;
    std::string fileSha256;
};

class AirDropProtocolBridge {
public:
    explicit AirDropProtocolBridge(std::string deviceId = "landrop-apple-bridge-01", std::string displayName = "LAN-Drop Device");
    ~AirDropProtocolBridge() = default;

    bool startAdvertising(uint16_t port = 8770);
    void stopAdvertising();

    std::string generateBonjourRecord() const;
    std::string buildDiscoverResponse() const;
    std::string buildAskRequest(const AirDropAsset& asset) const;

    bool handleIncomingAsk(const std::string& requestPayload, AirDropAsset& outAsset);
    bool acceptIncomingTransfer();
    void rejectIncomingTransfer();

    AirDropState getState() const { return m_state; }
    uint16_t getPort() const { return m_port; }
    const std::string& getDeviceId() const { return m_deviceId; }
    const std::string& getDisplayName() const { return m_displayName; }

private:
    std::string m_deviceId;
    std::string m_displayName;
    uint16_t m_port{8770};
    AirDropState m_state{AirDropState::Disabled};
    AirDropAsset m_pendingAsset;
};

}
