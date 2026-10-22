#include "landrop/core/MulticastBeacon.hpp"
#include <sstream>
#include <utility>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <cstring>

#include <fcntl.h>
#include <chrono>
#include <ctime>
#include <cstdio>

namespace landrop {

std::string MulticastBeacon::generateUniqueUuid() {
    uint32_t r1 = 0, r2 = 0;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        if (read(fd, &r1, sizeof(r1)) < 0) r1 = 0;
        if (read(fd, &r2, sizeof(r2)) < 0) r2 = 0;
        close(fd);
    }
    if (r1 == 0 && r2 == 0) {
        auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        r1 = static_cast<uint32_t>(now ^ (getpid() << 16));
        r2 = static_cast<uint32_t>(now >> 32);
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "node-%08x-%08x", r1, r2);
    return std::string(buf);
}

MulticastBeacon::MulticastBeacon(
    std::string deviceName,
    std::string deviceUuid,
    uint16_t listenPort,
    std::string multicastGroup,
    uint16_t beaconPort
) : m_deviceName(std::move(deviceName)),
    m_deviceUuid(deviceUuid.empty() ? generateUniqueUuid() : std::move(deviceUuid)),
    m_listenPort(listenPort),
    m_multicastGroup(std::move(multicastGroup)),
    m_beaconPort(beaconPort) {}

MulticastBeacon::~MulticastBeacon() {
    stop();
}

bool MulticastBeacon::start() {
    if (m_isRunning.load()) {
        return true;
    }

    m_listenSocketFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_listenSocketFd < 0) {
        return false;
    }

    int reuse = 1;
    setsockopt(m_listenSocketFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    setsockopt(m_listenSocketFd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif
    struct sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(m_beaconPort);
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(m_listenSocketFd, reinterpret_cast<struct sockaddr*>(&bindAddr), sizeof(bindAddr)) != 0) {
        close(m_listenSocketFd);
        m_listenSocketFd = -1;
        return false;
    }

    struct ip_mreq mreq{};
    if (inet_pton(AF_INET, m_multicastGroup.c_str(), &mreq.imr_multiaddr) == 1) {
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        setsockopt(m_listenSocketFd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    }

    m_isRunning.store(true);

    m_listenerThread = std::thread([this]() {
        char buf[2048];
        auto lastAnnouncement = std::chrono::steady_clock::now();
        broadcastAnnouncement();

        while (m_isRunning.load()) {
            fd_set readFds;
            FD_ZERO(&readFds);
            FD_SET(m_listenSocketFd, &readFds);
            struct timeval tv{0, 200000}; // 200ms
            int ret = select(m_listenSocketFd + 1, &readFds, nullptr, nullptr, &tv);
            if (ret > 0 && FD_ISSET(m_listenSocketFd, &readFds)) {
                struct sockaddr_in senderAddr{};
                socklen_t addrLen = sizeof(senderAddr);
                ssize_t n = recvfrom(m_listenSocketFd, buf, sizeof(buf) - 1, 0,
                                     reinterpret_cast<struct sockaddr*>(&senderAddr), &addrLen);
                if (n > 0) {
                    buf[n] = '\0';
                    char senderIp[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &senderAddr.sin_addr, senderIp, sizeof(senderIp));
                    std::string payload(buf, static_cast<size_t>(n));

                    std::string name, uuid;
                    uint16_t port = 0;
                    if (parsePayload(payload, name, uuid, port)) {
                        m_beaconsReceived.fetch_add(1);
                        if (uuid != m_deviceUuid && m_discoveryCallback) {
                            m_discoveryCallback(name, uuid, std::string(senderIp), port);
                        }
                    }
                }
            }

            auto now = std::chrono::steady_clock::now();
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastAnnouncement).count();
            if (elapsedMs >= static_cast<int64_t>(m_intervalMs)) {
                broadcastAnnouncement();
                lastAnnouncement = now;
            }
        }
    });

    return true;
}

void MulticastBeacon::stop() {
    if (!m_isRunning.exchange(false)) {
        return;
    }
    if (m_listenerThread.joinable()) {
        m_listenerThread.join();
    }
    if (m_listenSocketFd >= 0) {
        close(m_listenSocketFd);
        m_listenSocketFd = -1;
    }
}

bool MulticastBeacon::broadcastAnnouncement() {
    if (!m_isRunning.load()) {
        return false;
    }
    ++m_beaconsSent;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return false;
    }

    int broadcastEnable = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

    std::string payload = serializePayload();

    struct sockaddr_in bcastAddr{};
    bcastAddr.sin_family = AF_INET;
    bcastAddr.sin_port = htons(m_beaconPort);
    bcastAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(sock, payload.data(), payload.size(), 0, reinterpret_cast<struct sockaddr*>(&bcastAddr), sizeof(bcastAddr));

    struct sockaddr_in mcastAddr{};
    mcastAddr.sin_family = AF_INET;
    mcastAddr.sin_port = htons(m_beaconPort);
    if (inet_pton(AF_INET, m_multicastGroup.c_str(), &mcastAddr.sin_addr) == 1) {
        sendto(sock, payload.data(), payload.size(), 0, reinterpret_cast<struct sockaddr*>(&mcastAddr), sizeof(mcastAddr));
    }

    struct sockaddr_in loopAddr{};
    loopAddr.sin_family = AF_INET;
    loopAddr.sin_port = htons(m_beaconPort);
    loopAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(sock, payload.data(), payload.size(), 0, reinterpret_cast<struct sockaddr*>(&loopAddr), sizeof(loopAddr));

    close(sock);
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
        if (m_discoveryCallback) {
            m_discoveryCallback(name, uuid, "127.0.0.1", port);
        }
    }
}

}
