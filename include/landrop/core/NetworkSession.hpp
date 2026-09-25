#pragma once

#include <string>
#include <cstdint>
#include <chrono>
#include <vector>
#include "landrop/utils/Types.hpp"

namespace landrop {

class NetworkSession {
public:
    explicit NetworkSession(std::string sessionId, std::string remoteIp = "127.0.0.1", uint16_t remotePort = 8848);
    ~NetworkSession();

    NetworkSession(const NetworkSession&) = delete;
    NetworkSession& operator=(const NetworkSession&) = delete;
    NetworkSession(NetworkSession&&) noexcept;
    NetworkSession& operator=(NetworkSession&&) noexcept;

    bool connectTo(const std::string& ip, uint16_t port);
    bool listenOn(uint16_t port);
    bool acceptIncoming();
    void closeSession();

    bool sendAll(const uint8_t* data, size_t length);
    bool sendData(const std::vector<uint8_t>& data);
    bool receiveExact(uint8_t* buffer, size_t length);
    bool receiveData(std::vector<uint8_t>& buffer, size_t expectedSize);

    bool checkTimeout() const;
    void updateActivity();

    const std::string& getSessionId() const { return m_sessionId; }
    const std::string& getRemoteIp() const { return m_remoteIp; }
    uint16_t getRemotePort() const { return m_remotePort; }
    ConnectionState getConnectionState() const { return m_state; }
    void setConnectionState(ConnectionState state) { m_state = state; }

    uint64_t getBytesSent() const { return m_bytesSent; }
    uint64_t getBytesReceived() const { return m_bytesReceived; }
    uint32_t getTimeoutSeconds() const { return m_timeoutSeconds; }
    void setTimeoutSeconds(uint32_t timeout) { m_timeoutSeconds = timeout; }

    bool isEncrypted() const { return m_isEncrypted; }
    void setEncrypted(bool encrypted, std::string cipher = "TLS_AES_256_GCM_SHA384") {
        m_isEncrypted = encrypted;
        m_tlsCipherSuite = std::move(cipher);
    }
    const std::string& getTlsCipherSuite() const { return m_tlsCipherSuite; }

    double getElapsedSeconds() const;

private:
    std::string m_sessionId;
    std::string m_remoteIp;
    uint16_t m_remotePort;
    int m_socketFd{-1};
    int m_listenFd{-1};
    ConnectionState m_state{ConnectionState::Disconnected};
    uint64_t m_bytesSent{0};
    uint64_t m_bytesReceived{0};
    uint32_t m_timeoutSeconds{30};
    std::chrono::steady_clock::time_point m_connectedAt;
    std::chrono::steady_clock::time_point m_lastActivityAt;
    std::string m_tlsCipherSuite{"None"};
    bool m_isEncrypted{false};
};

}
