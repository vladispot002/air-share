#pragma once

#include "NetworkPeer.hpp"

namespace landrop {

class CloudPeer : public NetworkPeer {
public:
    CloudPeer(
        std::string id,
        std::string displayName,
        std::string endpointUrl,
        uint16_t port = 443,
        std::string cloudProvider = "DropCloud",
        std::string accessToken = "demo_token",
        uint64_t storageQuotaBytes = 10ULL * 1024 * 1024 * 1024
    );

    ~CloudPeer() override = default;

    bool initialize() override;
    std::string getPeerType() const override;
    std::string getEndpointInfo() const override;
    std::string getSummary() const override;
    bool ping() override;

    bool hasAvailableQuota(uint64_t requiredBytes) const;
    uint64_t getAvailableStorageBytes() const;
    void allocateStorage(uint64_t bytes);
    void releaseStorage(uint64_t bytes);

    void refreshAccessToken(std::string newToken);
    const std::string& getAccessToken() const { return m_accessToken; }

    const std::string& getCloudProvider() const { return m_cloudProvider; }
    const std::string& getEndpointUrl() const { return m_endpointUrl; }

    uint64_t getStorageQuotaBytes() const { return m_storageQuotaBytes; }
    uint64_t getUsedStorageBytes() const { return m_usedStorageBytes; }

    bool isAutoSyncEnabled() const { return m_autoSyncEnabled; }
    void setAutoSyncEnabled(bool enabled) { m_autoSyncEnabled = enabled; }

private:
    std::string m_cloudProvider;
    std::string m_endpointUrl;
    std::string m_accessToken;
    uint64_t m_storageQuotaBytes{0};
    uint64_t m_usedStorageBytes{0};
    bool m_autoSyncEnabled{true};
};

}
