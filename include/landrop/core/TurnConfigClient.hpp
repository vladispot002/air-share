#pragma once

#include <string>
#include <vector>
#include <memory>
#include "landrop/core/HttpClient.hpp"
#include "landrop/core/WebRtcDataChannel.hpp"

namespace landrop {

struct TurnConfigResponse {
    std::vector<std::string> stunServers;
    std::string turnServer;
    std::string turnUsername;
    std::string turnPassword;
    int64_t ttlSeconds{0};
    bool success{false};
    std::string errorMessage;
};

/**
 * @brief Client for querying Coturn ephemeral credentials from the Go signaling daemon.
 * Implements GET /api/v1/turn-config with Bearer token authentication,
 * parsing W3C RTCIceServer dictionaries into landrop::WebRtcConfig.
 */
class TurnConfigClient {
public:
    explicit TurnConfigClient(std::shared_ptr<HttpClient> httpClient = nullptr);
    virtual ~TurnConfigClient() = default;

    bool fetchTurnConfig(
        const std::string& apiBaseUrl,
        const std::string& roomToken,
        const std::string& clientId,
        WebRtcConfig& outConfig,
        int timeoutSeconds = 5,
        std::string* outError = nullptr
    );

    static bool parseTurnConfigJson(
        const std::string& jsonString,
        WebRtcConfig& outConfig,
        int64_t* outTtlSeconds = nullptr,
        std::string* outError = nullptr
    );

private:
    std::shared_ptr<HttpClient> m_httpClient;
};

} // namespace landrop
