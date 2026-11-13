#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <chrono>

#include "landrop/core/SignalingClient.hpp"
#include "landrop/core/CandidateQueue.hpp"
#include "landrop/core/WebRtcDataChannel.hpp"
#include "landrop/core/TurnConfigClient.hpp"

#if defined(LANDROP_HAVE_WEBRTC)
#include <rtc/rtc.hpp>
#endif

namespace landrop {

enum class SessionRole {
    Initiator,
    Joiner
};

enum class CoordinatorState {
    Idle,
    ConnectingSignaling,
    WaitingForPeer,
    GatheringCandidates,
    NegotiatingSdp,
    ConnectingTransport,
    Connected,
    Disconnected,
    Failed,
    Closed
};

inline const char* coordinatorStateToString(CoordinatorState state) {
    switch (state) {
        case CoordinatorState::Idle: return "Idle";
        case CoordinatorState::ConnectingSignaling: return "ConnectingSignaling";
        case CoordinatorState::WaitingForPeer: return "WaitingForPeer";
        case CoordinatorState::GatheringCandidates: return "GatheringCandidates";
        case CoordinatorState::NegotiatingSdp: return "NegotiatingSdp";
        case CoordinatorState::ConnectingTransport: return "ConnectingTransport";
        case CoordinatorState::Connected: return "Connected";
        case CoordinatorState::Disconnected: return "Disconnected";
        case CoordinatorState::Failed: return "Failed";
        case CoordinatorState::Closed: return "Closed";
        default: return "Unknown";
    }
}

inline std::string deriveWsHttpUrl(const std::string& wsUrl) {
    std::string httpUrl = wsUrl;
    if (httpUrl.rfind("wss://", 0) == 0) {
        httpUrl = "https://" + httpUrl.substr(6);
    } else if (httpUrl.rfind("ws://", 0) == 0) {
        httpUrl = "http://" + httpUrl.substr(5);
    }
    size_t pathPos = httpUrl.find('/', 8);
    if (pathPos != std::string::npos) {
        httpUrl = httpUrl.substr(0, pathPos);
    }
    return httpUrl;
}

struct CoordinatorOptions {
    int signalingTimeoutMs{10000};
    int peerJoinTimeoutMs{300000};
    int connectionTimeoutMs{30000};
    bool autoFetchTurn{true};
    std::string signalingHttpUrl;
    std::string dataChannelLabel{"lan-drop-data"};
    bool mockLoopbackMode{false};
    std::string authToken;
    std::string userId;
    std::string deviceId;
};

/**
 * @brief High-level coordinator that manages the end-to-end P2P session
 * across SignalingClient, libdatachannel PeerConnection, CandidateQueue, and WebRtcDataChannel.
 */
class WebRtcSessionCoordinator : public std::enable_shared_from_this<WebRtcSessionCoordinator> {
public:
    explicit WebRtcSessionCoordinator(
        std::string clientId = "",
        WebRtcConfig config = {},
        CoordinatorOptions options = {}
    );

    virtual ~WebRtcSessionCoordinator();

    // Callbacks
    std::function<void(const std::string& roomCode)> onRoomReady;
    std::function<void(const std::string& peerId)> onPeerJoined;
    std::function<void(std::shared_ptr<WebRtcDataChannel> channel)> onChannelOpened;
    std::function<void(CoordinatorState newState)> onStateChanged;
    std::function<void(const std::string& error)> onError;

    // Asynchronous Session Triggers
    bool startAsInitiator(const std::string& signalingWsUrl);
    bool startAsJoiner(const std::string& signalingWsUrl, const std::string& roomCode);

    // Crockford Base32 normalization and validation (DROP-XXXX-XXXX)
    static std::string normalizeRoomCode(const std::string& input, std::string* outError = nullptr);

    // Blocking wait for connection establishment (with cancellation support)
    std::shared_ptr<WebRtcDataChannel> waitForConnection(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(30000),
        std::atomic<bool>* cancelToken = nullptr,
        std::string* outError = nullptr
    );

    // Mock Loopback Wire Injection for CI and Unit Testing (TC-E2E-01, TC-E2E-02)
    void enableLoopbackMode(std::shared_ptr<WebRtcSessionCoordinator> peerCoordinator);

    std::shared_ptr<WebRtcDataChannel> getChannel() const;
    CoordinatorState getState() const;
    SessionRole getRole() const;
    std::string getRoomCode() const;
    std::string getRoomToken() const;
    std::string getPeerId() const;
    bool isConnected() const;

    void cancel();
    void close();

private:
    void setState(CoordinatorState newState);
    void setupSignalingBindings();
#if defined(LANDROP_HAVE_WEBRTC)
    void setupPeerConnection();
#endif

    std::string m_clientId;
    WebRtcConfig m_config;
    CoordinatorOptions m_options;

    mutable std::mutex m_stateMutex;
    std::condition_variable m_stateCv;
    CoordinatorState m_state{CoordinatorState::Idle};
    SessionRole m_role{SessionRole::Initiator};

    std::unique_ptr<signaling::SignalingClient> m_signaling;
    std::unique_ptr<TurnConfigClient> m_turnClient;
    CandidateQueue m_candidateQueue;
    std::shared_ptr<WebRtcDataChannel> m_channel;

    std::atomic<bool> m_cancelled{false};

#if defined(LANDROP_HAVE_WEBRTC)
    mutable std::mutex m_pcMutex;
    std::shared_ptr<rtc::PeerConnection> m_pc;
    std::shared_ptr<rtc::DataChannel> m_dc;
#endif

    std::weak_ptr<WebRtcSessionCoordinator> m_loopbackPeer;
};

} // namespace landrop
