#include "landrop/peers/LocalPeer.hpp"
#include <utility>

namespace landrop {

LocalPeer::LocalPeer(
    std::string id,
    std::string displayName,
    std::string ipAddress,
    uint16_t port,
    std::string osName,
    std::string deviceModel,
    std::string subnetMask,
    bool isDirectWifi
) : NetworkPeer(std::move(id), std::move(displayName), std::move(ipAddress), port),
    m_osName(std::move(osName)),
    m_deviceModel(std::move(deviceModel)),
    m_subnetMask(std::move(subnetMask)),
    m_isDirectWifi(isDirectWifi) {}

bool LocalPeer::initialize() {
    m_isOnline = true;
    updateLastSeen();
    return true;
}

std::string LocalPeer::getPeerType() const {
    return "LocalPeer";
}

std::string LocalPeer::getEndpointInfo() const {
    return m_ipAddress + ":" + std::to_string(m_port);
}

std::string LocalPeer::getSummary() const {
    return NetworkPeer::getSummary() + " | OS: " + m_osName + " | RSSI: " + std::to_string(m_signalStrengthRssi) + "dBm";
}

bool LocalPeer::ping() {
    updateLastSeen();
    return m_isOnline;
}

bool LocalPeer::isSameSubnet(const std::string& targetIp) const {
    auto pos1 = m_ipAddress.rfind('.');
    auto pos2 = targetIp.rfind('.');
    if (pos1 == std::string::npos || pos2 == std::string::npos) {
        return false;
    }
    return m_ipAddress.substr(0, pos1) == targetIp.substr(0, pos2);
}

}
