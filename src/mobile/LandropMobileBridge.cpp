#include "landrop/mobile/LandropMobileBridge.h"
#include "landrop/mobile/NfcPairingSession.hpp"
#include "landrop/mobile/AirDropProtocolBridge.hpp"
#include "landrop/core/MulticastBeacon.hpp"
#include "landrop/core/NetworkSession.hpp"
#include "landrop/tasks/P2PTransferTask.hpp"
#include "landrop/peers/LocalPeer.hpp"
#include "landrop/ui/QrCodeGenerator.hpp"
#include "landrop/core/WebRtcSessionCoordinator.hpp"
#include "landrop/core/TransferEngine.hpp"

#include <string>
#include <vector>
#include <memory>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

struct LandropMobileContext {
    mutable std::recursive_mutex mutex;
    std::string deviceName;
    std::string deviceUuid;
    landrop::mobile::NfcPairingSession nfcSession;
    landrop::mobile::AirDropProtocolBridge airdropBridge;
    landrop::MulticastBeacon beacon;
    std::vector<std::shared_ptr<landrop::LocalPeer>> discoveredPeers;

    // WebRTC Session Coordination
    std::shared_ptr<landrop::WebRtcSessionCoordinator> coordinator;
    std::shared_ptr<landrop::TransferEngine> transferEngine;
    std::shared_ptr<std::atomic<bool>> transferCancelToken;
    std::thread transferWorker;

    // Real-time transfer snapshot
    LandropTransferSnapshot snapshot{};

    LandropMobileContext(std::string name, std::string uuid)
        : deviceName(std::move(name)),
          deviceUuid(std::move(uuid)),
          nfcSession(deviceUuid, deviceName),
          airdropBridge(deviceUuid, deviceName),
          beacon(deviceName, deviceUuid),
          transferEngine(std::make_shared<landrop::TransferEngine>()),
          transferCancelToken(std::make_shared<std::atomic<bool>>(false)) {
        std::memset(&snapshot, 0, sizeof(snapshot));
    }

    ~LandropMobileContext() {
        if (transferCancelToken) {
            *transferCancelToken = true;
        }
        if (transferWorker.joinable()) {
            transferWorker.join();
        }
        if (coordinator) {
            coordinator->close();
        }
    }
};

static int copyStringToBuffer(const std::string& src, char* out_buf, int buf_cap, int* out_required_len) {
    int required = static_cast<int>(src.size() + 1);
    if (out_required_len) {
        *out_required_len = required;
    }
    if (!out_buf || buf_cap <= 0) {
        return LANDROP_ERR_BUFFER_TOO_SMALL;
    }
    if (buf_cap < required) {
        out_buf[0] = '\0';
        return LANDROP_ERR_BUFFER_TOO_SMALL;
    }
    std::memcpy(out_buf, src.c_str(), src.size());
    out_buf[src.size()] = '\0';
    return LANDROP_OK;
}

extern "C" {

/* ========================================================================= */
/* 1. Context Lifecycle Management                                           */
/* ========================================================================= */
LandropHandle landrop_mobile_create(const char* device_name, const char* device_uuid) {
    try {
        std::string name = device_name ? device_name : "MobileDevice";
        std::string uuid = device_uuid ? device_uuid : "mob-uuid-default";
        auto* ctx = new LandropMobileContext(name, uuid);
        return static_cast<LandropHandle>(ctx);
    } catch (...) {
        return nullptr;
    }
}

void landrop_mobile_destroy(LandropHandle handle) {
    if (handle) {
        auto* ctx = static_cast<LandropMobileContext*>(handle);
        delete ctx;
    }
}

/* ========================================================================= */
/* 2. Optical Pairing & QR Code Generation (Zero-Dependency ISO/IEC 18004)    */
/* ========================================================================= */
int landrop_mobile_qr_generate_svg(
    const char* text,
    int ecc_level,
    int border_modules,
    const char* fg_hex,
    const char* bg_hex,
    char* out_buf,
    int buf_cap,
    int* out_required_len
) {
    if (!text) return LANDROP_ERR_INVALID_ARG;
    landrop::ui::QrEccLevel ecc = landrop::ui::QrEccLevel::Medium;
    if (ecc_level >= 0 && ecc_level <= 3) {
        ecc = static_cast<landrop::ui::QrEccLevel>(ecc_level);
    }
    std::string fg = fg_hex ? fg_hex : "#000000";
    std::string bg = bg_hex ? bg_hex : "#ffffff";
    if (border_modules < 0) border_modules = 4;

    try {
        auto qr = landrop::ui::QrCodeGenerator::encodeText(text, ecc);
        std::string svg = qr.toSvg(border_modules, fg, bg);
        return copyStringToBuffer(svg, out_buf, buf_cap, out_required_len);
    } catch (const std::exception&) {
        return LANDROP_ERR_INTERNAL;
    }
}

int landrop_mobile_qr_generate_matrix(
    const char* text,
    int ecc_level,
    uint8_t* out_matrix,
    int matrix_cap,
    int* out_size
) {
    if (!text) return LANDROP_ERR_INVALID_ARG;
    landrop::ui::QrEccLevel ecc = landrop::ui::QrEccLevel::Medium;
    if (ecc_level >= 0 && ecc_level <= 3) {
        ecc = static_cast<landrop::ui::QrEccLevel>(ecc_level);
    }
    try {
        auto qr = landrop::ui::QrCodeGenerator::encodeText(text, ecc);
        int size = qr.getSize();
        if (out_size) {
            *out_size = size;
        }
        int required = size * size;
        if (!out_matrix || matrix_cap < required) {
            return LANDROP_ERR_BUFFER_TOO_SMALL;
        }
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                out_matrix[y * size + x] = qr.getModule(x, y) ? 1 : 0;
            }
        }
        return LANDROP_OK;
    } catch (const std::exception&) {
        return LANDROP_ERR_INTERNAL;
    }
}

int landrop_mobile_qr_generate_pairing_uri(
    const char* room_code,
    const char* host_domain,
    char* out_uri,
    int uri_cap
) {
    if (!room_code) return LANDROP_ERR_INVALID_ARG;
    std::string host = (host_domain && host_domain[0] != '\0') ? host_domain : "drop.local";
    std::string uri = "https://" + host + "/#" + room_code;
    return copyStringToBuffer(uri, out_uri, uri_cap, nullptr);
}

/* ========================================================================= */
/* 3. Crockford Base32 Room Code Normalization                               */
/* ========================================================================= */
int landrop_mobile_normalize_room_code(
    const char* input_code,
    char* out_buf,
    int buf_cap,
    char* out_err,
    int err_cap
) {
    if (!input_code) return LANDROP_ERR_INVALID_ARG;
    std::string err;
    std::string norm = landrop::WebRtcSessionCoordinator::normalizeRoomCode(input_code, &err);
    if (norm.empty()) {
        if (out_err && err_cap > 0) {
            std::strncpy(out_err, err.c_str(), static_cast<size_t>(err_cap - 1));
            out_err[err_cap - 1] = '\0';
        }
        if (out_buf && buf_cap > 0) out_buf[0] = '\0';
        return LANDROP_ERR_INVALID_ROOM_CODE;
    }
    return copyStringToBuffer(norm, out_buf, buf_cap, nullptr);
}

/* ========================================================================= */
/* 4. Asynchronous WebRTC Ephemeral Session Management                       */
/* ========================================================================= */
int landrop_mobile_webrtc_initiate_room(
    LandropHandle handle,
    const char* signaling_ws_url,
    const char* signaling_http_url
) {
    if (!handle) return LANDROP_ERR_INVALID_HANDLE;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mutex);

    std::string wsUrl = (signaling_ws_url && signaling_ws_url[0] != '\0')
        ? signaling_ws_url : "ws://127.0.0.1:8080/ws/signaling";

    landrop::CoordinatorOptions opts;
    if (signaling_http_url && signaling_http_url[0] != '\0') {
        opts.signalingHttpUrl = signaling_http_url;
    } else {
        opts.signalingHttpUrl = landrop::deriveWsHttpUrl(wsUrl);
    }
    opts.autoFetchTurn = true;

    ctx->coordinator = std::make_shared<landrop::WebRtcSessionCoordinator>(ctx->deviceUuid, landrop::WebRtcConfig{}, opts);

    // Non-blocking asynchronous room initiation
    std::thread([coordinator = ctx->coordinator, wsUrl]() {
        coordinator->startAsInitiator(wsUrl);
    }).detach();

    return LANDROP_OK;
}

int landrop_mobile_webrtc_join_room(
    LandropHandle handle,
    const char* signaling_ws_url,
    const char* signaling_http_url,
    const char* room_code
) {
    if (!handle) return LANDROP_ERR_INVALID_HANDLE;
    if (!room_code) return LANDROP_ERR_INVALID_ARG;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mutex);

    std::string err;
    std::string norm = landrop::WebRtcSessionCoordinator::normalizeRoomCode(room_code, &err);
    if (norm.empty()) {
        return LANDROP_ERR_INVALID_ROOM_CODE;
    }

    std::string wsUrl = (signaling_ws_url && signaling_ws_url[0] != '\0')
        ? signaling_ws_url : "ws://127.0.0.1:8080/ws/signaling";

    landrop::CoordinatorOptions opts;
    if (signaling_http_url && signaling_http_url[0] != '\0') {
        opts.signalingHttpUrl = signaling_http_url;
    } else {
        opts.signalingHttpUrl = landrop::deriveWsHttpUrl(wsUrl);
    }
    opts.autoFetchTurn = true;

    ctx->coordinator = std::make_shared<landrop::WebRtcSessionCoordinator>(ctx->deviceUuid, landrop::WebRtcConfig{}, opts);

    // Non-blocking asynchronous room join
    std::thread([coordinator = ctx->coordinator, wsUrl, norm]() {
        coordinator->startAsJoiner(wsUrl, norm);
    }).detach();

    return LANDROP_OK;
}

int landrop_mobile_webrtc_get_state(LandropHandle handle) {
    if (!handle) return LANDROP_ERR_INVALID_HANDLE;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
    if (!ctx->coordinator) return LANDROP_SESSION_IDLE;

    switch (ctx->coordinator->getState()) {
        case landrop::CoordinatorState::Idle: return LANDROP_SESSION_IDLE;
        case landrop::CoordinatorState::ConnectingSignaling: return LANDROP_SESSION_CONNECTING_SIGNALING;
        case landrop::CoordinatorState::WaitingForPeer: return LANDROP_SESSION_WAITING_FOR_PEER;
        case landrop::CoordinatorState::GatheringCandidates: return LANDROP_SESSION_GATHERING_CANDIDATES;
        case landrop::CoordinatorState::NegotiatingSdp: return LANDROP_SESSION_NEGOTIATING_SDP;
        case landrop::CoordinatorState::ConnectingTransport: return LANDROP_SESSION_CONNECTING_TRANSPORT;
        case landrop::CoordinatorState::Connected: return LANDROP_SESSION_CONNECTED;
        case landrop::CoordinatorState::Disconnected: return LANDROP_SESSION_DISCONNECTED;
        case landrop::CoordinatorState::Failed: return LANDROP_SESSION_FAILED;
        case landrop::CoordinatorState::Closed: return LANDROP_SESSION_CLOSED;
    }
    return LANDROP_SESSION_IDLE;
}

int landrop_mobile_webrtc_get_room_code(LandropHandle handle, char* out_buf, int buf_cap) {
    if (!handle) return LANDROP_ERR_INVALID_HANDLE;
    if (!out_buf || buf_cap <= 0) return LANDROP_ERR_INVALID_ARG;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
    if (!ctx->coordinator) return LANDROP_ERR_ILLEGAL_STATE;
    std::string code = ctx->coordinator->getRoomCode();
    return copyStringToBuffer(code, out_buf, buf_cap, nullptr);
}

int landrop_mobile_webrtc_get_peer_id(LandropHandle handle, char* out_buf, int buf_cap) {
    if (!handle) return LANDROP_ERR_INVALID_HANDLE;
    if (!out_buf || buf_cap <= 0) return LANDROP_ERR_INVALID_ARG;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
    if (!ctx->coordinator) return LANDROP_ERR_ILLEGAL_STATE;
    std::string peer = ctx->coordinator->getPeerId();
    return copyStringToBuffer(peer, out_buf, buf_cap, nullptr);
}

void landrop_mobile_webrtc_close(LandropHandle handle) {
    if (!handle) return;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    std::thread worker;
    std::shared_ptr<landrop::WebRtcSessionCoordinator> coord;
    {
        std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
        if (ctx->transferCancelToken) {
            *ctx->transferCancelToken = true;
        }
        if (ctx->transferWorker.joinable()) {
            worker = std::move(ctx->transferWorker);
        }
        coord = std::move(ctx->coordinator);
    }
    if (worker.joinable()) {
        worker.join();
    }
    if (coord) {
        coord->close();
    }
}

/* ========================================================================= */
/* 5. Non-Blocking File Streaming over WebRTC DataChannel                    */
/* ========================================================================= */
int landrop_mobile_webrtc_send_file_async(
    LandropHandle handle,
    const char* file_path,
    uint32_t speed_limit_kbps
) {
    if (!handle) return LANDROP_ERR_INVALID_HANDLE;
    if (!file_path) return LANDROP_ERR_INVALID_ARG;
    auto* ctx = static_cast<LandropMobileContext*>(handle);

    std::thread oldWorker;
    {
        std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
        if (!ctx->coordinator || ctx->coordinator->getState() != landrop::CoordinatorState::Connected) {
            return LANDROP_ERR_ILLEGAL_STATE;
        }
        auto channel = ctx->coordinator->getChannel();
        if (!channel || !channel->isConnected()) {
            return LANDROP_ERR_NETWORK_DISCONNECTED;
        }

        if (ctx->snapshot.transfer_state == LANDROP_XFER_IN_PROGRESS) {
            return LANDROP_ERR_ILLEGAL_STATE;
        }

        if (ctx->transferWorker.joinable()) {
            oldWorker = std::move(ctx->transferWorker);
        }
    }

    if (oldWorker.joinable()) {
        oldWorker.join();
    }

    std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
    if (!ctx->coordinator || ctx->coordinator->getState() != landrop::CoordinatorState::Connected) {
        return LANDROP_ERR_ILLEGAL_STATE;
    }
    auto channel = ctx->coordinator->getChannel();
    if (!channel || !channel->isConnected()) {
        return LANDROP_ERR_NETWORK_DISCONNECTED;
    }
    if (ctx->snapshot.transfer_state == LANDROP_XFER_IN_PROGRESS) {
        return LANDROP_ERR_ILLEGAL_STATE;
    }

    ctx->transferCancelToken = std::make_shared<std::atomic<bool>>(false);
    std::string pathStd = file_path;
    std::string baseName;
    try {
        baseName = std::filesystem::path(pathStd).filename().string();
    } catch (...) {
        baseName = pathStd;
    }
    std::string taskId = "mob-tx-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());

    std::memset(&ctx->snapshot, 0, sizeof(ctx->snapshot));
    ctx->snapshot.transfer_state = LANDROP_XFER_IN_PROGRESS;
    std::strncpy(ctx->snapshot.file_name, baseName.c_str(), sizeof(ctx->snapshot.file_name) - 1);
    ctx->snapshot.file_name[sizeof(ctx->snapshot.file_name) - 1] = '\0';

    ctx->transferWorker = std::thread([ctx, channel, taskId, pathStd, speed_limit_kbps]() {
        landrop::TransferOptions opts;
        opts.closeChannelOnCompletion = false;
        opts.speedLimitKbps = speed_limit_kbps;

        std::string err;
        bool ok = ctx->transferEngine->sendFileOverChannel(
            taskId,
            pathStd,
            channel,
            opts,
            [ctx](const landrop::TransferProgressUpdate& upd) {
                std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
                ctx->snapshot.bytes_transferred = upd.bytesTransferred;
                ctx->snapshot.total_bytes = upd.totalBytes;
                ctx->snapshot.progress_percentage = upd.progressPercentage;
                ctx->snapshot.speed_bytes_per_sec = upd.speedBytesPerSec;
                ctx->snapshot.transfer_state = LANDROP_XFER_IN_PROGRESS;
            },
            ctx->transferCancelToken.get(),
            &err
        );

        std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
        if (ok) {
            ctx->snapshot.transfer_state = LANDROP_XFER_COMPLETED;
            ctx->snapshot.progress_percentage = 100.0;
        } else {
            ctx->snapshot.transfer_state = (*ctx->transferCancelToken) ? LANDROP_XFER_CANCELLED : LANDROP_XFER_FAILED;
            std::strncpy(ctx->snapshot.error_message, err.c_str(), sizeof(ctx->snapshot.error_message) - 1);
            ctx->snapshot.error_message[sizeof(ctx->snapshot.error_message) - 1] = '\0';
        }
    });

    return LANDROP_OK;
}

int landrop_mobile_webrtc_receive_file_async(
    LandropHandle handle,
    const char* save_dir
) {
    if (!handle) return LANDROP_ERR_INVALID_HANDLE;
    if (!save_dir) return LANDROP_ERR_INVALID_ARG;
    auto* ctx = static_cast<LandropMobileContext*>(handle);

    std::thread oldWorker;
    {
        std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
        if (!ctx->coordinator || ctx->coordinator->getState() != landrop::CoordinatorState::Connected) {
            return LANDROP_ERR_ILLEGAL_STATE;
        }
        auto channel = ctx->coordinator->getChannel();
        if (!channel || !channel->isConnected()) {
            return LANDROP_ERR_NETWORK_DISCONNECTED;
        }

        if (ctx->snapshot.transfer_state == LANDROP_XFER_IN_PROGRESS) {
            return LANDROP_ERR_ILLEGAL_STATE;
        }

        if (ctx->transferWorker.joinable()) {
            oldWorker = std::move(ctx->transferWorker);
        }
    }

    if (oldWorker.joinable()) {
        oldWorker.join();
    }

    std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
    if (!ctx->coordinator || ctx->coordinator->getState() != landrop::CoordinatorState::Connected) {
        return LANDROP_ERR_ILLEGAL_STATE;
    }
    auto channel = ctx->coordinator->getChannel();
    if (!channel || !channel->isConnected()) {
        return LANDROP_ERR_NETWORK_DISCONNECTED;
    }
    if (ctx->snapshot.transfer_state == LANDROP_XFER_IN_PROGRESS) {
        return LANDROP_ERR_ILLEGAL_STATE;
    }

    ctx->transferCancelToken = std::make_shared<std::atomic<bool>>(false);
    std::string saveDirStd = save_dir;
    std::string taskId = "mob-rx-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());

    std::memset(&ctx->snapshot, 0, sizeof(ctx->snapshot));
    ctx->snapshot.transfer_state = LANDROP_XFER_IN_PROGRESS;

    ctx->transferWorker = std::thread([ctx, channel, taskId, saveDirStd]() {
        landrop::TransferOptions opts;
        opts.closeChannelOnCompletion = false;
        opts.autoRenameDuplicates = true;
        opts.headerTimeoutMs = 0; // indefinite wait

        std::string err;
        bool ok = ctx->transferEngine->receiveFileOverChannel(
            taskId,
            saveDirStd,
            channel,
            opts,
            [ctx](const landrop::TransferProgressUpdate& upd) {
                std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
                ctx->snapshot.bytes_transferred = upd.bytesTransferred;
                ctx->snapshot.total_bytes = upd.totalBytes;
                ctx->snapshot.progress_percentage = upd.progressPercentage;
                ctx->snapshot.speed_bytes_per_sec = upd.speedBytesPerSec;
                ctx->snapshot.transfer_state = LANDROP_XFER_IN_PROGRESS;
                if (!upd.fileName.empty()) {
                    std::strncpy(ctx->snapshot.file_name, upd.fileName.c_str(), sizeof(ctx->snapshot.file_name) - 1);
                    ctx->snapshot.file_name[sizeof(ctx->snapshot.file_name) - 1] = '\0';
                }
            },
            ctx->transferCancelToken.get(),
            &err,
            nullptr, // onIncoming
            nullptr  // onFinished
        );

        std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
        if (ok) {
            ctx->snapshot.transfer_state = LANDROP_XFER_COMPLETED;
            ctx->snapshot.progress_percentage = 100.0;
        } else {
            ctx->snapshot.transfer_state = (*ctx->transferCancelToken) ? LANDROP_XFER_CANCELLED : LANDROP_XFER_FAILED;
            std::strncpy(ctx->snapshot.error_message, err.c_str(), sizeof(ctx->snapshot.error_message) - 1);
            ctx->snapshot.error_message[sizeof(ctx->snapshot.error_message) - 1] = '\0';
        }
    });

    return LANDROP_OK;
}

int landrop_mobile_webrtc_get_transfer_snapshot(
    LandropHandle handle,
    LandropTransferSnapshot* out_snapshot
) {
    if (!handle) return LANDROP_ERR_INVALID_HANDLE;
    if (!out_snapshot) return LANDROP_ERR_INVALID_ARG;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
    *out_snapshot = ctx->snapshot;
    return LANDROP_OK;
}

int landrop_mobile_webrtc_cancel_transfer(LandropHandle handle) {
    if (!handle) return LANDROP_ERR_INVALID_HANDLE;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
    if (ctx->transferCancelToken) {
        *ctx->transferCancelToken = true;
    }
    return LANDROP_OK;
}

/* ========================================================================= */
/* 6. Local Subnet Discovery & NFC / AirDrop Emulation (Backward Compatible)  */
/* ========================================================================= */
int landrop_mobile_start_discovery(LandropHandle handle, uint16_t port) {
    if (!handle) return LANDROP_ERR_INVALID_HANDLE;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
    ctx->beacon.setIntervalMs(1000);
    ctx->beacon.start();

    ctx->discoveredPeers.clear();
    ctx->discoveredPeers.push_back(std::make_shared<landrop::LocalPeer>("dev-pc-01", "Desktop-Workstation", "192.168.1.100", port, "Linux", "Workstation"));
    ctx->discoveredPeers.push_back(std::make_shared<landrop::LocalPeer>("dev-ph-02", "iPhone-AirDrop", "192.168.1.102", port, "iOS", "iPhone 15"));

    return LANDROP_OK;
}

void landrop_mobile_stop_discovery(LandropHandle handle) {
    if (!handle) return;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
    ctx->beacon.stop();
}

int landrop_mobile_get_peer_count(LandropHandle handle) {
    if (!handle) return 0;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
    return static_cast<int>(ctx->discoveredPeers.size());
}

int landrop_mobile_get_peer_info(LandropHandle handle, int index, char* out_name, int name_cap, char* out_ip, int ip_cap, uint16_t* out_port) {
    if (!handle) return LANDROP_ERR_INVALID_HANDLE;
    if (index < 0) return LANDROP_ERR_INVALID_ARG;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
    if (static_cast<size_t>(index) >= ctx->discoveredPeers.size()) return LANDROP_ERR_INVALID_ARG;

    const auto& peer = ctx->discoveredPeers[static_cast<size_t>(index)];
    if (out_name && name_cap > 0) {
        std::strncpy(out_name, peer->getDisplayName().c_str(), static_cast<size_t>(name_cap - 1));
        out_name[name_cap - 1] = '\0';
    }
    if (out_ip && ip_cap > 0) {
        std::strncpy(out_ip, peer->getIpAddress().c_str(), static_cast<size_t>(ip_cap - 1));
        out_ip[ip_cap - 1] = '\0';
    }
    if (out_port) {
        *out_port = peer->getPort();
    }
    return LANDROP_OK;
}

int landrop_mobile_send_file(LandropHandle handle, const char* file_path, const char* target_ip, uint16_t target_port) {
    if (!handle) return LANDROP_ERR_INVALID_HANDLE;
    if (!file_path || !target_ip) return LANDROP_ERR_INVALID_ARG;
    landrop::NetworkSession session("mobile-sender", target_ip, target_port);
    if (!session.connectTo(target_ip, target_port)) {
        return LANDROP_ERR_NETWORK_DISCONNECTED;
    }
    return LANDROP_OK;
}

int landrop_mobile_nfc_generate_payload(LandropHandle handle, const char* local_ip, uint16_t port, char* out_buf, int buf_cap) {
    if (!handle) return LANDROP_ERR_INVALID_HANDLE;
    if (!local_ip || !out_buf || buf_cap <= 0) return LANDROP_ERR_INVALID_ARG;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
    std::string payload = ctx->nfcSession.generateNdefPayload(local_ip, port);
    if (static_cast<int>(payload.size() + 1) > buf_cap) {
        if (out_buf && buf_cap > 0) out_buf[0] = '\0';
        return LANDROP_ERR_BUFFER_TOO_SMALL;
    }
    std::strncpy(out_buf, payload.c_str(), static_cast<size_t>(buf_cap - 1));
    out_buf[buf_cap - 1] = '\0';
    return static_cast<int>(payload.size());
}

int landrop_mobile_nfc_process_payload(LandropHandle handle, const char* raw_payload, char* out_paired_name, int name_cap) {
    if (!handle) return LANDROP_ERR_INVALID_HANDLE;
    if (!raw_payload) return LANDROP_ERR_INVALID_ARG;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
    auto dataOpt = ctx->nfcSession.parseNdefPayload(raw_payload);
    if (!dataOpt.has_value()) {
        return LANDROP_ERR_INVALID_ARG;
    }

    if (out_paired_name && name_cap > 0) {
        std::strncpy(out_paired_name, dataOpt->deviceName.c_str(), static_cast<size_t>(name_cap - 1));
        out_paired_name[name_cap - 1] = '\0';
    }
    return LANDROP_OK;
}

int landrop_mobile_airdrop_enable(LandropHandle handle, int enable) {
    if (!handle) return LANDROP_ERR_INVALID_HANDLE;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
    if (enable) {
        return ctx->airdropBridge.startAdvertising() ? LANDROP_OK : LANDROP_ERR_INTERNAL;
    } else {
        ctx->airdropBridge.stopAdvertising();
        return LANDROP_OK;
    }
}

int landrop_mobile_airdrop_get_state(LandropHandle handle) {
    if (!handle) return LANDROP_ERR_INVALID_HANDLE;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mutex);
    return static_cast<int>(ctx->airdropBridge.getState());
}

}
