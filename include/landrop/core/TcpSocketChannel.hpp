#pragma once

#include "landrop/core/ITransportChannel.hpp"
#include "landrop/core/NetworkSession.hpp"
#include <memory>
#include <atomic>
#include <mutex>

namespace landrop {

/**
 * @brief Concrete TCP implementation of ITransportChannel wrapping NetworkSession.
 * Supports both plaintext TCP and TLS encrypted transport channels.
 */
class TcpSocketChannel : public ITransportChannel {
public:
    explicit TcpSocketChannel(std::unique_ptr<NetworkSession> session);

    TcpSocketChannel(
        const std::string& remoteIp,
        uint16_t remotePort,
        bool useTls = false,
        const TlsConfig& tlsConfig = {}
    );

    ~TcpSocketChannel() override;

    TransportType getTransportType() const override;
    std::string getRemoteAddress() const override;
    uint16_t getRemotePort() const override;
    bool isConnected() const override;
    bool isEncrypted() const override;

    bool sendExact(const uint8_t* data, size_t length) override;

    bool receiveExact(
        uint8_t* buffer,
        size_t length,
        int timeoutMs = 5000,
        std::atomic<bool>* cancelToken = nullptr
    ) override;

    bool waitForReadable(int timeoutMs, std::atomic<bool>* cancelToken = nullptr) override;

    size_t getBufferedAmount() const override;
    uint64_t getBytesSent() const override;
    uint64_t getBytesReceived() const override;

    void close() override;

    NetworkSession* getSession() const { return m_session.get(); }

    static std::shared_ptr<TcpSocketChannel> create(std::unique_ptr<NetworkSession> session);

private:
    std::unique_ptr<NetworkSession> m_session;
    mutable std::atomic<size_t> m_pendingInFlightBytes{0};
    mutable std::mutex m_sendMutex;
    mutable std::mutex m_recvMutex;
};

} // namespace landrop
