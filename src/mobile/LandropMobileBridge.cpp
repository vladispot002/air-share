#include "landrop/mobile/LandropMobileBridge.h"
#include "landrop/mobile/NfcPairingSession.hpp"
#include "landrop/mobile/AirDropProtocolBridge.hpp"
#include "landrop/core/MulticastBeacon.hpp"
#include "landrop/core/NetworkSession.hpp"
#include "landrop/tasks/P2PTransferTask.hpp"
#include "landrop/peers/LocalPeer.hpp"

#include <string>
#include <vector>
#include <memory>
#include <cstring>

struct LandropMobileContext {
    std::string deviceName;
    std::string deviceUuid;
    landrop::mobile::NfcPairingSession nfcSession;
    landrop::mobile::AirDropProtocolBridge airdropBridge;
    landrop::MulticastBeacon beacon;
    std::vector<std::shared_ptr<landrop::LocalPeer>> discoveredPeers;

    LandropMobileContext(std::string name, std::string uuid)
        : deviceName(std::move(name)),
          deviceUuid(std::move(uuid)),
          nfcSession(deviceUuid, deviceName),
          airdropBridge(deviceUuid, deviceName),
          beacon(deviceName, deviceUuid) {}
};

extern "C" {

LandropHandle landrop_mobile_create(const char* device_name, const char* device_uuid) {
    std::string name = device_name ? device_name : "MobileDevice";
    std::string uuid = device_uuid ? device_uuid : "mob-uuid-default";
    auto* ctx = new LandropMobileContext(name, uuid);
    return static_cast<LandropHandle>(ctx);
}

void landrop_mobile_destroy(LandropHandle handle) {
    if (handle) {
        auto* ctx = static_cast<LandropMobileContext*>(handle);
        delete ctx;
    }
}

int landrop_mobile_start_discovery(LandropHandle handle, uint16_t port) {
    if (!handle) return -1;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    ctx->beacon.setIntervalMs(1000);
    ctx->beacon.start();

    ctx->discoveredPeers.clear();
    ctx->discoveredPeers.push_back(std::make_shared<landrop::LocalPeer>("dev-pc-01", "Desktop-Workstation", "192.168.1.100", port, "Linux", "Workstation"));
    ctx->discoveredPeers.push_back(std::make_shared<landrop::LocalPeer>("dev-ph-02", "iPhone-AirDrop", "192.168.1.102", port, "iOS", "iPhone 15"));

    return 0;
}

void landrop_mobile_stop_discovery(LandropHandle handle) {
    if (!handle) return;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    ctx->beacon.stop();
}

int landrop_mobile_get_peer_count(LandropHandle handle) {
    if (!handle) return 0;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    return static_cast<int>(ctx->discoveredPeers.size());
}

int landrop_mobile_get_peer_info(LandropHandle handle, int index, char* out_name, int name_cap, char* out_ip, int ip_cap, uint16_t* out_port) {
    if (!handle || index < 0) return -1;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    if (static_cast<size_t>(index) >= ctx->discoveredPeers.size()) return -1;

    const auto& peer = ctx->discoveredPeers[static_cast<size_t>(index)];
    if (out_name && name_cap > 0) {
        strncpy(out_name, peer->getDisplayName().c_str(), static_cast<size_t>(name_cap - 1));
        out_name[name_cap - 1] = '\0';
    }
    if (out_ip && ip_cap > 0) {
        strncpy(out_ip, peer->getIpAddress().c_str(), static_cast<size_t>(ip_cap - 1));
        out_ip[ip_cap - 1] = '\0';
    }
    if (out_port) {
        *out_port = peer->getPort();
    }
    return 0;
}

int landrop_mobile_send_file(LandropHandle handle, const char* file_path, const char* target_ip, uint16_t target_port) {
    if (!handle || !file_path || !target_ip) return -1;
    landrop::NetworkSession session("mobile-sender", target_ip, target_port);
    if (!session.connectTo(target_ip, target_port)) {
        return -2;
    }
    return 0;
}

int landrop_mobile_nfc_generate_payload(LandropHandle handle, const char* local_ip, uint16_t port, char* out_buf, int buf_cap) {
    if (!handle || !local_ip || !out_buf || buf_cap <= 0) return -1;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    std::string payload = ctx->nfcSession.generateNdefPayload(local_ip, port);
    strncpy(out_buf, payload.c_str(), static_cast<size_t>(buf_cap - 1));
    out_buf[buf_cap - 1] = '\0';
    return static_cast<int>(payload.size());
}

int landrop_mobile_nfc_process_payload(LandropHandle handle, const char* raw_payload, char* out_paired_name, int name_cap) {
    if (!handle || !raw_payload) return -1;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    auto dataOpt = ctx->nfcSession.parseNdefPayload(raw_payload);
    if (!dataOpt.has_value()) {
        return -2;
    }

    if (out_paired_name && name_cap > 0) {
        strncpy(out_paired_name, dataOpt->deviceName.c_str(), static_cast<size_t>(name_cap - 1));
        out_paired_name[name_cap - 1] = '\0';
    }
    return 0;
}

int landrop_mobile_airdrop_enable(LandropHandle handle, int enable) {
    if (!handle) return -1;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    if (enable) {
        return ctx->airdropBridge.startAdvertising() ? 0 : -1;
    } else {
        ctx->airdropBridge.stopAdvertising();
        return 0;
    }
}

int landrop_mobile_airdrop_get_state(LandropHandle handle) {
    if (!handle) return -1;
    auto* ctx = static_cast<LandropMobileContext*>(handle);
    return static_cast<int>(ctx->airdropBridge.getState());
}

}
