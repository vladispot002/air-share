#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <atomic>
#include <functional>
#include <thread>
#include "landrop/utils/Types.hpp"
#include "landrop/utils/SocketCompat.hpp"

namespace landrop {

class MulticastBeacon {
public:
    using DiscoveryCallback = std::function<void(const std::string& name, const std::string& uuid, const std::string& ip, uint16_t port)>;

    explicit MulticastBeacon(
        std::string deviceName = "HostNode",
        std::string deviceUuid = "",
        uint16_t listenPort = 8848,
        std::string multicastGroup = "239.255.255.250",
        uint16_t beaconPort = 52637
    );
    ~MulticastBeacon();

    static std::string generateUniqueUuid();

    bool start();
    void stop();

    bool broadcastAnnouncement();
    std::string serializePayload() const;
    static bool parsePayload(const std::string& payload, std::string& outName, std::string& outUuid, uint16_t& outPort);

    void setDiscoveryCallback(DiscoveryCallback cb) { m_discoveryCallback = std::move(cb); }

    bool isRunning() const { return m_isRunning.load(); }
    size_t getBeaconsSent() const { return m_beaconsSent.load(); }
    size_t getBeaconsReceived() const { return m_beaconsReceived.load(); }

    const std::string& getDeviceName() const { return m_deviceName; }
    void setDeviceName(std::string name) { m_deviceName = std::move(name); }

    const std::string& getDeviceUuid() const { return m_deviceUuid; }
    void setDeviceUuid(std::string uuid) { m_deviceUuid = std::move(uuid); }

    const std::string& getMulticastGroup() const { return m_multicastGroup; }
    uint16_t getBeaconPort() const { return m_beaconPort; }
    uint16_t getListenPort() const { return m_listenPort; }
    void setListenPort(uint16_t port) { m_listenPort = port; }
    uint32_t getIntervalMs() const { return m_intervalMs; }
    void setIntervalMs(uint32_t ms) { m_intervalMs = ms; }

    void simulateReceiveBeacon(const std::string& packetData);

private:
    std::string m_deviceName;
    std::string m_deviceUuid;
    uint16_t m_listenPort;
    std::string m_multicastGroup;
    uint16_t m_beaconPort;
    uint32_t m_intervalMs{2000};
    std::atomic<bool> m_isRunning{false};
    std::atomic<size_t> m_beaconsSent{0};
    std::atomic<size_t> m_beaconsReceived{0};

    socket_handle_t m_listenSocketFd{INVALID_SOCKET_VALUE};
    std::thread m_listenerThread;
    DiscoveryCallback m_discoveryCallback;
};

}
