#include "landrop/core/WebRtcSessionCoordinator.hpp"
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include <random>
#include <sstream>
#include <iostream>

namespace landrop {

static std::string generateRandomId(const std::string& prefix) {
    thread_local std::mt19937_64 gen(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dis;
    std::stringstream ss;
    ss << prefix << "-" << std::hex << (dis(gen) & 0xFFFFFFFFFFULL);
    return ss.str();
}

WebRtcSessionCoordinator::WebRtcSessionCoordinator(
    std::string clientId,
    WebRtcConfig config,
    CoordinatorOptions options
) : m_clientId(clientId.empty() ? generateRandomId("c") : std::move(clientId)),
    m_config(std::move(config)),
    m_options(std::move(options)) {
    m_signaling = std::make_unique<signaling::SignalingClient>(m_clientId);
    m_turnClient = std::make_unique<TurnConfigClient>();
    m_channel = std::make_shared<WebRtcDataChannel>("peer-" + m_clientId, m_options.dataChannelLabel, m_config);
    setupSignalingBindings();
}

WebRtcSessionCoordinator::~WebRtcSessionCoordinator() {
    close();
}

void WebRtcSessionCoordinator::setState(CoordinatorState newState) {
    std::function<void(CoordinatorState)> cb;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (m_state == newState) return;
        m_state = newState;
        cb = onStateChanged;
    }
    m_stateCv.notify_all();
    if (cb) {
        cb(newState);
    }
}

CoordinatorState WebRtcSessionCoordinator::getState() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_state;
}

SessionRole WebRtcSessionCoordinator::getRole() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_role;
}

std::string WebRtcSessionCoordinator::getRoomCode() const {
    return m_signaling ? m_signaling->getRoomCode() : "";
}

std::string WebRtcSessionCoordinator::getRoomToken() const {
    return m_signaling ? m_signaling->getRoomToken() : "";
}

std::string WebRtcSessionCoordinator::getPeerId() const {
    return m_signaling ? m_signaling->getPeerId() : "";
}

bool WebRtcSessionCoordinator::isConnected() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_state == CoordinatorState::Connected && m_channel && m_channel->isConnected();
}

std::shared_ptr<WebRtcDataChannel> WebRtcSessionCoordinator::getChannel() const {
    return m_channel;
}

void WebRtcSessionCoordinator::cancel() {
    m_cancelled.store(true);
    close();
}

void WebRtcSessionCoordinator::close() {
    setState(CoordinatorState::Closed);
    if (m_signaling) {
        m_signaling->disconnect();
    }
    if (m_channel) {
        m_channel->close();
    }
#if defined(LANDROP_HAVE_WEBRTC)
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::DataChannel> dc;
    {
        std::lock_guard<std::mutex> lock(m_pcMutex);
        pc = std::move(m_pc);
        dc = std::move(m_dc);
    }
    if (dc) {
        dc->resetCallbacks();
        dc->close();
    }
    if (pc) {
        pc->resetCallbacks();
        pc->close();
    }
#endif
    m_candidateQueue.reset();
}

void WebRtcSessionCoordinator::setupSignalingBindings() {
    m_signaling->onConnected = [this]() {
        if (!m_options.authToken.empty()) {
            m_signaling->bindClient(m_options.authToken, m_options.userId, m_options.deviceId);
        }
    };

    m_signaling->onRoomCreated = [this](const signaling::RoomCreatedInfo& info) {
        setState(CoordinatorState::WaitingForPeer);
        if (onRoomReady) {
            onRoomReady(info.roomCode);
        }
    };

    m_signaling->onPeerJoined = [this](const signaling::PeerJoinedInfo& info) {
        if (onPeerJoined) {
            onPeerJoined(info.peerId);
        }

        // Auto fetch TURN config if requested
        std::string token = !info.roomToken.empty() ? info.roomToken : getRoomToken();
        if (m_options.autoFetchTurn && !m_options.signalingHttpUrl.empty() && !token.empty()) {
            m_turnClient->fetchTurnConfig(m_options.signalingHttpUrl, token, m_clientId, m_config);
        }

        if (m_options.mockLoopbackMode) {
            m_channel->setConnected(true);
            setState(CoordinatorState::Connected);
            if (onChannelOpened) {
                onChannelOpened(m_channel);
            }
            return;
        }

#if defined(LANDROP_HAVE_WEBRTC)
        if (m_role == SessionRole::Initiator) {
            setState(CoordinatorState::NegotiatingSdp);
            setupPeerConnection();

            rtc::DataChannelInit dcInit;
            dcInit.reliability.unordered = false;
            auto dc = m_pc->createDataChannel(m_options.dataChannelLabel, dcInit);
            {
                std::lock_guard<std::mutex> lock(m_pcMutex);
                m_dc = dc;
            }
            m_channel->setRealDataChannel(dc, m_pc);

            dc->onOpen([this]() {
                m_channel->setConnected(true);
                setState(CoordinatorState::Connected);
                if (onChannelOpened) {
                    onChannelOpened(m_channel);
                }
            });
            if (dc->isOpen()) {
                m_channel->setConnected(true);
                setState(CoordinatorState::Connected);
                if (onChannelOpened) {
                    onChannelOpened(m_channel);
                }
            }

            m_pc->setLocalDescription();
        }
#endif
    };

    m_signaling->onSdpOffer = [this](const signaling::SdpInfo& info) {
        (void)info;
        if (m_options.mockLoopbackMode) return;

#if defined(LANDROP_HAVE_WEBRTC)
        if (m_role == SessionRole::Joiner) {
            setState(CoordinatorState::NegotiatingSdp);
            setupPeerConnection();

            m_pc->setRemoteDescription(rtc::Description(info.sdp, "offer"));
            m_candidateQueue.setRemoteDescriptionSet();

            m_pc->setLocalDescription();
        }
#endif
    };

    m_signaling->onSdpAnswer = [this](const signaling::SdpInfo& info) {
        (void)info;
        if (m_options.mockLoopbackMode) return;

#if defined(LANDROP_HAVE_WEBRTC)
        if (m_role == SessionRole::Initiator && m_pc) {
            m_pc->setRemoteDescription(rtc::Description(info.sdp, "answer"));
            m_candidateQueue.setRemoteDescriptionSet();
        }
#endif
    };

    m_signaling->onIceCandidate = [this](const signaling::IceCandidateInfo& info) {
        m_candidateQueue.push({info.candidate, info.sdpMid, info.sdpMLineIndex});
    };

    m_signaling->onPeerLeft = [this](const signaling::PeerLeftInfo& info) {
        (void)info;
        setState(CoordinatorState::Disconnected);
    };

    m_signaling->onError = [this](const signaling::ErrorInfo& err) {
        if (onError) {
            onError(err.code + ": " + err.message);
        }
    };
}

#if defined(LANDROP_HAVE_WEBRTC)
void WebRtcSessionCoordinator::setupPeerConnection() {
    rtc::Configuration rtcConfig;
    for (const auto& s : m_config.iceServers) {
        rtc::IceServer server(s);
        if ((s.rfind("turn:", 0) == 0 || s.rfind("turns:", 0) == 0) && !m_config.turnUsername.empty()) {
            server.username = m_config.turnUsername;
            server.password = m_config.turnPassword;
        }
        rtcConfig.iceServers.push_back(server);
    }

    auto pc = std::make_shared<rtc::PeerConnection>(rtcConfig);
    {
        std::lock_guard<std::mutex> lock(m_pcMutex);
        m_pc = pc;
    }

    pc->onLocalCandidate([this](rtc::Candidate candidate) {
        m_signaling->sendIceCandidate(candidate.candidate(), candidate.mid(), 0);
    });

    pc->onLocalDescription([this](rtc::Description desc) {
        if (desc.type() == rtc::Description::Type::Offer) {
            if (m_role == SessionRole::Initiator) {
                m_signaling->sendSdpOffer(std::string(desc));
            }
        } else if (desc.type() == rtc::Description::Type::Answer) {
            if (m_role == SessionRole::Joiner) {
                m_signaling->sendSdpAnswer(std::string(desc));
            }
        }
    });

    pc->onStateChange([this](rtc::PeerConnection::State state) {
        if (state == rtc::PeerConnection::State::Disconnected) {
            setState(CoordinatorState::Disconnected);
        } else if (state == rtc::PeerConnection::State::Failed) {
            setState(CoordinatorState::Failed);
        }
    });

    if (m_role == SessionRole::Joiner) {
        pc->onDataChannel([this, pc](std::shared_ptr<rtc::DataChannel> dc) {
            {
                std::lock_guard<std::mutex> lock(m_pcMutex);
                m_dc = dc;
            }
            m_channel->setRealDataChannel(dc, pc);

            dc->onOpen([this]() {
                m_channel->setConnected(true);
                setState(CoordinatorState::Connected);
                if (onChannelOpened) {
                    onChannelOpened(m_channel);
                }
            });
            if (dc->isOpen()) {
                m_channel->setConnected(true);
                setState(CoordinatorState::Connected);
                if (onChannelOpened) {
                    onChannelOpened(m_channel);
                }
            }
        });
    }

    m_candidateQueue.setOnCandidateReady([pc](const IceCandidate& cand) {
        try {
            pc->addRemoteCandidate(rtc::Candidate(cand.candidate, cand.mid));
        } catch (...) {}
    });
}
#endif

bool WebRtcSessionCoordinator::startAsInitiator(const std::string& signalingWsUrl) {
    m_role = SessionRole::Initiator;
    m_signaling->setRole(signaling::Role::Initiator);
    setState(CoordinatorState::ConnectingSignaling);

    if (m_options.mockLoopbackMode) {
        m_signaling->createRoom();
        return true;
    }

    if (!m_signaling->connect(signalingWsUrl)) {
        setState(CoordinatorState::Failed);
        return false;
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_options.signalingTimeoutMs > 0 ? m_options.signalingTimeoutMs : 5000);
    while (!m_signaling->isConnected()) {
        if (std::chrono::steady_clock::now() >= deadline || m_cancelled.load()) {
            setState(CoordinatorState::Failed);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return m_signaling->createRoom();
}

std::string WebRtcSessionCoordinator::normalizeRoomCode(const std::string& input, std::string* outError) {
    if (input.empty()) {
        if (outError) *outError = "Room code cannot be empty";
        return "";
    }

    std::string str = input;
    // Trim leading whitespace
    while (!str.empty() && (str.front() == ' ' || str.front() == '\t' || str.front() == '\r' || str.front() == '\n')) {
        str.erase(str.begin());
    }
    // Trim trailing whitespace
    while (!str.empty() && (str.back() == ' ' || str.back() == '\t' || str.back() == '\r' || str.back() == '\n')) {
        str.pop_back();
    }

    // Support URLs with hash fragments or paths (e.g. https://landrop-server.duckdns.org/#DROP-06ZS-6PT1)
    size_t hashPos = str.find('#');
    if (hashPos != std::string::npos) {
        str = str.substr(hashPos + 1);
    } else if (str.rfind("http://", 0) == 0 || str.rfind("https://", 0) == 0) {
        size_t lastSlash = str.find_last_of('/');
        if (lastSlash != std::string::npos && lastSlash + 1 < str.length()) {
            str = str.substr(lastSlash + 1);
        }
    }

    std::string raw;
    raw.reserve(str.size());
    for (char c : str) {
        if (c != '-' && c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            raw.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
    }

    if (raw.rfind("DROP", 0) == 0 && raw.length() == 12) {
        raw = raw.substr(4);
    }

    const std::string kAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    std::string canonical;
    canonical.reserve(raw.size());
    for (char c : raw) {
        if (c == 'O') {
            canonical.push_back('0');
        } else if (c == 'I' || c == 'L') {
            canonical.push_back('1');
        } else if (kAlphabet.find(c) != std::string::npos) {
            canonical.push_back(c);
        } else {
            if (outError) {
                *outError = std::string("Invalid character '") + c + "' in room code";
            }
            return "";
        }
    }

    if (canonical.length() != 8) {
        if (outError) {
            *outError = "Invalid room code length: expected 8 characters, got " + std::to_string(canonical.length());
        }
        return "";
    }

    return "DROP-" + canonical.substr(0, 4) + "-" + canonical.substr(4, 4);
}

bool WebRtcSessionCoordinator::startAsJoiner(const std::string& signalingWsUrl, const std::string& roomCode) {
    m_role = SessionRole::Joiner;
    m_signaling->setRole(signaling::Role::Joiner);
    setState(CoordinatorState::ConnectingSignaling);

    std::string normErr;
    std::string normalizedCode = normalizeRoomCode(roomCode, &normErr);
    if (normalizedCode.empty()) {
        if (onError) {
            onError("ERR_INVALID_ROOM_CODE: " + normErr);
        }
        setState(CoordinatorState::Failed);
        return false;
    }

    if (m_options.mockLoopbackMode) {
        m_signaling->joinRoom(normalizedCode);
        return true;
    }

    if (!m_signaling->connect(signalingWsUrl)) {
        setState(CoordinatorState::Failed);
        return false;
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_options.signalingTimeoutMs > 0 ? m_options.signalingTimeoutMs : 5000);
    while (!m_signaling->isConnected()) {
        if (std::chrono::steady_clock::now() >= deadline || m_cancelled.load()) {
            setState(CoordinatorState::Failed);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return m_signaling->joinRoom(normalizedCode);
}

std::shared_ptr<WebRtcDataChannel> WebRtcSessionCoordinator::waitForConnection(
    std::chrono::milliseconds timeout,
    std::atomic<bool>* cancelToken,
    std::string* outError
) {
    std::unique_lock<std::mutex> lock(m_stateMutex);
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (m_state != CoordinatorState::Connected) {
        if (m_cancelled.load() || (cancelToken && cancelToken->load())) {
            if (outError) *outError = "Connection cancelled by caller";
            return nullptr;
        }

        if (m_state == CoordinatorState::Failed || m_state == CoordinatorState::Closed) {
            if (outError) *outError = "Session failed or closed";
            return nullptr;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            if (outError) *outError = "Timed out waiting for WebRTC connection";
            return nullptr;
        }

        m_stateCv.wait_for(lock, std::chrono::milliseconds(50), [&]() {
            return m_state == CoordinatorState::Connected ||
                   m_state == CoordinatorState::Failed ||
                   m_state == CoordinatorState::Closed ||
                   m_cancelled.load() ||
                   (cancelToken && cancelToken->load());
        });
    }

    return m_channel;
}

void WebRtcSessionCoordinator::enableLoopbackMode(std::shared_ptr<WebRtcSessionCoordinator> peerCoordinator) {
    if (!peerCoordinator) return;

    m_options.mockLoopbackMode = true;
    peerCoordinator->m_options.mockLoopbackMode = true;

    m_loopbackPeer = peerCoordinator;
    peerCoordinator->m_loopbackPeer = shared_from_this();

    struct MockBrokerState {
        std::mutex mutex;
        std::string roomCode = "DROP-K8M4-P9X2";
        std::string roomToken = "mock-token-loopback";
    };
    auto broker = std::make_shared<MockBrokerState>();

    auto selfCoordWeak = weak_from_this();
    auto peerCoordWeak = m_loopbackPeer;

    m_signaling->setCustomSender([selfCoordWeak, peerCoordWeak, broker](const std::string& msg) {
        auto self = selfCoordWeak.lock();
        auto peer = peerCoordWeak.lock();
        if (!self) return false;

        json env;
        try {
            env = json::parse(msg);
        } catch (...) {
            return false;
        }

        std::string action = env.value("action", "");
        if (action == "CREATE_ROOM") {
            json resp;
            resp["version"] = "2.0";
            resp["action"] = "ROOM_CREATED";
            resp["client_id"] = "server";
            resp["timestamp"] = 1700000000;
            resp["payload"] = {
                {"room_code", broker->roomCode},
                {"room_token", broker->roomToken}
            };
            return self->m_signaling->handleIncomingMessage(resp.dump());
        }

        if (action == "JOIN_ROOM") {
            json respSelf;
            respSelf["version"] = "2.0";
            respSelf["action"] = "PEER_JOINED";
            respSelf["client_id"] = "server";
            respSelf["timestamp"] = 1700000001;
            respSelf["payload"] = {
                {"peer_id", peer ? peer->m_clientId : "peer"},
                {"room_code", broker->roomCode},
                {"room_token", broker->roomToken}
            };
            self->m_signaling->handleIncomingMessage(respSelf.dump());

            if (peer) {
                json respPeer;
                respPeer["version"] = "2.0";
                respPeer["action"] = "PEER_JOINED";
                respPeer["client_id"] = "server";
                respPeer["timestamp"] = 1700000001;
                respPeer["payload"] = {
                    {"peer_id", self->m_clientId},
                    {"room_code", broker->roomCode},
                    {"room_token", broker->roomToken}
                };
                peer->m_signaling->handleIncomingMessage(respPeer.dump());
            }
            return true;
        }

        if (peer) {
            return peer->m_signaling->handleIncomingMessage(msg);
        }
        return false;
    });

    peerCoordinator->m_signaling->setCustomSender([peerCoordWeak, selfCoordWeak, broker](const std::string& msg) {
        auto self = peerCoordWeak.lock();
        auto peer = selfCoordWeak.lock();
        if (!self) return false;

        json env;
        try {
            env = json::parse(msg);
        } catch (...) {
            return false;
        }

        std::string action = env.value("action", "");
        if (action == "CREATE_ROOM") {
            json resp;
            resp["version"] = "2.0";
            resp["action"] = "ROOM_CREATED";
            resp["client_id"] = "server";
            resp["timestamp"] = 1700000000;
            resp["payload"] = {
                {"room_code", broker->roomCode},
                {"room_token", broker->roomToken}
            };
            return self->m_signaling->handleIncomingMessage(resp.dump());
        }

        if (action == "JOIN_ROOM") {
            json respSelf;
            respSelf["version"] = "2.0";
            respSelf["action"] = "PEER_JOINED";
            respSelf["client_id"] = "server";
            respSelf["timestamp"] = 1700000001;
            respSelf["payload"] = {
                {"peer_id", peer ? peer->m_clientId : "peer"},
                {"room_code", broker->roomCode},
                {"room_token", broker->roomToken}
            };
            self->m_signaling->handleIncomingMessage(respSelf.dump());

            if (peer) {
                json respPeer;
                respPeer["version"] = "2.0";
                respPeer["action"] = "PEER_JOINED";
                respPeer["client_id"] = "server";
                respPeer["timestamp"] = 1700000001;
                respPeer["payload"] = {
                    {"peer_id", self->m_clientId},
                    {"room_code", broker->roomCode},
                    {"room_token", broker->roomToken}
                };
                peer->m_signaling->handleIncomingMessage(respPeer.dump());
            }
            return true;
        }

        if (peer) {
            return peer->m_signaling->handleIncomingMessage(msg);
        }
        return false;
    });

    auto selfChan = m_channel;
    auto peerChan = peerCoordinator->m_channel;

    selfChan->setSimulatedMode(true);
    peerChan->setSimulatedMode(true);

    std::weak_ptr<WebRtcDataChannel> selfChanWeak = selfChan;
    std::weak_ptr<WebRtcDataChannel> peerChanWeak = peerChan;

    selfChan->setCustomSink([peerChanWeak](const uint8_t* data, size_t len) {
        if (auto p = peerChanWeak.lock()) {
            p->injectIncomingBytes(data, len);
            return true;
        }
        return false;
    });

    peerChan->setCustomSink([selfChanWeak](const uint8_t* data, size_t len) {
        if (auto s = selfChanWeak.lock()) {
            s->injectIncomingBytes(data, len);
            return true;
        }
        return false;
    });
}


} // namespace landrop
