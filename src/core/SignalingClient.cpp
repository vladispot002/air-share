#include "landrop/core/SignalingClient.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <random>
#include <sstream>

using json = nlohmann::json;

namespace landrop::signaling {

static std::string generateRandomId(size_t len = 12) {
    static const char hexChars[] = "0123456789abcdef";
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<size_t> dis(0, 15);
    std::string id;
    id.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        id += hexChars[dis(gen)];
    }
    return id;
}

SignalingClient::SignalingClient(std::string clientId)
    : m_clientId(std::move(clientId)) {
    if (m_clientId.empty()) {
        m_clientId = "client_" + generateRandomId(12);
    }
}

SignalingClient::~SignalingClient() {
    disconnect();
}

bool SignalingClient::connect(const std::string& wsUrl) {
    if (m_customSender) {
        m_connected.store(true);
        if (onConnected) {
            onConnected();
        }
        return true;
    }

#if defined(LANDROP_HAVE_WEBRTC)
    try {
        m_ws = std::make_shared<rtc::WebSocket>();

        m_ws->onOpen([this]() {
            m_connected.store(true);
            if (onConnected) {
                onConnected();
            }
        });

        m_ws->onClosed([this]() {
            m_connected.store(false);
            if (onDisconnected) {
                onDisconnected();
            }
        });

        m_ws->onError([this](std::string error) {
            if (onError) {
                onError({"ERR_WEBSOCKET", std::move(error)});
            }
        });

        m_ws->onMessage([this](auto msg) {
            if (std::holds_alternative<std::string>(msg)) {
                handleIncomingMessage(std::get<std::string>(msg));
            } else if (std::holds_alternative<rtc::binary>(msg)) {
                auto& bin = std::get<rtc::binary>(msg);
                std::string s(reinterpret_cast<const char*>(bin.data()), bin.size());
                handleIncomingMessage(s);
            }
        });

        m_ws->open(wsUrl);
        return true;
    } catch (const std::exception& e) {
        if (onError) {
            onError({"ERR_WEBSOCKET_OPEN", e.what()});
        }
        return false;
    }
#else
    (void)wsUrl;
    if (onError) {
        onError({"ERR_NOT_SUPPORTED", "WebRTC / WebSocket transport not compiled in"});
    }
    return false;
#endif
}

void SignalingClient::disconnect() {
    m_connected.store(false);
#if defined(LANDROP_HAVE_WEBRTC)
    if (m_ws) {
        m_ws->close();
        m_ws.reset();
    }
#endif
    if (onDisconnected) {
        onDisconnected();
    }
}

bool SignalingClient::isConnected() const {
    return m_connected.load();
}

void SignalingClient::setCustomSender(SendFunc sender) {
    m_customSender = std::move(sender);
}

Role SignalingClient::getRole() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_role;
}

void SignalingClient::setRole(Role role) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_role = role;
}

std::string SignalingClient::getRoomCode() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_roomCode;
}

std::string SignalingClient::getRoomToken() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_roomToken;
}

std::string SignalingClient::getClientId() const {
    return m_clientId;
}

std::string SignalingClient::getPeerId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_peerId;
}

void SignalingClient::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_role = Role::Unknown;
    m_roomCode.clear();
    m_roomToken.clear();
    m_peerId.clear();
}

bool SignalingClient::sendEnvelope(const std::string& action, const std::string& payloadJson) {
    json env;
    env["version"] = "2.0";
    env["action"] = action;
    env["client_id"] = m_clientId;
    
    int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    env["timestamp"] = nowSec;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_roomCode.empty()) {
            env["room_code"] = m_roomCode;
        }
    }

    if (!payloadJson.empty()) {
        try {
            env["payload"] = json::parse(payloadJson);
        } catch (...) {
            return false;
        }
    }

    std::string text = env.dump();

    if (m_customSender) {
        return m_customSender(text);
    }

#if defined(LANDROP_HAVE_WEBRTC)
    if (m_ws && m_ws->isOpen()) {
        return m_ws->send(text);
    }
#endif

    return false;
}

bool SignalingClient::createRoom() {
    return sendEnvelope("CREATE_ROOM");
}

bool SignalingClient::joinRoom(const std::string& roomCode) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_roomCode = roomCode;
    }
    json p;
    p["room_code"] = roomCode;
    return sendEnvelope("JOIN_ROOM", p.dump());
}

bool SignalingClient::sendSdpOffer(const std::string& sdp, const std::string& e2eeKey) {
    // Glare resolution: Joiner MUST NOT send SDP offer
    bool isJoiner = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        isJoiner = (m_role == Role::Joiner);
    }
    if (isJoiner) {
        if (onError) {
            onError({"ERR_GLARE_VIOLATION", "Role 'joiner' cannot send SDP offer"});
        }
        return false;
    }

    json p;
    p["type"] = "offer";
    p["sdp"] = sdp;
    if (!e2eeKey.empty()) {
        p["e2ee_public_key"] = e2eeKey;
    }
    return sendEnvelope("SDP_OFFER", p.dump());
}

bool SignalingClient::sendSdpAnswer(const std::string& sdp, const std::string& e2eeKey) {
    // Glare resolution: Initiator MUST NOT send SDP answer
    bool isInitiator = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        isInitiator = (m_role == Role::Initiator);
    }
    if (isInitiator) {
        if (onError) {
            onError({"ERR_GLARE_VIOLATION", "Role 'initiator' cannot send SDP answer"});
        }
        return false;
    }

    json p;
    p["type"] = "answer";
    p["sdp"] = sdp;
    if (!e2eeKey.empty()) {
        p["e2ee_public_key"] = e2eeKey;
    }
    return sendEnvelope("SDP_ANSWER", p.dump());
}

bool SignalingClient::sendIceCandidate(const std::string& candidate, const std::string& sdpMid, int sdpMLineIndex) {
    json p;
    p["candidate"] = candidate;
    p["sdp_mid"] = sdpMid;
    p["sdp_mline_index"] = sdpMLineIndex;
    return sendEnvelope("ICE_CANDIDATE", p.dump());
}

bool SignalingClient::leaveRoom(const std::string& reason) {
    json p;
    p["reason"] = reason;
    bool ok = sendEnvelope("LEAVE_ROOM", p.dump());
    reset();
    return ok;
}

bool SignalingClient::handleIncomingMessage(const std::string& rawJson) {
    json env;
    try {
        env = json::parse(rawJson);
    } catch (const std::exception& e) {
        if (onError) {
            onError({"ERR_INVALID_MESSAGE", std::string("Malformed JSON envelope: ") + e.what()});
        }
        return false;
    }

    if (!env.is_object() || !env.contains("action") || !env["action"].is_string()) {
        if (onError) {
            onError({"ERR_INVALID_MESSAGE", "Envelope missing required string 'action' field"});
        }
        return false;
    }

    std::string action = env["action"].get<std::string>();
    json payload = env.value("payload", json::object());

    if (action == "ROOM_CREATED") {
        RoomCreatedInfo info;
        info.roomCode = payload.value("room_code", "");
        info.roomToken = payload.value("room_token", "");
        info.role = payload.value("role", "initiator");
        info.expiresAt = payload.value("expires_at", static_cast<int64_t>(0));

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_roomCode = info.roomCode;
            m_roomToken = info.roomToken;
            m_role = Role::Initiator;
        }

        if (onRoomCreated) {
            onRoomCreated(info);
        }
        return true;
    }

    if (action == "PEER_JOINED") {
        PeerJoinedInfo info;
        info.roomCode = payload.value("room_code", "");
        info.peerId = payload.value("peer_id", "");
        info.role = payload.value("role", "");
        info.roomToken = payload.value("room_token", "");

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_peerId = info.peerId;
            if (info.role == "joiner" && m_role != Role::Initiator) {
                m_role = Role::Joiner;
            }
            if (!info.roomToken.empty()) {
                m_roomToken = info.roomToken;
            }
        }

        if (onPeerJoined) {
            onPeerJoined(info);
        }
        return true;
    }

    if (action == "SDP_OFFER") {
        SdpInfo info;
        info.type = payload.value("type", "offer");
        info.sdp = payload.value("sdp", "");
        info.e2eePublicKey = payload.value("e2ee_public_key", "");

        if (onSdpOffer) {
            onSdpOffer(info);
        }
        return true;
    }

    if (action == "SDP_ANSWER") {
        SdpInfo info;
        info.type = payload.value("type", "answer");
        info.sdp = payload.value("sdp", "");
        info.e2eePublicKey = payload.value("e2ee_public_key", "");

        if (onSdpAnswer) {
            onSdpAnswer(info);
        }
        return true;
    }

    if (action == "ICE_CANDIDATE") {
        IceCandidateInfo info;
        info.candidate = payload.value("candidate", "");
        info.sdpMid = payload.value("sdp_mid", "");
        info.sdpMLineIndex = payload.value("sdp_mline_index", 0);

        if (onIceCandidate) {
            onIceCandidate(info);
        }
        return true;
    }

    if (action == "PEER_LEFT") {
        PeerLeftInfo info;
        info.peerId = payload.value("peer_id", "");
        info.reason = payload.value("reason", "");

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_peerId.clear();
        }

        if (onPeerLeft) {
            onPeerLeft(info);
        }
        return true;
    }

    if (action == "ERROR") {
        ErrorInfo info;
        info.code = payload.value("code", "ERR_GENERIC");
        info.message = payload.value("message", "");

        if (onError) {
            onError(info);
        }
        return true;
    }

    return true;
}

} // namespace landrop::signaling
