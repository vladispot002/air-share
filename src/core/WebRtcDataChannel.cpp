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
    std::shared_ptr<rtc::DataChannel> dc;
    {
        std::lock_guard<std::mutex> lock(m_channelMutex);
        dc = m_dataChannel;
    }
    if (dc && dc->isOpen()) {
        return dc->bufferedAmount();
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

    // Non-failing backpressure flow control (TC-RTC-02 / F-03 / F-07):
    // Wait in slices up to 30 seconds for buffer drain rather than failing
    if (getBufferedAmount() + length > m_config.maxBufferedAmount) {
        std::unique_lock<std::mutex> lock(m_txMutex);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (getBufferedAmount() + length > m_config.maxBufferedAmount) {
            if (!m_connected.load()) {
                return false;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            m_txCv.wait_for(lock, std::chrono::milliseconds(100), [&]() {
                return !m_connected.load() || (getBufferedAmount() + length <= m_config.maxBufferedAmount);
            });
        }
    }

    if (m_customSink) {
        bool ok = m_customSink(data, length);
        if (ok) {
            m_bytesSent += length;
        }
        return ok;
    }

#if defined(LANDROP_HAVE_WEBRTC)
    std::shared_ptr<rtc::DataChannel> dc;
    {
        std::lock_guard<std::mutex> lock(m_channelMutex);
        dc = m_dataChannel;
    }
    if (dc) {
        if (!dc->isOpen()) {
            return false;
        }
        try {
            // In libdatachannel, dc->send() returns true if sent immediately,
            // or false if buffered into mSendQueue. In both cases data is enqueued successfully.
            dc->send(reinterpret_cast<const std::byte*>(data), length);
            m_bytesSent += length;
            return true;
        } catch (...) {
            return false;
        }
    } else if (!m_simulatedMode) {
        // Prevent silent data loss if no real channel is attached in production (F-05)
        return false;
    }
#else
    if (!m_simulatedMode) {
        return false;
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
    {
        std::lock_guard<std::mutex> lock(m_channelMutex);
        m_dataChannel = std::move(channel);
        m_peerConnection = std::move(pc);
    }

    std::shared_ptr<rtc::DataChannel> dc;
    {
        std::lock_guard<std::mutex> lock(m_channelMutex);
        dc = m_dataChannel;
    }

    if (dc) {
        dc->setBufferedAmountLowThreshold(m_config.lowWaterMark);

        // F-04: Register onOpen callback so m_connected transitions to true when channel opens
        dc->onOpen([this]() {
            m_connected.store(true);
        });

        dc->onBufferedAmountLow([this]() {
            notifyBufferedAmountLow();
        });

        dc->onMessage([this](auto msg) {
            if (std::holds_alternative<rtc::binary>(msg)) {
                auto& bin = std::get<rtc::binary>(msg);
                injectIncomingBytes(reinterpret_cast<const uint8_t*>(bin.data()), bin.size());
            } else if (std::holds_alternative<std::string>(msg)) {
                auto& str = std::get<std::string>(msg);
                injectIncomingBytes(reinterpret_cast<const uint8_t*>(str.data()), str.size());
            }
        });

        dc->onClosed([this]() {
            close();
        });

        m_connected.store(dc->isOpen());
    }
}

std::shared_ptr<rtc::DataChannel> WebRtcDataChannel::getDataChannel() const {
    std::lock_guard<std::mutex> lock(m_channelMutex);
    return m_dataChannel;
}

std::shared_ptr<rtc::PeerConnection> WebRtcDataChannel::getPeerConnection() const {
    std::lock_guard<std::mutex> lock(m_channelMutex);
    return m_peerConnection;
}
#endif

void WebRtcDataChannel::close() {
    m_connected.store(false);
#if defined(LANDROP_HAVE_WEBRTC)
    std::shared_ptr<rtc::DataChannel> dc;
    std::shared_ptr<rtc::PeerConnection> pc;
    {
        std::lock_guard<std::mutex> lock(m_channelMutex);
        dc = std::move(m_dataChannel);
        pc = std::move(m_peerConnection);
    }
    // F-06: resetCallbacks to prevent use-after-free on teardown
    if (dc) {
        dc->resetCallbacks();
        dc->close();
    }
    if (pc) {
        pc->resetCallbacks();
        pc->close();
    }
#endif
    {
        std::lock_guard<std::mutex> lock(m_rxMutex);
        m_rxBuffer.clear();
        m_rxOffset = 0;
    }
    m_customSink = nullptr;
    m_rxCv.notify_all();
    m_txCv.notify_all();
}

} // namespace landrop
