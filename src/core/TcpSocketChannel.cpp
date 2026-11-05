#include "landrop/core/TcpSocketChannel.hpp"
#include <chrono>
#include <thread>
#include <algorithm>

#if !defined(_WIN32) && !defined(_WIN64)
#include <sys/ioctl.h>
#include <sys/socket.h>
#endif

namespace landrop {

TcpSocketChannel::TcpSocketChannel(std::unique_ptr<NetworkSession> session)
    : m_session(std::move(session)) {}

TcpSocketChannel::TcpSocketChannel(
    const std::string& remoteIp,
    uint16_t remotePort,
    bool useTls,
    const TlsConfig& tlsConfig
) {
    m_session = std::make_unique<NetworkSession>("tcp-channel", remoteIp, remotePort);
    if (useTls) {
        m_session->enableTls(tlsConfig);
    }
    m_session->connectTo(remoteIp, remotePort);
}

TcpSocketChannel::~TcpSocketChannel() {
    close();
}

TransportType TcpSocketChannel::getTransportType() const {
    if (isEncrypted()) {
        return TransportType::TlsTcp;
    }
    return TransportType::TcpDirect;
}

std::string TcpSocketChannel::getRemoteAddress() const {
    return m_session ? m_session->getRemoteIp() : "";
}

uint16_t TcpSocketChannel::getRemotePort() const {
    return m_session ? m_session->getRemotePort() : 0;
}

bool TcpSocketChannel::isConnected() const {
    return m_session && m_session->getConnectionState() == ConnectionState::Connected;
}

bool TcpSocketChannel::isEncrypted() const {
    return m_session && m_session->isEncrypted();
}

bool TcpSocketChannel::sendExact(const uint8_t* data, size_t length) {
    if (!isConnected()) {
        return false;
    }
    m_pendingInFlightBytes += length;
    bool ok = m_session->sendAll(data, length);
    m_pendingInFlightBytes -= length;
    return ok;
}

bool TcpSocketChannel::receiveExact(
    uint8_t* buffer,
    size_t length,
    int timeoutMs,
    std::atomic<bool>* cancelToken
) {
    if (!isConnected()) {
        return false;
    }

    if (!cancelToken) {
        if (timeoutMs > 0 && m_session) {
            uint32_t sec = static_cast<uint32_t>((timeoutMs + 999) / 1000);
            m_session->setTimeoutSeconds(sec);
        }
        return m_session->receiveExact(buffer, length);
    }

    // Cancellation-aware receive loop
    size_t totalReceived = 0;
    auto start = std::chrono::steady_clock::now();

    while (totalReceived < length) {
        if (cancelToken->load()) {
            return false;
        }

        if (timeoutMs > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start
            ).count();
            if (elapsed > timeoutMs) {
                return false;
            }
        }

        // Read in small slices (up to 16KB per step) to keep cancel check responsive
        size_t sliceSize = std::min<size_t>(length - totalReceived, 16384);
        if (!m_session->receiveExact(buffer + totalReceived, sliceSize)) {
            return false;
        }
        totalReceived += sliceSize;
    }

    return true;
}

size_t TcpSocketChannel::getBufferedAmount() const {
    if (!m_session || !isValidSocket(m_session->getSocketFd())) {
        return 0;
    }

#if !defined(_WIN32) && !defined(_WIN64)
    int pending = 0;
#if defined(TIOCOUTQ)
    if (::ioctl(m_session->getSocketFd(), TIOCOUTQ, &pending) == 0 && pending >= 0) {
        return static_cast<size_t>(pending);
    }
#elif defined(SIOCOUTQ)
    if (::ioctl(m_session->getSocketFd(), SIOCOUTQ, &pending) == 0 && pending >= 0) {
        return static_cast<size_t>(pending);
    }
#endif
#endif

    return m_pendingInFlightBytes.load();
}

uint64_t TcpSocketChannel::getBytesSent() const {
    return m_session ? m_session->getBytesSent() : 0;
}

uint64_t TcpSocketChannel::getBytesReceived() const {
    return m_session ? m_session->getBytesReceived() : 0;
}

void TcpSocketChannel::close() {
    if (m_session) {
        m_session->closeSession();
    }
}

std::shared_ptr<TcpSocketChannel> TcpSocketChannel::create(std::unique_ptr<NetworkSession> session) {
    return std::make_shared<TcpSocketChannel>(std::move(session));
}

} // namespace landrop
