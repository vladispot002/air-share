#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "landrop/identity/IdentityClient.hpp"

namespace landrop {

/**
 * @brief AccountPresenter coordinates presentation logic for the Account / Contacts view.
 * Ensures strict guest-first default, allowing anonymous file transfers without login,
 * while offering full identity features (contacts, multi-device management, pairing) when authenticated.
 */
class AccountPresenter {
public:
    explicit AccountPresenter(std::shared_ptr<IdentityClient> client = nullptr);
    virtual ~AccountPresenter() = default;

    bool isGuest() const;
    std::string getGuestId() const;
    UserProfile getCurrentUser() const;
    std::vector<DeviceInfo> getDevices() const;
    std::vector<ContactInfo> getContacts() const;
    std::vector<RecentUserInfo> getRecentUsers() const;
    std::vector<TransferHistoryItem> getHistory() const;
    std::shared_ptr<IdentityClient> getClient() const { return m_client; }

    void setContacts(const std::vector<ContactInfo>& contacts);
    void setDevices(const std::vector<DeviceInfo>& devices);
    void setHistory(const std::vector<TransferHistoryItem>& history);

    bool loginWithEmail(const std::string& email, const std::string& displayName, std::string* outError = nullptr);
    bool loginWithProvider(const std::string& provider, const std::string& providerUserId, const std::string& email, const std::string& displayName, std::string* outError = nullptr);
    bool requestMagicLink(const std::string& email, std::string* outToken = nullptr, std::string* outError = nullptr);
    bool verifyMagicLink(const std::string& token, std::string* outError = nullptr);
    void logout();

    bool refreshContacts(std::string* outError = nullptr);
    bool addContact(const std::string& targetLandropId, std::string* outError = nullptr);
    bool acceptContact(const std::string& targetUserId, std::string* outError = nullptr);
    bool declineContact(const std::string& targetUserId, std::string* outError = nullptr);
    bool toggleFavorite(const std::string& targetUserId, bool isFavorite, std::string* outError = nullptr);

    bool refreshDevices(std::string* outError = nullptr);
    bool revokeDevice(const std::string& deviceId, std::string* outError = nullptr);
    bool generatePairingSession(DevicePairingInfo& outSession, std::string* outError = nullptr);

    bool refreshHistory(int limit = 20, std::string* outError = nullptr);
    bool recordTransfer(const TransferHistoryItem& item, std::string* outError = nullptr);
    bool clearHistory(std::string* outError = nullptr);

    void setOnStateChanged(std::function<void()> callback);

private:
    void notifyStateChanged();

    std::shared_ptr<IdentityClient> m_client;
    std::vector<DeviceInfo> m_cachedDevices;
    std::vector<ContactInfo> m_cachedContacts;
    std::vector<RecentUserInfo> m_cachedRecents;
    std::vector<TransferHistoryItem> m_cachedHistory;
    std::function<void()> m_onStateChanged;
};

} // namespace landrop
