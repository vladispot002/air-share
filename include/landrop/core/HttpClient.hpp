#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

namespace landrop {

struct HttpResponse {
    int statusCode{0};
    std::string statusMessage;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    bool success{false};
    std::string errorMessage;

    bool isOk() const { return success && statusCode >= 200 && statusCode < 300; }
};

struct HttpRequest {
    std::string method{"GET"};
    std::string url;
    std::string host{"127.0.0.1"};
    uint16_t port{80};
    std::string path{"/"};
    bool isHttps{false};
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    int timeoutSeconds{5};
    std::string caCertPem;
    bool allowInsecureTls{false};
};

class HttpClient {
public:
    HttpClient() = default;
    ~HttpClient() = default;

    HttpResponse send(const HttpRequest& request);
    HttpResponse get(const std::string& url, int timeoutSeconds = 5);
    HttpResponse get(const std::string& url, int timeoutSeconds, const std::string& caCertPem);

    HttpResponse post(const std::string& url, const std::string& jsonBody, const std::string& bearerToken = "", int timeoutSeconds = 5);
    HttpResponse post(const std::string& url, const std::string& jsonBody, const std::string& bearerToken, int timeoutSeconds, const std::string& caCertPem);

    static bool parseUrl(const std::string& url, std::string& outHost, uint16_t& outPort, std::string& outPath, bool& outIsHttps);
};

} // namespace landrop
