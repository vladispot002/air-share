#include "landrop/core/NetworkSession.hpp"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <utility>

namespace landrop {

NetworkSession::NetworkSession(std::string sessionId, std::string remoteIp, uint16_t remotePort)
    : m_sessionId(std::move(sessionId)),
      m_remoteIp(std::move(remoteIp)),
      m_remotePort(remotePort),
      m_connectedAt(std::chrono::steady_clock::now()),
      m_lastActivityAt(std::chrono::steady_clock::now()) {}

NetworkSession::~NetworkSession() {
    closeSession();
}

NetworkSession::NetworkSession(NetworkSession&& other) noexcept
    : m_sessionId(std::move(other.m_sessionId)),
      m_remoteIp(std::move(other.m_remoteIp)),
      m_remotePort(other.m_remotePort),
      m_socketFd(other.m_socketFd),
      m_listenFd(other.m_listenFd),
      m_state(other.m_state),
      m_bytesSent(other.m_bytesSent),
      m_bytesReceived(other.m_bytesReceived),
      m_timeoutSeconds(other.m_timeoutSeconds),
      m_connectedAt(other.m_connectedAt),
      m_lastActivityAt(other.m_lastActivityAt),
      m_tlsCipherSuite(std::move(other.m_tlsCipherSuite)),
      m_isEncrypted(other.m_isEncrypted) {
    other.m_socketFd = -1;
    other.m_listenFd = -1;
    other.m_state = ConnectionState::Disconnected;
}

NetworkSession& NetworkSession::operator=(NetworkSession&& other) noexcept {
    if (this != &other) {
        closeSession();
        m_sessionId = std::move(other.m_sessionId);
        m_remoteIp = std::move(other.m_remoteIp);
        m_remotePort = other.m_remotePort;
        m_socketFd = other.m_socketFd;
        m_listenFd = other.m_listenFd;
        m_state = other.m_state;
        m_bytesSent = other.m_bytesSent;
        m_bytesReceived = other.m_bytesReceived;
        m_timeoutSeconds = other.m_timeoutSeconds;
        m_connectedAt = other.m_connectedAt;
        m_lastActivityAt = other.m_lastActivityAt;
        m_tlsCipherSuite = std::move(other.m_tlsCipherSuite);
        m_isEncrypted = other.m_isEncrypted;

        other.m_socketFd = -1;
        other.m_listenFd = -1;
        other.m_state = ConnectionState::Disconnected;
    }
    return *this;
}

bool NetworkSession::connectTo(const std::string& ip, uint16_t port) {
    closeSession();
    m_remoteIp = ip;
    m_remotePort = port;

    m_socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_socketFd < 0) {
        m_state = ConnectionState::Disconnected;
        return false;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr) <= 0) {
        closeSession();
        return false;
    }

    m_state = ConnectionState::Handshake;
    if (connect(m_socketFd, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
        closeSession();
        return false;
    }

    m_state = ConnectionState::Connected;
    m_connectedAt = std::chrono::steady_clock::now();
    m_lastActivityAt = m_connectedAt;
    return true;
}

bool NetworkSession::listenOn(uint16_t port) {
    closeSession();
    m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenFd < 0) {
        return false;
    }

    int opt = 1;
    setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(m_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(m_listenFd);
        m_listenFd = -1;
        return false;
    }

    if (listen(m_listenFd, 5) < 0) {
        close(m_listenFd);
        m_listenFd = -1;
        return false;
    }

    m_state = ConnectionState::Handshake;
    return true;
}

bool NetworkSession::acceptIncoming() {
    if (m_listenFd < 0) {
        return false;
    }

    sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);
    m_socketFd = accept(m_listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
    if (m_socketFd < 0) {
        return false;
    }

    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
    m_remoteIp = ipStr;
    m_remotePort = ntohs(clientAddr.sin_port);

    m_state = ConnectionState::Connected;
    m_connectedAt = std::chrono::steady_clock::now();
    m_lastActivityAt = m_connectedAt;
    return true;
}

void NetworkSession::closeSession() {
    if (m_socketFd >= 0) {
        close(m_socketFd);
        m_socketFd = -1;
    }
    if (m_listenFd >= 0) {
        close(m_listenFd);
        m_listenFd = -1;
    }
    m_state = ConnectionState::Terminated;
}

bool NetworkSession::sendAll(const uint8_t* data, size_t length) {
    if (m_socketFd < 0 || m_state != ConnectionState::Connected) {
        return false;
    }
    size_t total = 0;
    while (total < length) {
        ssize_t bytes = send(m_socketFd, data + total, length - total, 0);
        if (bytes <= 0) {
            closeSession();
            return false;
        }
        total += static_cast<size_t>(bytes);
        m_bytesSent += static_cast<uint64_t>(bytes);
    }
    updateActivity();
    return true;
}

bool NetworkSession::sendData(const std::vector<uint8_t>& data) {
    return sendAll(data.data(), data.size());
}

bool NetworkSession::receiveExact(uint8_t* buffer, size_t length) {
    if (m_socketFd < 0 || m_state != ConnectionState::Connected) {
        return false;
    }
    size_t total = 0;
    while (total < length) {
        ssize_t bytes = recv(m_socketFd, buffer + total, length - total, 0);
        if (bytes <= 0) {
            closeSession();
            return false;
        }
        total += static_cast<size_t>(bytes);
        m_bytesReceived += static_cast<uint64_t>(bytes);
    }
    updateActivity();
    return true;
}

bool NetworkSession::receiveData(std::vector<uint8_t>& buffer, size_t expectedSize) {
    buffer.resize(expectedSize);
    return receiveExact(buffer.data(), expectedSize);
}

bool NetworkSession::checkTimeout() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastActivityAt).count();
    return elapsed > static_cast<int64_t>(m_timeoutSeconds);
}

void NetworkSession::updateActivity() {
    m_lastActivityAt = std::chrono::steady_clock::now();
}

double NetworkSession::getElapsedSeconds() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - m_connectedAt).count();
}

}
