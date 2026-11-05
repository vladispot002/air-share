#pragma once

#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <cstdint>

#if defined(LANDROP_HAVE_WEBRTC)
#include <rtc/rtc.hpp>
#include <rtc/websocket.hpp>
#endif

namespace landrop::signaling {

/**
 * @brief Endpoint role in room negotiation for glare avoidance.
 */
enum class Role {
    Unknown,
    Initiator,
    Joiner
};

struct RoomCreatedInfo {
    std::string roomCode;
    std::string roomToken;
    std::string role;
    int64_t expiresAt{0};
};

struct PeerJoinedInfo {
    std::string roomCode;
    std::string peerId;
    std::string role;
    std::string roomToken;
};

struct SdpInfo {
    std::string type; // "offer" or "answer"
    std::string sdp;
    std::string e2eePublicKey;
};

struct IceCandidateInfo {
    std::string candidate;
    std::string sdpMid;
    int sdpMLineIndex{0};
};

struct PeerLeftInfo {
    std::string peerId;
    std::string reason;
};

struct ErrorInfo {
    std::string code;
    std::string message;
};

/**
 * @brief High-level C++ client for the LAN-Drop Go Signaling Service.
 * Implements Protocol v2 JSON envelopes, glare prevention, room coordination,
 * and ICE/SDP exchange over WebSockets.
 */
class SignalingClient {
public:
    using SendFunc = std::function<bool(const std::string&)>;

    explicit SignalingClient(std::string clientId = "");
    virtual ~SignalingClient();

    // Event Callbacks
    std::function<void()> onConnected;
    std::function<void()> onDisconnected;
    std::function<void(const RoomCreatedInfo&)> onRoomCreated;
    std::function<void(const PeerJoinedInfo&)> onPeerJoined;
    std::function<void(const SdpInfo&)> onSdpOffer;
    std::function<void(const SdpInfo&)> onSdpAnswer;
    std::function<void(const IceCandidateInfo&)> onIceCandidate;
    std::function<void(const PeerLeftInfo&)> onPeerLeft;
    std::function<void(const ErrorInfo&)> onError;

    // Outgoing Protocol Actions
    bool createRoom();
    bool joinRoom(const std::string& roomCode);
    bool sendSdpOffer(const std::string& sdp, const std::string& e2eeKey = "");
    bool sendSdpAnswer(const std::string& sdp, const std::string& e2eeKey = "");
    bool sendIceCandidate(const std::string& candidate, const std::string& sdpMid, int sdpMLineIndex);
    bool leaveRoom(const std::string& reason = "user_exit");

    // Message Ingress & Simulation
    bool handleIncomingMessage(const std::string& rawJson);

    // Transport Connection Lifecycle
    bool connect(const std::string& wsUrl);
    void disconnect();
    bool isConnected() const;

    // Mocking / Custom transport injection for headless testing
    void setCustomSender(SendFunc sender);

    // State Accessors
    Role getRole() const;
    void setRole(Role role);
    std::string getRoomCode() const;
    std::string getRoomToken() const;
    std::string getClientId() const;
    std::string getPeerId() const;

    void reset();

private:
    bool sendEnvelope(const std::string& action, const std::string& payloadJson = "");

    std::string m_clientId;
    mutable std::mutex m_mutex;
    Role m_role{Role::Unknown};
    std::string m_roomCode;
    std::string m_roomToken;
    std::string m_peerId;

    std::atomic<bool> m_connected{false};
    SendFunc m_customSender;

#if defined(LANDROP_HAVE_WEBRTC)
    std::shared_ptr<rtc::WebSocket> m_ws;
#endif
};

} // namespace landrop::signaling
