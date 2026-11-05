#pragma once

#include "landrop/core/ITransportChannel.hpp"
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <memory>

#if defined(LANDROP_HAVE_WEBRTC)
#include <rtc/rtc.hpp>
#endif

namespace landrop {

/**
 * @brief Configuration parameters for establishing a WebRTC PeerConnection.
 */
struct WebRtcConfig {
    std::vector<std::string> iceServers = {
        "stun:stun.l.google.com:19302",
        "stun:global.stun.twilio.com:3478"
    };
    std::string turnServer;
    std::string turnUsername;
    std::string turnPassword;
    uint32_t maxBufferedAmount{16 * 1024 * 1024}; // 16 MB max send queue ceiling
    uint32_t lowWaterMark{4 * 1024 * 1024};      // 4 MB low-water mark for resume
    bool enableMdns{true};
};

/**
 * @brief WebRTC DataChannel implementation of ITransportChannel.
 * Provides transparent high-performance P2P file transfers through NAT/Firewalls
 * with non-failing backpressure flow control (TC-RTC-02).
 */
class WebRtcDataChannel : public ITransportChannel {
public:
    explicit WebRtcDataChannel(
        std::string peerId,
        std::string label = "lan-drop-data",
        WebRtcConfig config = {}
    );

    ~WebRtcDataChannel() override;

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

    size_t getBufferedAmount() const override;
    uint64_t getBytesSent() const override;
    uint64_t getBytesReceived() const override;

    void close() override;

    // Flow control & backpressure synchronization
    void notifyBufferedAmountLow();

    // Simulation & testing hooks
    void setConnected(bool connected) {
        m_connected.store(connected);
    }

    void setSimulatedMode(bool sim) {
        m_simulatedMode = sim;
    }

    bool isSimulatedMode() const {
        return m_simulatedMode;
    }

    void setBufferedAmount(size_t amount) {
        m_bufferedAmount.store(amount);
        if (amount <= m_config.lowWaterMark) {
            notifyBufferedAmountLow();
        }
    }

    void injectIncomingBytes(const uint8_t* data, size_t length);

    const WebRtcConfig& getConfig() const { return m_config; }
    const std::string& getLabel() const { return m_label; }

#if defined(LANDROP_HAVE_WEBRTC)
    void setRealDataChannel(
        std::shared_ptr<rtc::DataChannel> channel,
        std::shared_ptr<rtc::PeerConnection> pc
    );
    std::shared_ptr<rtc::DataChannel> getDataChannel() const;
    std::shared_ptr<rtc::PeerConnection> getPeerConnection() const;
#endif

private:
    std::string m_peerId;
    std::string m_label;
    WebRtcConfig m_config;
    std::atomic<bool> m_connected{false};
    bool m_simulatedMode{false};
    std::atomic<size_t> m_bufferedAmount{0};
    std::atomic<uint64_t> m_bytesSent{0};
    std::atomic<uint64_t> m_bytesReceived{0};

    mutable std::mutex m_rxMutex;
    std::condition_variable m_rxCv;
    std::vector<uint8_t> m_rxBuffer;
    size_t m_rxOffset{0};

    mutable std::mutex m_txMutex;
    std::condition_variable m_txCv;

#if defined(LANDROP_HAVE_WEBRTC)
    mutable std::mutex m_channelMutex;
    std::shared_ptr<rtc::PeerConnection> m_peerConnection;
    std::shared_ptr<rtc::DataChannel> m_dataChannel;
#endif
};

} // namespace landrop
