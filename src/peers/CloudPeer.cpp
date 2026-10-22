#include "landrop/peers/CloudPeer.hpp"
#include <utility>
#include <nlohmann/json.hpp>

namespace landrop {

CloudPeer::CloudPeer(
    std::string id,
    std::string displayName,
    std::string endpointUrl,
    uint16_t port,
    std::string cloudProvider,
    std::string accessToken,
    uint64_t storageQuotaBytes
) : NetworkPeer(std::move(id), std::move(displayName), endpointUrl, port),
    m_cloudProvider(std::move(cloudProvider)),
    m_endpointUrl(std::move(endpointUrl)),
    m_accessToken(std::move(accessToken)),
    m_storageQuotaBytes(storageQuotaBytes) {}

bool CloudPeer::initialize() {
    m_isOnline = !m_accessToken.empty();
    updateLastSeen();
    return m_isOnline;
}

std::string CloudPeer::getPeerType() const {
    return "CloudPeer";
}

std::string CloudPeer::getEndpointInfo() const {
    return m_endpointUrl + ":" + std::to_string(m_port);
}

std::string CloudPeer::getSummary() const {
    return NetworkPeer::getSummary() + " | Provider: " + m_cloudProvider + " | Quota: " +
           std::to_string(m_usedStorageBytes / (1024 * 1024)) + "/" +
           std::to_string(m_storageQuotaBytes / (1024 * 1024)) + " MB";
}

bool CloudPeer::ping() {
    updateLastSeen();
    return m_isOnline;
}

bool CloudPeer::hasAvailableQuota(uint64_t requiredBytes) const {
    return (m_usedStorageBytes + requiredBytes) <= m_storageQuotaBytes;
}

uint64_t CloudPeer::getAvailableStorageBytes() const {
    if (m_usedStorageBytes >= m_storageQuotaBytes) {
        return 0;
    }
    return m_storageQuotaBytes - m_usedStorageBytes;
}

void CloudPeer::allocateStorage(uint64_t bytes) {
    m_usedStorageBytes += bytes;
}

void CloudPeer::releaseStorage(uint64_t bytes) {
    if (bytes >= m_usedStorageBytes) {
        m_usedStorageBytes = 0;
    } else {
        m_usedStorageBytes -= bytes;
    }
}

void CloudPeer::refreshAccessToken(std::string newToken) {
    m_accessToken = std::move(newToken);
    m_isOnline = !m_accessToken.empty();
}

std::string CloudPeer::buildQuotaRequestPayload() const {
    nlohmann::json j;
    j["peer_id"] = m_id;
    j["cloud_provider"] = m_cloudProvider;
    j["access_token"] = m_accessToken;
    j["request_type"] = "GET_QUOTA";
    return j.dump();
}

bool CloudPeer::parseQuotaResponse(const std::string& jsonResponse) {
    try {
        auto j = nlohmann::json::parse(jsonResponse);
        if (j.contains("storage_quota_bytes") && j["storage_quota_bytes"].is_number()) {
            m_storageQuotaBytes = j["storage_quota_bytes"].get<uint64_t>();
        }
        if (j.contains("used_storage_bytes") && j["used_storage_bytes"].is_number()) {
            m_usedStorageBytes = j["used_storage_bytes"].get<uint64_t>();
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}
