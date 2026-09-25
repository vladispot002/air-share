#include "landrop/peers/NetworkPeer.hpp"
#include <utility>

namespace landrop {

NetworkPeer::NetworkPeer(
    std::string id,
    std::string displayName,
    std::string ipAddress,
    uint16_t port,
    uint32_t protocolVersion
) : m_id(std::move(id)),
    m_displayName(std::move(displayName)),
    m_ipAddress(std::move(ipAddress)),
    m_port(port),
    m_protocolVersion(protocolVersion),
    m_lastSeen(std::chrono::system_clock::now()) {}

std::string NetworkPeer::getSummary() const {
    return "[" + getPeerType() + "] " + m_displayName + " (" + getEndpointInfo() + ")";
}

void NetworkPeer::updateLastSeen() {
    m_lastSeen = std::chrono::system_clock::now();
}

int64_t NetworkPeer::getSecondsSinceLastSeen() const {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now - m_lastSeen).count();
}

std::ostream& operator<<(std::ostream& os, const NetworkPeer& peer) {
    os << peer.getSummary() << " | Online: " << (peer.isOnline() ? "true" : "false");
    return os;
}

}
