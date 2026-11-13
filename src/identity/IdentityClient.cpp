#include "landrop/identity/IdentityClient.hpp"
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;

namespace landrop {

static std::string generateRandomHex(size_t length) {
    static const char hexChars[] = "0123456789abcdef";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::string s;
    s.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        s.push_back(hexChars[dis(gen)]);
    }
    return s;
}

IdentityClient::IdentityClient(std::shared_ptr<HttpClient> httpClient, const std::string& apiBaseUrl)
    : m_httpClient(httpClient ? httpClient : std::make_shared<HttpClient>())
    , m_apiBaseUrl(apiBaseUrl)
{
    authenticateGuest();
}

bool IdentityClient::isGuest() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_isGuest;
}

std::string IdentityClient::getGuestId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_guestId;
}

std::string IdentityClient::getAuthToken() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_token;
}

UserProfile IdentityClient::getCurrentUser() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_user;
}

DeviceInfo IdentityClient::getCurrentDevice() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_device;
}

void IdentityClient::setBaseUrl(const std::string& baseUrl) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_apiBaseUrl = baseUrl;
    while (!m_apiBaseUrl.empty() && m_apiBaseUrl.back() == '/') {
        m_apiBaseUrl.pop_back();
    }
}

void IdentityClient::setAuthToken(const std::string& token) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_token = token;
    if (!token.empty()) {
        m_isGuest = false;
    }
}

void IdentityClient::setCurrentUser(const UserProfile& user) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_user = user;
    m_isGuest = user.userId.empty();
}

void IdentityClient::setSessionStorePath(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessionStorePath = path;
}

std::string IdentityClient::getSessionStorePath() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sessionStorePath;
}

bool IdentityClient::saveSession(std::string* outError) {
    std::string path;
    json j;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        path = m_sessionStorePath;
        if (path.empty() || m_isGuest || m_token.empty()) {
            return false;
        }
        j["token"] = m_token;
        j["user"] = {
            {"user_id", m_user.userId},
            {"landrop_id", m_user.landropId},
            {"display_name", m_user.displayName},
            {"email", m_user.email},
            {"avatar_url", m_user.avatarUrl},
            {"status", m_user.status}
        };
        j["device"] = {
            {"device_id", m_device.deviceId},
            {"user_id", m_device.userId},
            {"device_name", m_device.deviceName},
            {"device_type", m_device.deviceType},
            {"platform", m_device.platform},
            {"status", m_device.status},
            {"presence", m_device.presence}
        };
    }

    try {
        std::error_code ec;
        auto parentDir = std::filesystem::path(path).parent_path();
        if (!parentDir.empty()) {
            std::filesystem::create_directories(parentDir, ec);
        }
        std::ofstream f(path, std::ios::trunc);
        if (!f.is_open()) {
            if (outError) *outError = "Failed to open session file for writing";
            return false;
        }
        f << j.dump(2);
        f.close();
#if !defined(_WIN32)
        std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace, ec);
#endif
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }
}

bool IdentityClient::loadSession(std::string* outError) {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        path = m_sessionStorePath;
    }
    if (path.empty()) return false;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return false;

    try {
        std::ifstream f(path);
        if (!f.is_open()) return false;
        json j = json::parse(f);
        std::string token = j.value("token", "");
        if (token.empty()) return false;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_token = token;
            m_isGuest = false;

            if (j.contains("user") && j["user"].is_object()) {
                auto ju = j["user"];
                m_user.userId = ju.value("user_id", "");
                m_user.landropId = ju.value("landrop_id", "");
                m_user.displayName = ju.value("display_name", "");
                m_user.email = ju.value("email", "");
                m_user.avatarUrl = ju.value("avatar_url", "");
                m_user.status = ju.value("status", "active");
            }
            if (j.contains("device") && j["device"].is_object()) {
                auto jd = j["device"];
                m_device.deviceId = jd.value("device_id", "");
                m_device.userId = jd.value("user_id", "");
                m_device.deviceName = jd.value("device_name", "");
                m_device.deviceType = jd.value("device_type", "desktop");
                m_device.platform = jd.value("platform", "linux");
                m_device.status = jd.value("status", "active");
                m_device.presence = jd.value("presence", "online");
            }
        }

        // Validate session against backend
        UserProfile profile;
        std::string verifyErr;
        if (!fetchProfile(profile, &verifyErr)) {
            if (verifyErr.find("401") != std::string::npos || verifyErr.find("Unauthorized") != std::string::npos) {
                clearSession();
                authenticateGuest();
                if (outError) *outError = "Session expired or invalid";
                return false;
            }
        }
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }
}

void IdentityClient::clearSession() {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        path = m_sessionStorePath;
    }
    if (!path.empty()) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
}

bool IdentityClient::authenticateGuest() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_isGuest = true;
    m_token.clear();
    m_guestId = "guest-" + generateRandomHex(6);
    m_user = UserProfile{};
    m_device = DeviceInfo{};
    return true;
}

void IdentityClient::logout() {
    clearSession();
    authenticateGuest();
}

bool IdentityClient::authenticateWithProvider(
    const std::string& provider,
    const std::string& providerUserId,
    const std::string& email,
    const std::string& displayName,
    const std::string& deviceName,
    const std::string& platform,
    std::string* outError
) {
    json payload;
    payload["provider"] = provider;
    payload["provider_user_id"] = providerUserId;
    payload["email"] = email;
    payload["display_name"] = displayName;
    payload["device_name"] = deviceName.empty() ? "Desktop Client" : deviceName;
    payload["platform"] = platform.empty() ? "linux" : platform;

    std::string url;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        url = m_apiBaseUrl + "/api/v1/auth/login";
    }

    HttpResponse res = m_httpClient->post(url, payload.dump(), "", 5);
    if (!res.isOk()) {
        if (outError) *outError = res.errorMessage.empty() ? ("HTTP " + std::to_string(res.statusCode)) : res.errorMessage;
        return false;
    }

    try {
        json j = json::parse(res.body);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_token = j.value("token", "");
            m_isGuest = false;

            if (j.contains("user") && j["user"].is_object()) {
                auto ju = j["user"];
                m_user.userId = ju.value("user_id", ju.value("id", ""));
                m_user.landropId = ju.value("landrop_id", "");
                m_user.displayName = ju.value("display_name", "");
                m_user.email = ju.value("email", "");
                m_user.avatarUrl = ju.value("avatar_url", "");
                m_user.status = ju.value("status", "active");
            }

            if (j.contains("device") && j["device"].is_object()) {
                auto jd = j["device"];
                m_device.deviceId = jd.value("device_id", jd.value("id", ""));
                m_device.userId = jd.value("user_id", "");
                m_device.deviceName = jd.value("device_name", jd.value("name", ""));
                m_device.deviceType = jd.value("device_type", "desktop");
                m_device.platform = jd.value("platform", "linux");
                m_device.status = jd.value("status", "active");
                m_device.presence = jd.value("presence", "online");
            }
        }
        saveSession();
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = std::string("JSON parse error: ") + e.what();
        return false;
    }
}

bool IdentityClient::registerWithPassword(
    const std::string& email,
    const std::string& password,
    const std::string& displayName,
    const std::string& deviceName,
    const std::string& platform,
    std::string* outError
) {
    json payload;
    payload["email"] = email;
    payload["password"] = password;
    payload["confirm_password"] = password;
    payload["display_name"] = displayName.empty() ? email.substr(0, email.find('@')) : displayName;
    payload["device_name"] = deviceName.empty() ? "LAN-Drop Desktop" : deviceName;
    payload["platform"] = platform.empty() ? "linux" : platform;
    payload["app_version"] = "2.0.0";

    std::string url;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        url = m_apiBaseUrl + "/api/v1/auth/register";
    }

    HttpResponse res = m_httpClient->post(url, payload.dump(), "", 5);
    if (!res.isOk()) {
        try {
            json errJ = json::parse(res.body);
            if (errJ.contains("error")) {
                if (outError) *outError = errJ["error"].get<std::string>();
                return false;
            }
        } catch (...) {}
        if (outError) *outError = res.errorMessage.empty() ? ("HTTP " + std::to_string(res.statusCode)) : res.errorMessage;
        return false;
    }

    try {
        json j = json::parse(res.body);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_token = j.value("token", "");
            m_isGuest = false;

            if (j.contains("user") && j["user"].is_object()) {
                auto ju = j["user"];
                m_user.userId = ju.value("user_id", ju.value("id", ""));
                m_user.landropId = ju.value("landrop_id", "");
                m_user.displayName = ju.value("display_name", "");
                m_user.email = ju.value("email", "");
                m_user.avatarUrl = ju.value("avatar_url", "");
                m_user.status = ju.value("status", "active");
            }

            if (j.contains("device") && j["device"].is_object()) {
                auto jd = j["device"];
                m_device.deviceId = jd.value("device_id", jd.value("id", ""));
                m_device.userId = jd.value("user_id", "");
                m_device.deviceName = jd.value("device_name", jd.value("name", ""));
                m_device.deviceType = jd.value("device_type", "desktop");
                m_device.platform = jd.value("platform", "linux");
                m_device.status = jd.value("status", "active");
                m_device.presence = jd.value("presence", "online");
            }
        }
        saveSession();
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = std::string("JSON parse error: ") + e.what();
        return false;
    }
}

bool IdentityClient::loginWithPassword(
    const std::string& email,
    const std::string& password,
    const std::string& deviceName,
    const std::string& platform,
    std::string* outError
) {
    json payload;
    payload["email"] = email;
    payload["password"] = password;
    payload["device_name"] = deviceName.empty() ? "LAN-Drop Desktop" : deviceName;
    payload["platform"] = platform.empty() ? "linux" : platform;
    payload["app_version"] = "2.0.0";

    std::string url;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        url = m_apiBaseUrl + "/api/v1/auth/login";
    }

    HttpResponse res = m_httpClient->post(url, payload.dump(), "", 5);
    if (!res.isOk()) {
        try {
            json errJ = json::parse(res.body);
            if (errJ.contains("error")) {
                if (outError) *outError = errJ["error"].get<std::string>();
                return false;
            }
        } catch (...) {}
        if (outError) *outError = res.errorMessage.empty() ? ("HTTP " + std::to_string(res.statusCode)) : res.errorMessage;
        return false;
    }

    try {
        json j = json::parse(res.body);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_token = j.value("token", "");
            m_isGuest = false;

            if (j.contains("user") && j["user"].is_object()) {
                auto ju = j["user"];
                m_user.userId = ju.value("user_id", ju.value("id", ""));
                m_user.landropId = ju.value("landrop_id", "");
                m_user.displayName = ju.value("display_name", "");
                m_user.email = ju.value("email", "");
                m_user.avatarUrl = ju.value("avatar_url", "");
                m_user.status = ju.value("status", "active");
            }

            if (j.contains("device") && j["device"].is_object()) {
                auto jd = j["device"];
                m_device.deviceId = jd.value("device_id", jd.value("id", ""));
                m_device.userId = jd.value("user_id", "");
                m_device.deviceName = jd.value("device_name", jd.value("name", ""));
                m_device.deviceType = jd.value("device_type", "desktop");
                m_device.platform = jd.value("platform", "linux");
                m_device.status = jd.value("status", "active");
                m_device.presence = jd.value("presence", "online");
            }
        }
        saveSession();
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = std::string("JSON parse error: ") + e.what();
        return false;
    }
}

bool IdentityClient::requestMagicLink(const std::string& email, std::string* outToken, std::string* outError) {
    json payload;
    payload["email"] = email;

    std::string url;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        url = m_apiBaseUrl + "/api/v1/auth/magic-link/request";
    }

    HttpResponse res = m_httpClient->post(url, payload.dump(), "", 5);
    if (!res.isOk()) {
        if (outError) *outError = res.errorMessage.empty() ? ("HTTP " + std::to_string(res.statusCode)) : res.errorMessage;
        return false;
    }

    try {
        json j = json::parse(res.body);
        if (outToken) {
            *outToken = j.value("token", "");
        }
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = std::string("JSON parse error: ") + e.what();
        return false;
    }
}

bool IdentityClient::verifyMagicLink(const std::string& token, const std::string& deviceName, const std::string& platform, std::string* outError) {
    json payload;
    payload["token"] = token;
    payload["device_name"] = deviceName.empty() ? "LAN-Drop Desktop" : deviceName;
    payload["platform"] = platform.empty() ? "linux" : platform;

    std::string url;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        url = m_apiBaseUrl + "/api/v1/auth/magic-link/verify";
    }

    HttpResponse res = m_httpClient->post(url, payload.dump(), "", 5);
    if (!res.isOk()) {
        if (outError) *outError = res.errorMessage.empty() ? ("HTTP " + std::to_string(res.statusCode)) : res.errorMessage;
        return false;
    }

    try {
        json j = json::parse(res.body);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_token = j.value("token", "");
            m_isGuest = false;

            if (j.contains("user") && j["user"].is_object()) {
                auto ju = j["user"];
                m_user.userId = ju.value("user_id", ju.value("id", ""));
                m_user.landropId = ju.value("landrop_id", "");
                m_user.displayName = ju.value("display_name", "");
                m_user.email = ju.value("email", "");
                m_user.avatarUrl = ju.value("avatar_url", "");
                m_user.status = ju.value("status", "active");
            }

            if (j.contains("device") && j["device"].is_object()) {
                auto jd = j["device"];
                m_device.deviceId = jd.value("device_id", jd.value("id", ""));
                m_device.userId = jd.value("user_id", "");
                m_device.deviceName = jd.value("device_name", jd.value("name", ""));
                m_device.deviceType = jd.value("device_type", "desktop");
                m_device.platform = jd.value("platform", "linux");
                m_device.status = jd.value("status", "active");
                m_device.presence = jd.value("presence", "online");
            }
        }
        saveSession();
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = std::string("JSON parse error: ") + e.what();
        return false;
    }
}

bool IdentityClient::fetchProfile(UserProfile& outProfile, std::string* outError) {
    std::string url, token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_isGuest || m_token.empty()) {
            if (outError) *outError = "Not authenticated";
            return false;
        }
        url = m_apiBaseUrl + "/api/v1/users/me";
        token = m_token;
    }

    HttpRequest req;
    req.method = "GET";
    req.url = url;
    req.headers["Authorization"] = "Bearer " + token;

    HttpResponse res = m_httpClient->send(req);
    if (!res.isOk()) {
        if (outError) *outError = "Failed to fetch profile: " + std::to_string(res.statusCode);
        return false;
    }

    try {
        json j = json::parse(res.body);
        outProfile.userId = j.value("user_id", "");
        outProfile.landropId = j.value("landrop_id", "");
        outProfile.displayName = j.value("display_name", "");
        outProfile.email = j.value("email", "");
        outProfile.avatarUrl = j.value("avatar_url", "");
        outProfile.status = j.value("status", "active");

        std::lock_guard<std::mutex> lock(m_mutex);
        m_user = outProfile;
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }
}

bool IdentityClient::lookupUser(const std::string& landropId, UserProfile& outUser, std::string* outError) {
    std::string norm = landropId;
    if (!norm.empty() && norm[0] != '@') {
        norm = "@" + norm;
    }

    std::string url;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        url = m_apiBaseUrl + "/api/v1/users/lookup?id=" + norm;
    }

    HttpResponse res = m_httpClient->get(url, 5);
    if (!res.isOk()) {
        if (outError) *outError = "User not found";
        return false;
    }

    try {
        json j = json::parse(res.body);
        outUser.userId = j.value("user_id", "");
        outUser.landropId = j.value("landrop_id", "");
        outUser.displayName = j.value("display_name", "");
        outUser.avatarUrl = j.value("avatar_url", "");
        outUser.status = j.value("status", "active");
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }
}

bool IdentityClient::searchUsers(const std::string& query, std::vector<UserProfile>& outUsers, std::string* outError) {
    std::string url;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        url = m_apiBaseUrl + "/api/v1/users/search?q=" + query;
    }

    HttpResponse res = m_httpClient->get(url, 5);
    if (!res.isOk()) {
        if (outError) *outError = "Search failed";
        return false;
    }

    try {
        json arr = json::parse(res.body);
        outUsers.clear();
        for (const auto& j : arr) {
            UserProfile u;
            u.userId = j.value("user_id", "");
            u.landropId = j.value("landrop_id", "");
            u.displayName = j.value("display_name", "");
            u.avatarUrl = j.value("avatar_url", "");
            outUsers.push_back(u);
        }
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }
}

bool IdentityClient::listDevices(std::vector<DeviceInfo>& outDevices, std::string* outError) {
    std::string url, token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_isGuest || m_token.empty()) {
            if (outError) *outError = "Not authenticated";
            return false;
        }
        url = m_apiBaseUrl + "/api/v1/devices";
        token = m_token;
    }

    HttpRequest req;
    req.method = "GET";
    req.url = url;
    req.headers["Authorization"] = "Bearer " + token;

    HttpResponse res = m_httpClient->send(req);
    if (!res.isOk()) {
        if (outError) *outError = "Failed to list devices";
        return false;
    }

    try {
        json parsed = json::parse(res.body);
        json arr = parsed.is_array() ? parsed : (parsed.contains("devices") ? parsed["devices"] : json::array());
        outDevices.clear();
        for (const auto& j : arr) {
            DeviceInfo d;
            d.deviceId = j.value("device_id", "");
            d.userId = j.value("user_id", "");
            d.deviceName = j.value("device_name", "");
            d.deviceType = j.value("device_type", "desktop");
            d.platform = j.value("platform", "linux");
            d.status = j.value("status", "active");
            d.presence = j.value("presence", "online");
            outDevices.push_back(d);
        }
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }
}

bool IdentityClient::registerCurrentDevice(const std::string& name, const std::string& platform, std::string* outError) {
    std::string url, token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_token.empty()) {
            if (outError) *outError = "Not authenticated";
            return false;
        }
        url = m_apiBaseUrl + "/api/v1/devices";
        token = m_token;
    }

    json payload;
    payload["device_name"] = name;
    payload["device_type"] = "desktop";
    payload["platform"] = platform;

    HttpResponse res = m_httpClient->post(url, payload.dump(), token, 5);
    if (!res.isOk()) {
        if (outError) *outError = "Failed to register device";
        return false;
    }

    try {
        json j = json::parse(res.body);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_device.deviceId = j.value("device_id", "");
        m_device.deviceName = j.value("device_name", "");
        m_device.platform = j.value("platform", platform);
        m_device.status = "active";
        m_device.presence = "online";
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }
}

bool IdentityClient::revokeDevice(const std::string& deviceId, std::string* outError) {
    std::string url, token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_token.empty()) {
            if (outError) *outError = "Not authenticated";
            return false;
        }
        url = m_apiBaseUrl + "/api/v1/devices/revoke";
        token = m_token;
    }

    json payload;
    payload["device_id"] = deviceId;
    HttpResponse res = m_httpClient->post(url, payload.dump(), token, 5);
    if (!res.isOk()) {
        if (outError) *outError = "Failed to revoke device";
        return false;
    }
    return true;
}

bool IdentityClient::createPairingSession(DevicePairingInfo& outSession, std::string* outError) {
    std::string url, token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_token.empty()) {
            if (outError) *outError = "Not authenticated";
            return false;
        }
        url = m_apiBaseUrl + "/api/v1/devices/pairing";
        token = m_token;
    }

    HttpResponse res = m_httpClient->post(url, "{}", token, 5);
    if (!res.isOk()) {
        if (outError) *outError = "Failed to create pairing session";
        return false;
    }

    try {
        json j = json::parse(res.body);
        outSession.sessionId = j.value("session_id", "");
        outSession.userId = j.value("user_id", "");
        outSession.secret = j.value("secret", "");
        outSession.expiresAt = j.value("expires_at", "");
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }
}

bool IdentityClient::confirmPairing(
    const std::string& sessionId,
    const std::string& secret,
    const std::string& deviceName,
    const std::string& platform,
    std::string* outError
) {
    std::string url;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        url = m_apiBaseUrl + "/api/v1/devices/pairing/confirm";
    }

    json payload;
    payload["session_id"] = sessionId;
    payload["secret"] = secret;
    payload["device_name"] = deviceName;
    payload["device_type"] = "desktop";
    payload["platform"] = platform;

    HttpResponse res = m_httpClient->post(url, payload.dump(), "", 5);
    if (!res.isOk()) {
        if (outError) *outError = "Pairing confirmation failed";
        return false;
    }

    try {
        json j = json::parse(res.body);
        std::lock_guard<std::mutex> lock(m_mutex);
        if (j.contains("auth") && j["auth"].is_object()) {
            m_token = j["auth"].value("token", "");
            m_isGuest = false;
            if (j["auth"].contains("user")) {
                auto ju = j["auth"]["user"];
                m_user.userId = ju.value("user_id", "");
                m_user.landropId = ju.value("landrop_id", "");
                m_user.displayName = ju.value("display_name", "");
            }
        }
        if (j.contains("device") && j["device"].is_object()) {
            auto jd = j["device"];
            m_device.deviceId = jd.value("device_id", "");
            m_device.deviceName = jd.value("device_name", "");
            m_device.platform = jd.value("platform", platform);
        }
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }
}

bool IdentityClient::listContacts(std::vector<ContactInfo>& outContacts, std::string* outError) {
    std::string url, token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_token.empty()) {
            if (outError) *outError = "Not authenticated";
            return false;
        }
        url = m_apiBaseUrl + "/api/v1/contacts";
        token = m_token;
    }

    HttpRequest req;
    req.method = "GET";
    req.url = url;
    req.headers["Authorization"] = "Bearer " + token;

    HttpResponse res = m_httpClient->send(req);
    if (!res.isOk()) {
        if (outError) *outError = "Failed to list contacts";
        return false;
    }

    try {
        json parsed = json::parse(res.body);
        json arr = parsed.is_array() ? parsed : (parsed.contains("contacts") ? parsed["contacts"] : json::array());
        outContacts.clear();
        for (const auto& j : arr) {
            ContactInfo c;
            c.userId = j.value("user_id", "");
            c.targetUserId = j.value("target_user_id", "");
            c.landropId = j.value("landrop_id", "");
            c.displayName = j.value("display_name", "");
            c.avatarUrl = j.value("avatar_url", "");
            c.state = j.value("state", "NONE");
            c.isFavorite = j.value("is_favorite", false);
            outContacts.push_back(c);
        }
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }
}

bool IdentityClient::sendContactRequest(const std::string& targetLandropId, ContactInfo& outContact, std::string* outError) {
    std::string url, token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_token.empty()) {
            if (outError) *outError = "Not authenticated";
            return false;
        }
        url = m_apiBaseUrl + "/api/v1/contacts/request";
        token = m_token;
    }

    json payload;
    payload["target_landrop_id"] = targetLandropId;

    HttpResponse res = m_httpClient->post(url, payload.dump(), token, 5);
    if (!res.isOk()) {
        if (outError) *outError = "Failed to send contact request";
        return false;
    }

    try {
        json j = json::parse(res.body);
        outContact.userId = j.value("user_id", "");
        outContact.targetUserId = j.value("target_user_id", "");
        outContact.landropId = j.value("landrop_id", "");
        outContact.displayName = j.value("display_name", "");
        outContact.state = j.value("state", "REQUEST_SENT");
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }
}

bool IdentityClient::acceptContactRequest(const std::string& targetUserId, std::string* outError) {
    std::string url, token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_token.empty()) return false;
        url = m_apiBaseUrl + "/api/v1/contacts/accept";
        token = m_token;
    }
    json payload;
    payload["target_user_id"] = targetUserId;
    HttpResponse res = m_httpClient->post(url, payload.dump(), token, 5);
    if (!res.isOk()) {
        if (outError) *outError = "Failed to accept contact";
        return false;
    }
    return true;
}

bool IdentityClient::declineContactRequest(const std::string& targetUserId, std::string* outError) {
    (void)outError;
    std::string url, token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_token.empty()) return false;
        url = m_apiBaseUrl + "/api/v1/contacts/decline";
        token = m_token;
    }
    json payload;
    payload["target_user_id"] = targetUserId;
    HttpResponse res = m_httpClient->post(url, payload.dump(), token, 5);
    return res.isOk();
}

bool IdentityClient::blockUser(const std::string& targetUserId, std::string* outError) {
    (void)outError;
    std::string url, token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_token.empty()) return false;
        url = m_apiBaseUrl + "/api/v1/contacts/block";
        token = m_token;
    }
    json payload;
    payload["target_user_id"] = targetUserId;
    HttpResponse res = m_httpClient->post(url, payload.dump(), token, 5);
    return res.isOk();
}

bool IdentityClient::setFavorite(const std::string& targetUserId, bool isFavorite, std::string* outError) {
    (void)outError;
    std::string url, token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_token.empty()) return false;
        url = m_apiBaseUrl + "/api/v1/contacts/favorite";
        token = m_token;
    }
    json payload;
    payload["target_user_id"] = targetUserId;
    payload["is_favorite"] = isFavorite;
    HttpResponse res = m_httpClient->post(url, payload.dump(), token, 5);
    return res.isOk();
}

bool IdentityClient::getRecentUsers(std::vector<RecentUserInfo>& outRecents, std::string* outError) {
    std::string url, token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_token.empty()) return false;
        url = m_apiBaseUrl + "/api/v1/contacts/recents";
        token = m_token;
    }

    HttpRequest req;
    req.method = "GET";
    req.url = url;
    req.headers["Authorization"] = "Bearer " + token;

    HttpResponse res = m_httpClient->send(req);
    if (!res.isOk()) return false;

    try {
        json arr = json::parse(res.body);
        outRecents.clear();
        for (const auto& j : arr) {
            RecentUserInfo r;
            r.targetUserId = j.value("target_user_id", "");
            r.landropId = j.value("landrop_id", "");
            r.displayName = j.value("display_name", "");
            r.avatarUrl = j.value("avatar_url", "");
            r.lastInteraction = j.value("last_interaction", "");
            r.lastTransferStatus = j.value("last_transfer_status", "");
            outRecents.push_back(r);
        }
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }
}

bool IdentityClient::getHistory(std::vector<TransferHistoryItem>& outHistory, int limit, std::string* outError) {
    std::string url, token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_token.empty()) {
            if (outError) *outError = "Not authenticated";
            return false;
        }
        url = m_apiBaseUrl + "/api/v1/history?limit=" + std::to_string(limit > 0 ? limit : 20);
        token = m_token;
    }

    HttpRequest req;
    req.method = "GET";
    req.url = url;
    req.headers["Authorization"] = "Bearer " + token;

    HttpResponse res = m_httpClient->send(req);
    if (!res.isOk()) {
        if (outError) *outError = res.errorMessage.empty() ? ("HTTP " + std::to_string(res.statusCode)) : res.errorMessage;
        return false;
    }

    try {
        json arr = json::parse(res.body);
        outHistory.clear();
        for (const auto& item : arr) {
            TransferHistoryItem h;
            h.transferId = item.value("transfer_id", "");
            h.userId = item.value("user_id", "");
            h.direction = item.value("direction", "upload");
            h.peerUserId = item.value("peer_user_id", "");
            h.peerLandropId = item.value("peer_landrop_id", "");
            h.peerDeviceName = item.value("peer_device_name", "");
            h.totalBytes = item.value("total_bytes", static_cast<uint64_t>(0));
            h.status = item.value("status", "COMPLETED");
            h.transportType = item.value("transport_type", "WebRtcDataChannel");
            h.startedAt = item.value("started_at", "");
            h.completedAt = item.value("completed_at", "");

            if (item.contains("files") && item["files"].is_array()) {
                for (const auto& f : item["files"]) {
                    TransferFileMetadata fm;
                    fm.fileName = f.value("file_name", "");
                    fm.fileSize = f.value("file_size", static_cast<uint64_t>(0));
                    fm.sha256Hex = f.value("sha256_hex", "");
                    h.files.push_back(fm);
                }
            }
            outHistory.push_back(h);
        }
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }
}

bool IdentityClient::recordHistory(const TransferHistoryItem& item, std::string* outError) {
    std::string url, token, userId;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_token.empty()) {
            if (outError) *outError = "Not authenticated";
            return false;
        }
        url = m_apiBaseUrl + "/api/v1/history";
        token = m_token;
        userId = m_user.userId;
    }

    json payload;
    payload["transfer_id"] = item.transferId.empty() ? ("xfer-" + generateRandomHex(8)) : item.transferId;
    payload["user_id"] = userId;
    payload["direction"] = item.direction.empty() ? "upload" : item.direction;
    payload["peer_user_id"] = item.peerUserId;
    payload["peer_landrop_id"] = item.peerLandropId;
    payload["peer_device_name"] = item.peerDeviceName;
    payload["total_bytes"] = item.totalBytes;
    payload["status"] = item.status.empty() ? "COMPLETED" : item.status;
    payload["transport_type"] = item.transportType.empty() ? "WebRtcDataChannel" : item.transportType;

    json filesArr = json::array();
    for (const auto& f : item.files) {
        filesArr.push_back({
            {"file_name", f.fileName},
            {"file_size", f.fileSize},
            {"sha256_hex", f.sha256Hex}
        });
    }
    payload["files"] = filesArr;

    HttpRequest req;
    req.method = "POST";
    req.url = url;
    req.headers["Authorization"] = "Bearer " + token;
    req.headers["Content-Type"] = "application/json";
    req.body = payload.dump();

    HttpResponse res = m_httpClient->send(req);
    if (!res.isOk()) {
        if (outError) *outError = res.errorMessage.empty() ? ("HTTP " + std::to_string(res.statusCode)) : res.errorMessage;
        return false;
    }
    return true;
}

bool IdentityClient::clearHistory(std::string* outError) {
    std::string url, token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_token.empty()) {
            if (outError) *outError = "Not authenticated";
            return false;
        }
        url = m_apiBaseUrl + "/api/v1/history";
        token = m_token;
    }

    HttpRequest req;
    req.method = "DELETE";
    req.url = url;
    req.headers["Authorization"] = "Bearer " + token;

    HttpResponse res = m_httpClient->send(req);
    if (!res.isOk()) {
        if (outError) *outError = res.errorMessage.empty() ? ("HTTP " + std::to_string(res.statusCode)) : res.errorMessage;
        return false;
    }
    return true;
}

} // namespace landrop
