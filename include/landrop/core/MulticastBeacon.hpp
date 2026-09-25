#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <atomic>
#include "landrop/utils/Types.hpp"

namespace landrop {

class MulticastBeacon {
public:
    explicit MulticastBeacon(
        std::string deviceName = "HostNode",
        std::string deviceUuid = "node-uuid-001",
        uint16_t listenPort = 8848,
        std::string multicastGroup = "239.255.255.250",
        uint16_t beaconPort = 52637
    );
    ~MulticastBeacon();

    bool start();
    void stop();

    bool broadcastAnnouncement();
    std::string serializePayload() const;
    static bool parsePayload(const std::string& payload, std::string& outName, std::string& outUuid, uint16_t& outPort);

    bool isRunning() const { return m_isRunning.load(); }
    size_t getBeaconsSent() const { return m_beaconsSent; }
    size_t getBeaconsReceived() const { return m_beaconsReceived; }

    const std::string& getDeviceName() const { return m_deviceName; }
    void setDeviceName(std::string name) { m_deviceName = std::move(name); }

    const std::string& getMulticastGroup() const { return m_multicastGroup; }
    uint16_t getBeaconPort() const { return m_beaconPort; }
    uint32_t getIntervalMs() const { return m_intervalMs; }
    void setIntervalMs(uint32_t ms) { m_intervalMs = ms; }

    void simulateReceiveBeacon(const std::string& packetData);

private:
    std::string m_deviceName;
    std::string m_deviceUuid;
    uint16_t m_listenPort;
    std::string m_multicastGroup;
    uint16_t m_beaconPort;
    uint32_t m_intervalMs{1500};
    std::atomic<bool> m_isRunning{false};
    size_t m_beaconsSent{0};
    size_t m_beaconsReceived{0};
};

}
