#include "landrop/peers/CloudPeer.hpp"
#include <utility>

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

}
