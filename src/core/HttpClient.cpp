#include "landrop/core/HttpClient.hpp"
#include "landrop/core/TlsContext.hpp"
#include "landrop/utils/SocketCompat.hpp"
#include <cstring>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <openssl/x509v3.h>

namespace landrop {

namespace {

struct SocketGuard {
    int fd{-1};
    ~SocketGuard() {
        if (fd >= 0) {
            closeSocket(fd);
        }
    }
};

struct SslCtxGuard {
    SSL_CTX* ctx{nullptr};
    ~SslCtxGuard() {
        if (ctx) {
            SSL_CTX_free(ctx);
        }
    }
};

struct SslGuard {
    SSL* ssl{nullptr};
    ~SslGuard() {
        if (ssl) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
    }
};

} // namespace

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
        return false;
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

    if (hostPort.empty()) {
        return false;
    }

    size_t colonPos = hostPort.find(':');
    if (colonPos != std::string::npos) {
        outHost = hostPort.substr(0, colonPos);
        try {
            int p = std::stoi(hostPort.substr(colonPos + 1));
            if (p <= 0 || p > 65535) return false;
            outPort = static_cast<uint16_t>(p);
        } catch (...) {
            return false;
        }
    } else {
        outHost = hostPort;
    }

    return !outHost.empty();
}

HttpResponse HttpClient::get(const std::string& url, int timeoutSeconds) {
    return get(url, timeoutSeconds, "");
}

HttpResponse HttpClient::get(const std::string& url, int timeoutSeconds, const std::string& caCertPem) {
    HttpRequest req;
    req.method = "GET";
    req.url = url;
    req.timeoutSeconds = timeoutSeconds;
    req.caCertPem = caCertPem;
    return send(req);
}

HttpResponse HttpClient::post(const std::string& url, const std::string& jsonBody, const std::string& bearerToken, int timeoutSeconds) {
    return post(url, jsonBody, bearerToken, timeoutSeconds, "");
}

HttpResponse HttpClient::post(const std::string& url, const std::string& jsonBody, const std::string& bearerToken, int timeoutSeconds, const std::string& caCertPem) {
    HttpRequest req;
    req.method = "POST";
    req.url = url;
    req.body = jsonBody;
    req.timeoutSeconds = timeoutSeconds;
    req.caCertPem = caCertPem;
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

    SocketGuard sockGuard;
    sockGuard.fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockGuard.fd < 0) {
        res.success = false;
        res.statusCode = 0;
        res.errorMessage = "Failed to create socket";
        return res;
    }

    int timeoutSec = (request.timeoutSeconds > 0) ? request.timeoutSeconds : 5;
    struct timeval tv;
    tv.tv_sec = timeoutSec;
    tv.tv_usec = 0;
    setsockopt(sockGuard.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sockGuard.fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

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
            res.success = false;
            res.statusCode = 0;
            res.errorMessage = "Could not resolve host: " + host;
            return res;
        }
        auto* ipv4 = reinterpret_cast<sockaddr_in*>(result->ai_addr);
        serverAddr.sin_addr = ipv4->sin_addr;
        freeaddrinfo(result);
    }

    // Non-blocking connect with timeout guard
    setSocketNonBlocking(sockGuard.fd, true);

    int connRet = connect(sockGuard.fd, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
    if (connRet < 0) {
        bool inProgress = false;
#if defined(_WIN32) || defined(_WIN64)
        inProgress = (::WSAGetLastError() == WSAEWOULDBLOCK);
#else
        inProgress = (errno == EINPROGRESS);
#endif
        if (inProgress) {
            fd_set wfds, efds;
            FD_ZERO(&wfds);
            FD_ZERO(&efds);
            FD_SET(sockGuard.fd, &wfds);
            FD_SET(sockGuard.fd, &efds);

            struct timeval tvConn;
            tvConn.tv_sec = timeoutSec;
            tvConn.tv_usec = 0;

            int sel = select(sockGuard.fd + 1, nullptr, &wfds, &efds, &tvConn);
            if (sel <= 0) {
                res.success = false;
                res.statusCode = 0;
                res.errorMessage = (sel == 0) ? "Connection timed out" : "Connection select error";
                return res;
            }

            int so_error = 0;
            socklen_t len = sizeof(so_error);
            getsockopt(sockGuard.fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &len);
            if (so_error != 0) {
                res.success = false;
                res.statusCode = 0;
                res.errorMessage = "Connection failed";
                return res;
            }
        } else {
            res.success = false;
            res.statusCode = 0;
            res.errorMessage = "Connection failed";
            return res;
        }
    }
    setSocketNonBlocking(sockGuard.fd, false);

    SslCtxGuard sslCtxGuard;
    SslGuard sslGuard;

    if (isHttps) {
        TlsHelper::initOpenSsl();
        sslCtxGuard.ctx = TlsHelper::createClientContext(request.caCertPem);
        if (!sslCtxGuard.ctx) {
            res.success = false;
            res.statusCode = 0;
            res.errorMessage = "Failed to create TLS client context";
            return res;
        }

        sslGuard.ssl = SSL_new(sslCtxGuard.ctx);
        if (!sslGuard.ssl) {
            res.success = false;
            res.statusCode = 0;
            res.errorMessage = "Failed to create SSL instance";
            return res;
        }

        SSL_set_fd(sslGuard.ssl, sockGuard.fd);

        // Pass SNI (Server Name Indication) - RFC 6066: literal IP addresses must NOT be sent
        sockaddr_in ipCheck{};
        if (inet_pton(AF_INET, host.c_str(), &ipCheck.sin_addr) != 1) {
            SSL_set_tlsext_host_name(sslGuard.ssl, host.c_str());
        }

        // Hostname verification & Peer verification (unless allowInsecureTls is explicitly opted-in)
        if (!request.allowInsecureTls) {
            X509_VERIFY_PARAM* param = SSL_get0_param(sslGuard.ssl);
            if (param) {
                sockaddr_in dummyAddr{};
                if (inet_pton(AF_INET, host.c_str(), &dummyAddr.sin_addr) == 1) {
                    X509_VERIFY_PARAM_set1_ip_asc(param, host.c_str());
                } else {
                    X509_VERIFY_PARAM_set1_host(param, host.c_str(), 0);
                }
            }
            SSL_set_verify(sslGuard.ssl, SSL_VERIFY_PEER, nullptr);
        } else {
            SSL_set_verify(sslGuard.ssl, SSL_VERIFY_NONE, nullptr);
        }

        // Perform TLS Handshake
        int ret = SSL_connect(sslGuard.ssl);
        if (ret <= 0) {
            long verifyRes = SSL_get_verify_result(sslGuard.ssl);
            std::string errDetail;
            if (verifyRes != X509_V_OK) {
                errDetail = X509_verify_cert_error_string(verifyRes);
            } else {
                unsigned long osslErr = ERR_get_error();
                char errBuf[256] = {0};
                ERR_error_string_n(osslErr, errBuf, sizeof(errBuf));
                errDetail = errBuf;
            }
            res.success = false;
            res.statusCode = 0;
            res.errorMessage = "TLS handshake failed: " + errDetail;
            return res;
        }

        // Post-handshake certificate check
        if (!request.allowInsecureTls) {
            long verifyRes = SSL_get_verify_result(sslGuard.ssl);
            if (verifyRes != X509_V_OK) {
                res.success = false;
                res.statusCode = 0;
                res.errorMessage = "Certificate verification failed: " + std::string(X509_verify_cert_error_string(verifyRes));
                return res;
            }
        }
    }

    // Format HTTP request
    std::ostringstream reqStream;
    reqStream << request.method << " " << path << " HTTP/1.1\r\n";
    reqStream << "Host: " << host << ":" << port << "\r\n";
    reqStream << "User-Agent: LAN-Drop-HttpClient/1.0\r\n";
    reqStream << "Connection: close\r\n";

    bool hasContentLength = false;
    for (const auto& [k, v] : request.headers) {
        std::string lowerK = k;
        std::transform(lowerK.begin(), lowerK.end(), lowerK.begin(), ::tolower);
        if (lowerK == "content-length") {
            hasContentLength = true;
            break;
        }
    }

    if (!request.body.empty() && !hasContentLength) {
        reqStream << "Content-Length: " << request.body.size() << "\r\n";
    }

    for (const auto& [k, v] : request.headers) {
        reqStream << k << ": " << v << "\r\n";
    }
    reqStream << "\r\n";
    reqStream << request.body;

    std::string fullReq = reqStream.str();

    // Send request loop (handles partial writes)
    size_t totalSent = 0;
    while (totalSent < fullReq.size()) {
        if (isHttps) {
            int toSend = static_cast<int>(std::min<size_t>(fullReq.size() - totalSent, 16384));
            int written = SSL_write(sslGuard.ssl, fullReq.data() + totalSent, toSend);
            if (written <= 0) {
                int err = SSL_get_error(sslGuard.ssl, written);
                res.success = false;
                res.statusCode = 0;
                res.errorMessage = "Failed to send HTTPS request (SSL error " + std::to_string(err) + ")";
                return res;
            }
            totalSent += static_cast<size_t>(written);
        } else {
            size_t toSend = std::min<size_t>(fullReq.size() - totalSent, 16384);
            int flags = 0;
#ifdef MSG_NOSIGNAL
            flags |= MSG_NOSIGNAL;
#endif
            ssize_t sent = ::send(sockGuard.fd, fullReq.data() + totalSent, static_cast<int>(toSend), flags);
            if (sent <= 0) {
                res.success = false;
                res.statusCode = 0;
                res.errorMessage = "Failed to send HTTP request";
                return res;
            }
            totalSent += static_cast<size_t>(sent);
        }
    }

    // Read response loop
    std::string rawResponse;
    char buf[8192];
    size_t expectedContentLength = std::string::npos;
    bool headersParsed = false;
    size_t headerSepLen = 0;
    size_t headerEnd = std::string::npos;
    bool readError = false;
    std::string readErrorMsg;

    while (true) {
        ssize_t n = 0;
        if (isHttps) {
            int ret = SSL_read(sslGuard.ssl, buf, sizeof(buf));
            if (ret <= 0) {
                int sslErr = SSL_get_error(sslGuard.ssl, ret);
                if (sslErr != SSL_ERROR_ZERO_RETURN && sslErr != SSL_ERROR_NONE) {
                    if (ret < 0) {
                        readError = true;
                        readErrorMsg = "TLS read error (SSL error " + std::to_string(sslErr) + ")";
                    }
                }
                break;
            }
            n = ret;
        } else {
            n = ::recv(sockGuard.fd, buf, static_cast<int>(sizeof(buf)), 0);
            if (n < 0) {
                readError = true;
                readErrorMsg = "Socket receive error";
                break;
            }
            if (n == 0) {
                break; // Clean TCP EOF
            }
        }
        rawResponse.append(buf, static_cast<size_t>(n));

        if (!headersParsed) {
            headerEnd = rawResponse.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                headerSepLen = 4;
                headersParsed = true;
            } else {
                headerEnd = rawResponse.find("\n\n");
                if (headerEnd != std::string::npos) {
                    headerSepLen = 2;
                    headersParsed = true;
                }
            }

            if (headersParsed) {
                std::string headerPart = rawResponse.substr(0, headerEnd);
                std::istringstream hStream(headerPart);
                std::string line;
                while (std::getline(hStream, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    size_t cPos = line.find(':');
                    if (cPos != std::string::npos) {
                        std::string k = line.substr(0, cPos);
                        std::string v = line.substr(cPos + 1);
                        std::string lowerK = k;
                        std::transform(lowerK.begin(), lowerK.end(), lowerK.begin(), ::tolower);
                        if (lowerK == "content-length") {
                            while (!v.empty() && v.front() == ' ') v = v.substr(1);
                            while (!v.empty() && v.back() == ' ') v.pop_back();
                            try {
                                expectedContentLength = std::stoull(v);
                            } catch (...) {}
                            break;
                        }
                    }
                }
            }
        }

        if (headersParsed && expectedContentLength != std::string::npos) {
            size_t currentBodySize = rawResponse.size() - (headerEnd + headerSepLen);
            if (currentBodySize >= expectedContentLength) {
                break;
            }
        }
    }

    if (readError) {
        res.success = false;
        res.statusCode = 0;
        res.errorMessage = readErrorMsg.empty() ? "Read error from server" : readErrorMsg;
        return res;
    }

    if (rawResponse.empty()) {
        res.success = false;
        res.statusCode = 0;
        res.errorMessage = "Empty or timed out response from server";
        return res;
    }

    if (!headersParsed) {
        res.success = false;
        res.statusCode = 0;
        res.errorMessage = "Malformed HTTP response: missing header delimiter";
        return res;
    }

    std::string headerPart = rawResponse.substr(0, headerEnd);
    res.body = rawResponse.substr(headerEnd + headerSepLen);

    // Bound body to expected Content-Length if extra bytes arrived
    if (expectedContentLength != std::string::npos && res.body.size() > expectedContentLength) {
        res.body.resize(expectedContentLength);
    }

    // Parse HTTP status line
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

    // Parse headers
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

    // Detect premature connection drop / truncation
    if (expectedContentLength != std::string::npos && res.body.size() < expectedContentLength) {
        res.success = false;
        res.statusCode = 0;
        res.errorMessage = "Truncated HTTP response: expected " + std::to_string(expectedContentLength) +
                           " bytes, received " + std::to_string(res.body.size());
        return res;
    }

    res.success = (res.statusCode >= 200 && res.statusCode < 300);
    return res;
}

} // namespace landrop
