#pragma once

#include <string>
#include <cstdint>
#include <chrono>
#include <iostream>
#include <vector>
#include <memory>
#include "landrop/utils/Types.hpp"

namespace landrop {

class NetworkPeer {
public:
    NetworkPeer(
        std::string id,
        std::string displayName,
        std::string ipAddress,
        uint16_t port,
        uint32_t protocolVersion = 1
    );

    virtual ~NetworkPeer() = default;

    virtual bool initialize() = 0;
    virtual std::string getPeerType() const = 0;
    virtual std::string getEndpointInfo() const = 0;
    virtual bool ping() = 0;
    virtual std::string getSummary() const;

    bool operator==(const NetworkPeer& other) const {
        return m_id == other.m_id;
    }

    bool operator!=(const NetworkPeer& other) const {
        return !(*this == other);
    }

    friend std::ostream& operator<<(std::ostream& os, const NetworkPeer& peer);

    const std::string& getId() const { return m_id; }
    const std::string& getDisplayName() const { return m_displayName; }
    void setDisplayName(std::string name) { m_displayName = std::move(name); }

    const std::string& getIpAddress() const { return m_ipAddress; }
    void setIpAddress(std::string ip) { m_ipAddress = std::move(ip); }

    uint16_t getPort() const { return m_port; }
    void setPort(uint16_t port) { m_port = port; }

    bool isOnline() const { return m_isOnline; }
    void setOnline(bool online) { m_isOnline = online; }

    uint32_t getProtocolVersion() const { return m_protocolVersion; }

    void updateLastSeen();
    std::chrono::system_clock::time_point getLastSeen() const { return m_lastSeen; }
    int64_t getSecondsSinceLastSeen() const;

    template <typename TSpecificPeer>
    static std::vector<std::shared_ptr<TSpecificPeer>> filterPeers(
        const std::vector<std::shared_ptr<NetworkPeer>>& peers
    ) {
        std::vector<std::shared_ptr<TSpecificPeer>> result;
        for (const auto& peer : peers) {
            if (auto specific = std::dynamic_pointer_cast<TSpecificPeer>(peer)) {
                result.push_back(specific);
            }
        }
        return result;
    }

protected:
    std::string m_id;
    std::string m_displayName;
    std::string m_ipAddress;
    uint16_t m_port;
    bool m_isOnline{false};
    uint32_t m_protocolVersion{1};
    std::chrono::system_clock::time_point m_lastSeen;
};

}
