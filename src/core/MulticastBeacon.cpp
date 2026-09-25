#include "landrop/core/MulticastBeacon.hpp"
#include <sstream>
#include <utility>

namespace landrop {

MulticastBeacon::MulticastBeacon(
    std::string deviceName,
    std::string deviceUuid,
    uint16_t listenPort,
    std::string multicastGroup,
    uint16_t beaconPort
) : m_deviceName(std::move(deviceName)),
    m_deviceUuid(std::move(deviceUuid)),
    m_listenPort(listenPort),
    m_multicastGroup(std::move(multicastGroup)),
    m_beaconPort(beaconPort) {}

MulticastBeacon::~MulticastBeacon() {
    stop();
}

bool MulticastBeacon::start() {
    m_isRunning.store(true);
    return true;
}

void MulticastBeacon::stop() {
    m_isRunning.store(false);
}

bool MulticastBeacon::broadcastAnnouncement() {
    if (!m_isRunning.load()) {
        return false;
    }
    ++m_beaconsSent;
    return true;
}

std::string MulticastBeacon::serializePayload() const {
    std::ostringstream oss;
    oss << "LANDROP_BEACON|" << m_deviceName << "|" << m_deviceUuid << "|" << m_listenPort;
    return oss.str();
}

bool MulticastBeacon::parsePayload(const std::string& payload, std::string& outName, std::string& outUuid, uint16_t& outPort) {
    if (payload.rfind("LANDROP_BEACON|", 0) != 0) {
        return false;
    }

    std::stringstream ss(payload.substr(15));
    std::string name, uuid, portStr;
    if (std::getline(ss, name, '|') && std::getline(ss, uuid, '|') && std::getline(ss, portStr, '|')) {
        try {
            outName = name;
            outUuid = uuid;
            outPort = static_cast<uint16_t>(std::stoi(portStr));
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

void MulticastBeacon::simulateReceiveBeacon(const std::string& packetData) {
    std::string name, uuid;
    uint16_t port = 0;
    if (parsePayload(packetData, name, uuid, port)) {
        ++m_beaconsReceived;
    }
}

}
