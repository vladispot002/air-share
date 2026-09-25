#pragma once

#include "NetworkPeer.hpp"

namespace landrop {

class LocalPeer : public NetworkPeer {
public:
    LocalPeer(
        std::string id,
        std::string displayName,
        std::string ipAddress,
        uint16_t port,
        std::string osName = "Linux",
        std::string deviceModel = "Generic Workstation",
        std::string subnetMask = "255.255.255.0",
        bool isDirectWifi = false
    );

    ~LocalPeer() override = default;

    bool initialize() override;
    std::string getPeerType() const override;
    std::string getEndpointInfo() const override;
    std::string getSummary() const override;
    bool ping() override;

    bool isSameSubnet(const std::string& targetIp) const;
    
    const std::string& getOsName() const { return m_osName; }
    void setOsName(std::string os) { m_osName = std::move(os); }

    const std::string& getDeviceModel() const { return m_deviceModel; }
    const std::string& getSubnetMask() const { return m_subnetMask; }
    
    bool isDirectWifi() const { return m_isDirectWifi; }
    void setDirectWifi(bool direct) { m_isDirectWifi = direct; }

    int getSignalStrengthRssi() const { return m_signalStrengthRssi; }
    void setSignalStrengthRssi(int rssi) { m_signalStrengthRssi = rssi; }

private:
    std::string m_osName;
    std::string m_deviceModel;
    std::string m_subnetMask;
    bool m_isDirectWifi{false};
    int m_signalStrengthRssi{-55};
};

}
