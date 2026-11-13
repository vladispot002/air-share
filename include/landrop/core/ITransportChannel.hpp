#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include "landrop/utils/Types.hpp"

namespace landrop {

enum class TransportType {
    TcpDirect,
    TlsTcp,
    WebRtcDataChannel,
    CloudRelay
};

inline const char* transportTypeToString(TransportType type) {
    switch (type) {
        case TransportType::TcpDirect: return "TcpDirect";
        case TransportType::TlsTcp: return "TlsTcp";
        case TransportType::WebRtcDataChannel: return "WebRtcDataChannel";
        case TransportType::CloudRelay: return "CloudRelay";
        default: return "Unknown";
    }
}

/**
 * @brief Pure virtual transport interface decoupling the transfer engine and wire protocols
 * from underlying network mediums (Raw TCP, TLS-TCP, WebRTC DataChannels, Cloud Relay).
 */
class ITransportChannel {
public:
    virtual ~ITransportChannel() = default;

    virtual TransportType getTransportType() const = 0;
    virtual std::string getRemoteAddress() const = 0;
    virtual uint16_t getRemotePort() const = 0;
    virtual bool isConnected() const = 0;
    virtual bool isEncrypted() const = 0;

    /**
     * @brief Sends exactly @p length bytes of data over the channel.
     * Blocks or loops until all bytes are sent, an error occurs, or timeout/cancellation is triggered.
     */
    virtual bool sendExact(const uint8_t* data, size_t length) = 0;

    /**
     * @brief Receives exactly @p length bytes into @p buffer.
     * Supports configurable timeouts and external cancellation.
     */
    virtual bool receiveExact(
        uint8_t* buffer,
        size_t length,
        int timeoutMs = 5000,
        std::atomic<bool>* cancelToken = nullptr
    ) = 0;

    /**
     * @brief Waits up to @p timeoutMs for at least one byte of data to become readable on the channel.
     * Returns true if data is ready to be read, or false on timeout/cancellation/disconnection.
     */
    virtual bool waitForReadable(int timeoutMs, std::atomic<bool>* cancelToken = nullptr) {
        (void)timeoutMs;
        (void)cancelToken;
        return false;
    }

    /**
     * @brief Returns queued unacknowledged bytes in transport buffer for backpressure flow control.
     * Critical for WebRTC DataChannel SCTP buffering and kernel send queues.
     */
    virtual size_t getBufferedAmount() const = 0;

    virtual uint64_t getBytesSent() const = 0;
    virtual uint64_t getBytesReceived() const = 0;

    virtual void close() = 0;
};

} // namespace landrop
