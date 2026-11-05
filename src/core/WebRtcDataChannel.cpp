#include "landrop/core/WebRtcDataChannel.hpp"
#include <chrono>
#include <cstring>

namespace landrop {

WebRtcDataChannel::WebRtcDataChannel(
    std::string peerId,
    std::string label,
    WebRtcConfig config
) : m_peerId(std::move(peerId)),
    m_label(std::move(label)),
    m_config(std::move(config)) {}

WebRtcDataChannel::~WebRtcDataChannel() {
    close();
}

TransportType WebRtcDataChannel::getTransportType() const {
    return TransportType::WebRtcDataChannel;
}

std::string WebRtcDataChannel::getRemoteAddress() const {
    return m_peerId;
}

uint16_t WebRtcDataChannel::getRemotePort() const {
    return 0; // WebRTC operates over dynamic ICE candidate pairs
}

bool WebRtcDataChannel::isConnected() const {
    return m_connected.load();
}

bool WebRtcDataChannel::isEncrypted() const {
    return true; // WebRTC DataChannels are always DTLS-SRTP encrypted
}

size_t WebRtcDataChannel::getBufferedAmount() const {
    return m_bufferedAmount.load();
}

uint64_t WebRtcDataChannel::getBytesSent() const {
    return m_bytesSent.load();
}

uint64_t WebRtcDataChannel::getBytesReceived() const {
    return m_bytesReceived.load();
}

bool WebRtcDataChannel::sendExact(const uint8_t* data, size_t length) {
    if (!m_connected.load() || !data) {
        return false;
    }

    // Backpressure flow control check against max configured send buffer
    if (m_bufferedAmount.load() + length > m_config.maxBufferedAmount) {
        return false;
    }

#if defined(LANDROP_HAVE_WEBRTC)
    if (m_dataChannel && m_dataChannel->isOpen()) {
        m_bufferedAmount += length;
        bool ok = m_dataChannel->send(reinterpret_cast<const std::byte*>(data), length);
        if (ok) {
            m_bytesSent += length;
        }
        return ok;
    }
    return false;
#else
    m_bytesSent += length;
    return true;
#endif
}

bool WebRtcDataChannel::receiveExact(
    uint8_t* buffer,
    size_t length,
    int timeoutMs,
    std::atomic<bool>* cancelToken
) {
    if (!buffer || length == 0) return false;

    std::unique_lock<std::mutex> lock(m_rxMutex);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs : 5000);

    while (m_rxBuffer.size() < length) {
        if (!m_connected.load()) {
            return false;
        }
        if (cancelToken && cancelToken->load()) {
            return false;
        }
        if (timeoutMs > 0 && std::chrono::steady_clock::now() >= deadline) {
            return false;
        }

        auto waitSlice = std::chrono::milliseconds(50);
        m_rxCv.wait_for(lock, waitSlice, [&]() {
            return m_rxBuffer.size() >= length || !m_connected.load() || (cancelToken && cancelToken->load());
        });
    }

    std::memcpy(buffer, m_rxBuffer.data(), length);
    m_rxBuffer.erase(m_rxBuffer.begin(), m_rxBuffer.begin() + length);
    m_bytesReceived += length;
    return true;
}

void WebRtcDataChannel::injectIncomingBytes(const uint8_t* data, size_t length) {
    if (!data || length == 0) return;
    {
        std::lock_guard<std::mutex> lock(m_rxMutex);
        m_rxBuffer.insert(m_rxBuffer.end(), data, data + length);
    }
    m_rxCv.notify_all();
}

void WebRtcDataChannel::close() {
    m_connected.store(false);
#if defined(LANDROP_HAVE_WEBRTC)
    if (m_dataChannel) {
        m_dataChannel->close();
        m_dataChannel.reset();
    }
    if (m_peerConnection) {
        m_peerConnection->close();
        m_peerConnection.reset();
    }
#endif
    m_rxCv.notify_all();
}

} // namespace landrop
