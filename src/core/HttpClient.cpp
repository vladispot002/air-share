#include "landrop/core/HttpClient.hpp"
#include "landrop/core/TlsContext.hpp"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace landrop {

bool HttpClient::parseUrl(const std::string& url, std::string& outHost, uint16_t& outPort, std::string& outPath, bool& outIsHttps) {
    if (url.empty()) return false;

    std::string remaining = url;
    outIsHttps = false;

    if (remaining.rfind("https://", 0) == 0) {
        outIsHttps = true;
        outPort = 443;
        remaining = remaining.substr(8);
    } else if (remaining.rfind("http://", 0) == 0) {
        outIsHttps = false;
        outPort = 80;
        remaining = remaining.substr(7);
    } else {
        outPort = 80;
    }

    size_t slashPos = remaining.find('/');
    std::string hostPort;
    if (slashPos != std::string::npos) {
        hostPort = remaining.substr(0, slashPos);
        outPath = remaining.substr(slashPos);
    } else {
        hostPort = remaining;
        outPath = "/";
    }

    size_t colonPos = hostPort.find(':');
    if (colonPos != std::string::npos) {
        outHost = hostPort.substr(0, colonPos);
        try {
            outPort = static_cast<uint16_t>(std::stoi(hostPort.substr(colonPos + 1)));
        } catch (...) {
            return false;
        }
    } else {
        outHost = hostPort;
    }

    return !outHost.empty();
}

HttpResponse HttpClient::get(const std::string& url, int timeoutSeconds) {
    HttpRequest req;
    req.method = "GET";
    req.url = url;
    req.timeoutSeconds = timeoutSeconds;
    return send(req);
}

HttpResponse HttpClient::post(const std::string& url, const std::string& jsonBody, const std::string& bearerToken, int timeoutSeconds) {
    HttpRequest req;
    req.method = "POST";
    req.url = url;
    req.body = jsonBody;
    req.timeoutSeconds = timeoutSeconds;
    req.headers["Content-Type"] = "application/json";
    if (!bearerToken.empty()) {
        req.headers["Authorization"] = "Bearer " + bearerToken;
    }
    return send(req);
}

HttpResponse HttpClient::send(const HttpRequest& request) {
    HttpResponse res;

    std::string host = request.host;
    uint16_t port = request.port;
    std::string path = request.path;
    bool isHttps = request.isHttps;

    if (!request.url.empty()) {
        if (!parseUrl(request.url, host, port, path, isHttps)) {
            res.success = false;
            res.statusCode = 0;
            res.errorMessage = "Malformed URL: " + request.url;
            return res;
        }
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        res.success = false;
        res.statusCode = 0;
        res.errorMessage = "Failed to create socket";
        return res;
    }

    // Set send and receive timeouts
    struct timeval tv;
    tv.tv_sec = request.timeoutSeconds;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0) {
        // Try DNS resolve
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) {
            close(sock);
            res.success = false;
            res.statusCode = 0;
            res.errorMessage = "Could not resolve host: " + host;
            return res;
        }
        auto* ipv4 = reinterpret_cast<sockaddr_in*>(result->ai_addr);
        serverAddr.sin_addr = ipv4->sin_addr;
        freeaddrinfo(result);
    }

    if (connect(sock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
        close(sock);
        res.success = false;
        res.statusCode = 0;
        res.errorMessage = "Connection failed: " + std::string(strerror(errno));
        return res;
    }

    SSL_CTX* sslCtx = nullptr;
    SSL* ssl = nullptr;

    if (isHttps) {
        TlsHelper::initOpenSsl();
        sslCtx = TlsHelper::createClientContext();
        if (!sslCtx) {
            close(sock);
            res.success = false;
            res.statusCode = 0;
            res.errorMessage = "Failed to create TLS client context";
            return res;
        }
        ssl = SSL_new(sslCtx);
        SSL_set_fd(ssl, sock);
        if (SSL_connect(ssl) <= 0) {
            SSL_free(ssl);
            SSL_CTX_free(sslCtx);
            close(sock);
            res.success = false;
            res.statusCode = 0;
            res.errorMessage = "TLS handshake failed for HTTPS";
            return res;
        }
    }

    // Format HTTP request
    std::ostringstream reqStream;
    reqStream << request.method << " " << path << " HTTP/1.1\r\n";
    reqStream << "Host: " << host << ":" << port << "\r\n";
    reqStream << "User-Agent: LAN-Drop-HttpClient/1.0\r\n";
    reqStream << "Connection: close\r\n";

    if (!request.body.empty() && request.headers.find("Content-Length") == request.headers.end()) {
        reqStream << "Content-Length: " << request.body.size() << "\r\n";
    }

    for (const auto& [k, v] : request.headers) {
        reqStream << k << ": " << v << "\r\n";
    }
    reqStream << "\r\n";
    reqStream << request.body;

    std::string fullReq = reqStream.str();

    // Send request
    if (isHttps) {
        if (SSL_write(ssl, fullReq.data(), static_cast<int>(fullReq.size())) <= 0) {
            SSL_free(ssl);
            SSL_CTX_free(sslCtx);
            close(sock);
            res.success = false;
            res.statusCode = 0;
            res.errorMessage = "Failed to send HTTPS request";
            return res;
        }
    } else {
        if (::send(sock, fullReq.data(), fullReq.size(), 0) <= 0) {
            close(sock);
            res.success = false;
            res.statusCode = 0;
            res.errorMessage = "Failed to send HTTP request";
            return res;
        }
    }

    // Read response
    std::string rawResponse;
    char buf[4096];
    while (true) {
        ssize_t n = 0;
        if (isHttps) {
            int ret = SSL_read(ssl, buf, sizeof(buf));
            if (ret <= 0) break;
            n = ret;
        } else {
            n = ::recv(sock, buf, sizeof(buf), 0);
            if (n <= 0) break;
        }
        rawResponse.append(buf, static_cast<size_t>(n));
    }


    if (isHttps) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(sslCtx);
    }
    close(sock);

    if (rawResponse.empty()) {
        res.success = false;
        res.statusCode = 0;
        res.errorMessage = "Empty or timed out response from server";
        return res;
    }

    // Parse HTTP response
    size_t headerEnd = rawResponse.find("\r\n\r\n");
    std::string headerPart;
    if (headerEnd != std::string::npos) {
        headerPart = rawResponse.substr(0, headerEnd);
        res.body = rawResponse.substr(headerEnd + 4);
    } else {
        headerEnd = rawResponse.find("\n\n");
        if (headerEnd != std::string::npos) {
            headerPart = rawResponse.substr(0, headerEnd);
            res.body = rawResponse.substr(headerEnd + 2);
        } else {
            headerPart = rawResponse;
        }
    }

    std::istringstream respLines(headerPart);
    std::string statusLine;
    if (std::getline(respLines, statusLine)) {
        if (!statusLine.empty() && statusLine.back() == '\r') {
            statusLine.pop_back();
        }
        std::istringstream sl(statusLine);
        std::string httpVersion;
        sl >> httpVersion >> res.statusCode;
        std::getline(sl, res.statusMessage);
        if (!res.statusMessage.empty() && res.statusMessage.front() == ' ') {
            res.statusMessage = res.statusMessage.substr(1);
        }
    }

    std::string headerLine;
    while (std::getline(respLines, headerLine)) {
        if (!headerLine.empty() && headerLine.back() == '\r') {
            headerLine.pop_back();
        }
        size_t cPos = headerLine.find(':');
        if (cPos != std::string::npos) {
            std::string k = headerLine.substr(0, cPos);
            std::string v = headerLine.substr(cPos + 1);
            while (!v.empty() && v.front() == ' ') v = v.substr(1);
            res.headers[k] = v;
        }
    }

    res.success = (res.statusCode >= 200 && res.statusCode < 300);
    return res;
}

} // namespace landrop
