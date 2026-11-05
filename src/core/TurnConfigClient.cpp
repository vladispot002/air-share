#include "landrop/core/TurnConfigClient.hpp"
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

namespace landrop {

TurnConfigClient::TurnConfigClient(std::shared_ptr<HttpClient> httpClient)
    : m_httpClient(std::move(httpClient)) {
    if (!m_httpClient) {
        m_httpClient = std::make_shared<HttpClient>();
    }
}

bool TurnConfigClient::parseTurnConfigJson(
    const std::string& jsonString,
    WebRtcConfig& outConfig,
    int64_t* outTtlSeconds,
    std::string* outError
) {
    try {
        json root = json::parse(jsonString);

        if (!root.is_object() || !root.contains("ice_servers") || !root["ice_servers"].is_array()) {
            if (outError) *outError = "Response missing required array \"ice_servers\"";
            return false;
        }

        if (outTtlSeconds) {
            *outTtlSeconds = root.value("ttl_seconds", static_cast<int64_t>(0));
        }

        const auto& iceServers = root["ice_servers"];
        if (!iceServers.empty()) {
            outConfig.iceServers.clear();
        }

        for (const auto& server : iceServers) {
            if (!server.is_object()) continue;

            std::vector<std::string> urls;
            if (server.contains("urls")) {
                if (server["urls"].is_array()) {
                    for (const auto& u : server["urls"]) {
                        if (u.is_string()) urls.push_back(u.get<std::string>());
                    }
                } else if (server["urls"].is_string()) {
                    urls.push_back(server["urls"].get<std::string>());
                }
            }

            std::string username = server.value("username", "");
            std::string credential = server.value("credential", "");

            if (!username.empty() && !credential.empty()) {
                outConfig.turnUsername = username;
                outConfig.turnPassword = credential;
                for (const auto& u : urls) {
                    if (outConfig.turnServer.empty() && (u.rfind("turn:", 0) == 0 || u.rfind("turns:", 0) == 0)) {
                        outConfig.turnServer = u;
                    }
                    outConfig.iceServers.push_back(u);
                }
            } else {
                for (const auto& u : urls) {
                    outConfig.iceServers.push_back(u);
                }
            }
        }
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = std::string("Malformed JSON or invalid types in response: ") + e.what();
        return false;
    }
}

bool TurnConfigClient::fetchTurnConfig(
    const std::string& apiBaseUrl,
    const std::string& roomToken,
    const std::string& clientId,
    WebRtcConfig& outConfig,
    int timeoutSeconds,
    std::string* outError
) {
    if (apiBaseUrl.empty()) {
        if (outError) *outError = "API base URL is empty";
        return false;
    }

    std::string url = apiBaseUrl;
    if (url.back() == '/') {
        url.pop_back();
    }
    url += "/api/v1/turn-config?token=" + roomToken;
    if (!clientId.empty()) {
        url += "&client_id=" + clientId;
    }

    HttpRequest req;
    req.method = "GET";
    req.url = url;
    req.headers["Authorization"] = "Bearer " + roomToken;
    req.headers["Accept"] = "application/json";
    req.timeoutSeconds = timeoutSeconds > 0 ? timeoutSeconds : 5;

    HttpResponse resp = m_httpClient->send(req);
    if (!resp.isOk()) {
        std::string err = "HTTP " + std::to_string(resp.statusCode) + ": " + resp.errorMessage;
        if (!resp.body.empty()) {
            err += " (" + resp.body + ")";
        }
        if (outError) *outError = err;
        return false;
    }

    return parseTurnConfigJson(resp.body, outConfig, nullptr, outError);
}

} // namespace landrop
