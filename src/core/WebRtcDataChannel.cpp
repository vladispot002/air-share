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
#if defined(LANDROP_HAVE_WEBRTC)
    if (m_dataChannel && m_dataChannel->isOpen()) {
        return m_dataChannel->bufferedAmount();
    }
#endif
    return m_bufferedAmount.load();
}

uint64_t WebRtcDataChannel::getBytesSent() const {
    return m_bytesSent.load();
}

uint64_t WebRtcDataChannel::getBytesReceived() const {
    return m_bytesReceived.load();
}

void WebRtcDataChannel::notifyBufferedAmountLow() {
    m_txCv.notify_all();
}

bool WebRtcDataChannel::sendExact(const uint8_t* data, size_t length) {
    if (!m_connected.load() || !data || length == 0) {
        return false;
    }

    if (length > m_config.maxBufferedAmount) {
        return false;
    }

    // Non-failing backpressure flow control (TC-RTC-02):
    // If send queue exceeds max limit, block and wait for buffer drain rather than failing
    if (getBufferedAmount() + length > m_config.maxBufferedAmount) {
        std::unique_lock<std::mutex> lock(m_txMutex);
        bool drained = m_txCv.wait_for(lock, std::chrono::milliseconds(2000), [&]() {
            return !m_connected.load() || (getBufferedAmount() + length <= m_config.maxBufferedAmount);
        });

        if (!drained || !m_connected.load()) {
            return false;
        }
    }

#if defined(LANDROP_HAVE_WEBRTC)
    if (m_dataChannel) {
        if (!m_dataChannel->isOpen()) {
            return false;
        }
        bool ok = m_dataChannel->send(reinterpret_cast<const std::byte*>(data), length);
        if (ok) {
            m_bytesSent += length;
        }
        return ok;
    }
#endif
    m_bytesSent += length;
    return true;
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

    while ((m_rxBuffer.size() - m_rxOffset) < length) {
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
            return (m_rxBuffer.size() - m_rxOffset) >= length || !m_connected.load() || (cancelToken && cancelToken->load());
        });
    }

    std::memcpy(buffer, m_rxBuffer.data() + m_rxOffset, length);
    m_rxOffset += length;
    if (m_rxOffset == m_rxBuffer.size()) {
        m_rxBuffer.clear();
        m_rxOffset = 0;
    } else if (m_rxOffset >= 65536) {
        m_rxBuffer.erase(m_rxBuffer.begin(), m_rxBuffer.begin() + m_rxOffset);
        m_rxOffset = 0;
    }
    m_bytesReceived += length;
    return true;
}

void WebRtcDataChannel::injectIncomingBytes(const uint8_t* data, size_t length) {
    if (!data || length == 0) return;
    {
        std::lock_guard<std::mutex> lock(m_rxMutex);
        if (m_rxBuffer.empty()) {
            m_rxOffset = 0;
        }
        m_rxBuffer.insert(m_rxBuffer.end(), data, data + length);
    }
    m_rxCv.notify_all();
}

#if defined(LANDROP_HAVE_WEBRTC)
void WebRtcDataChannel::setRealDataChannel(
    std::shared_ptr<rtc::DataChannel> channel,
    std::shared_ptr<rtc::PeerConnection> pc
) {
    m_dataChannel = std::move(channel);
    m_peerConnection = std::move(pc);

    if (m_dataChannel) {
        m_dataChannel->setBufferedAmountLowThreshold(m_config.lowWaterMark);

        m_dataChannel->onBufferedAmountLow([this]() {
            notifyBufferedAmountLow();
        });

        m_dataChannel->onMessage([this](auto msg) {
            if (std::holds_alternative<rtc::binary>(msg)) {
                auto& bin = std::get<rtc::binary>(msg);
                injectIncomingBytes(reinterpret_cast<const uint8_t*>(bin.data()), bin.size());
            } else if (std::holds_alternative<std::string>(msg)) {
                auto& str = std::get<std::string>(msg);
                injectIncomingBytes(reinterpret_cast<const uint8_t*>(str.data()), str.size());
            }
        });

        m_dataChannel->onClosed([this]() {
            close();
        });

        m_connected.store(m_dataChannel->isOpen());
    }
}
#endif

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
    {
        std::lock_guard<std::mutex> lock(m_rxMutex);
        m_rxBuffer.clear();
        m_rxOffset = 0;
    }
    m_rxCv.notify_all();
    m_txCv.notify_all();
}

} // namespace landrop
