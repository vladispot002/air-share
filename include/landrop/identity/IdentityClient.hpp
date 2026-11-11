#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "landrop/core/HttpClient.hpp"

namespace landrop {

struct UserProfile {
    std::string userId;
    std::string landropId;
    std::string displayName;
    std::string avatarUrl;
    std::string email;
    bool emailVerified{false};
    std::string status{"active"};
};

struct DeviceInfo {
    std::string deviceId;
    std::string userId;
    std::string deviceName;
    std::string deviceType{"desktop"};
    std::string platform{"linux"};
    std::string status{"active"};
    std::string presence{"online"};
};

struct ContactInfo {
    std::string userId;
    std::string targetUserId;
    std::string landropId;
    std::string displayName;
    std::string avatarUrl;
    std::string state{"NONE"}; // "NONE", "REQUEST_SENT", "REQUEST_RECEIVED", "ACCEPTED", "BLOCKED"
    bool isFavorite{false};
};

struct RecentUserInfo {
    std::string targetUserId;
    std::string landropId;
    std::string displayName;
    std::string avatarUrl;
    std::string lastInteraction;
    std::string lastTransferStatus;
};

struct DevicePairingInfo {
    std::string sessionId;
    std::string userId;
    std::string secret;
    std::string expiresAt;
};

/**
 * @brief IdentityClient connects to the LAN-Drop identity backend.
 * Provides user accounts, multi-device management, contacts, and pairing.
 * Adheres strictly to the Transfer-First, Account-Optional principle:
 * guest mode is the default and never blocks transfers.
 */
class IdentityClient {
public:
    explicit IdentityClient(std::shared_ptr<HttpClient> httpClient = nullptr, const std::string& apiBaseUrl = "http://127.0.0.1:8848");
    virtual ~IdentityClient() = default;

    bool isGuest() const;
    std::string getGuestId() const;
    std::string getAuthToken() const;
    UserProfile getCurrentUser() const;
    DeviceInfo getCurrentDevice() const;

    void setBaseUrl(const std::string& baseUrl);
    void setAuthToken(const std::string& token);
    void setCurrentUser(const UserProfile& user);

    // Auth & Profile
    bool authenticateGuest();
    bool authenticateWithProvider(
        const std::string& provider,
        const std::string& providerUserId,
        const std::string& email,
        const std::string& displayName,
        const std::string& deviceName,
        const std::string& platform,
        std::string* outError = nullptr
    );
    void logout();

    bool fetchProfile(UserProfile& outProfile, std::string* outError = nullptr);
    bool lookupUser(const std::string& landropId, UserProfile& outUser, std::string* outError = nullptr);
    bool searchUsers(const std::string& query, std::vector<UserProfile>& outUsers, std::string* outError = nullptr);

    // Devices
    bool listDevices(std::vector<DeviceInfo>& outDevices, std::string* outError = nullptr);
    bool registerCurrentDevice(const std::string& name, const std::string& platform, std::string* outError = nullptr);
    bool revokeDevice(const std::string& deviceId, std::string* outError = nullptr);
    bool createPairingSession(DevicePairingInfo& outSession, std::string* outError = nullptr);
    bool confirmPairing(
        const std::string& sessionId,
        const std::string& secret,
        const std::string& deviceName,
        const std::string& platform,
        std::string* outError = nullptr
    );

    // Contacts & Recent Interactions
    bool listContacts(std::vector<ContactInfo>& outContacts, std::string* outError = nullptr);
    bool sendContactRequest(const std::string& targetLandropId, ContactInfo& outContact, std::string* outError = nullptr);
    bool acceptContactRequest(const std::string& targetUserId, std::string* outError = nullptr);
    bool declineContactRequest(const std::string& targetUserId, std::string* outError = nullptr);
    bool blockUser(const std::string& targetUserId, std::string* outError = nullptr);
    bool setFavorite(const std::string& targetUserId, bool isFavorite, std::string* outError = nullptr);
    bool getRecentUsers(std::vector<RecentUserInfo>& outRecents, std::string* outError = nullptr);

private:
    std::shared_ptr<HttpClient> m_httpClient;
    std::string m_apiBaseUrl;
    bool m_isGuest{true};
    std::string m_guestId;
    std::string m_token;
    UserProfile m_user;
    DeviceInfo m_device;
    mutable std::mutex m_mutex;
};

} // namespace landrop
