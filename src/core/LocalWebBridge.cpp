#include "landrop/core/LocalWebBridge.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <chrono>

namespace landrop {

LocalWebBridge::LocalWebBridge(uint16_t listenPort)
    : m_port(listenPort) {
}

LocalWebBridge::~LocalWebBridge() {
    stop();
}

bool LocalWebBridge::isOriginAllowed(const std::string& origin) {
    if (origin.empty() || origin == "null") {
        return true;
    }
    // Allow localhost / loopback on any port
    if (origin.rfind("http://localhost:", 0) == 0 ||
        origin.rfind("https://localhost:", 0) == 0 ||
        origin == "http://localhost" ||
        origin == "https://localhost" ||
        origin.rfind("http://127.0.0.1:", 0) == 0 ||
        origin.rfind("https://127.0.0.1:", 0) == 0 ||
        origin == "http://127.0.0.1" ||
        origin == "https://127.0.0.1") {
        return true;
    }
    // Allow official LAN-Drop web client domains
    if (origin == "https://landrop.io" || origin == "http://landrop.io") {
        return true;
    }
    return false;
}

bool LocalWebBridge::start(BridgePeerProvider peerProvider) {
    stop();

    m_peerProvider = std::move(peerProvider);

    m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenFd == INVALID_SOCKET_HANDLE) {
        return false;
    }

    int opt = 1;
#ifndef _WIN32
    setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#else
    setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    // Explicitly bind strictly to 127.0.0.1 loopback for defense in depth
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) <= 0) {
        closeSocket(m_listenFd);
        m_listenFd = INVALID_SOCKET_HANDLE;
        return false;
    }

    if (bind(m_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeSocket(m_listenFd);
        m_listenFd = INVALID_SOCKET_HANDLE;
        return false;
    }

    if (listen(m_listenFd, 16) != 0) {
        closeSocket(m_listenFd);
        m_listenFd = INVALID_SOCKET_HANDLE;
        return false;
    }

    m_running.store(true);
    m_serverThread = std::make_unique<std::thread>(&LocalWebBridge::serverLoop, this);
    return true;
}

void LocalWebBridge::stop() {
    m_running.store(false);

    if (m_listenFd != INVALID_SOCKET_HANDLE) {
        closeSocket(m_listenFd);
        m_listenFd = INVALID_SOCKET_HANDLE;
    }

    if (m_serverThread && m_serverThread->joinable()) {
        m_serverThread->join();
    }
    m_serverThread.reset();
}

void LocalWebBridge::serverLoop() {
    while (m_running.load()) {
        if (m_listenFd == INVALID_SOCKET_HANDLE) break;

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(m_listenFd, &fds);
        timeval tv{0, 200000}; // 200 ms timeout for clean thread shutdown

        int ret = select(static_cast<int>(m_listenFd + 1), &fds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(m_listenFd, &fds)) {
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            socket_handle_t clientFd = accept(m_listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
            if (clientFd != INVALID_SOCKET_HANDLE) {
                handleClient(clientFd);
            }
        }
    }
}

static std::string escapeJsonString(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (static_cast<unsigned char>(c) < 32) {
            // control char skip or hex
        } else {
            out += c;
        }
    }
    return out;
}

void LocalWebBridge::handleClient(socket_handle_t clientFd) {
    char reqBuf[4096];
    std::memset(reqBuf, 0, sizeof(reqBuf));

    int bytesRead = recv(clientFd, reqBuf, sizeof(reqBuf) - 1, 0);
    if (bytesRead <= 0) {
        closeSocket(clientFd);
        return;
    }

    std::string req(reqBuf, bytesRead);
    std::istringstream reqStream(req);
    std::string method, path, protocol;
    reqStream >> method >> path >> protocol;

    // Parse headers to locate Origin
    std::string originHeader;
    std::string line;
    std::getline(reqStream, line); // consume rest of first line
    while (std::getline(reqStream, line) && line != "\r" && !line.empty()) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("Origin:", 0) == 0 || line.rfind("origin:", 0) == 0) {
            originHeader = line.substr(7);
            // trim whitespace
            while (!originHeader.empty() && (originHeader.front() == ' ' || originHeader.front() == '\t')) {
                originHeader.erase(originHeader.begin());
            }
        }
    }

    bool allowedOrigin = isOriginAllowed(originHeader);
    std::string allowOriginStr = allowedOrigin ? (originHeader.empty() ? "*" : originHeader) : "";

    std::ostringstream resp;

    if (!allowedOrigin) {
        resp << "HTTP/1.1 403 Forbidden\r\n"
             << "Content-Type: text/plain\r\n"
             << "Content-Length: 17\r\n"
             << "Connection: close\r\n\r\n"
             << "Forbidden Origin\n";
    } else if (method == "OPTIONS") {
        resp << "HTTP/1.1 204 No Content\r\n"
             << "Access-Control-Allow-Origin: " << allowOriginStr << "\r\n"
             << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
             << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
             << "Access-Control-Max-Age: 86400\r\n"
             << "Connection: close\r\n\r\n";
    } else if (method == "GET" && (path == "/bridge/status" || path == "/status")) {
        std::string body = "{\"status\":\"ok\",\"service\":\"LAN-Drop Local Bridge\",\"version\":\"1.0.2\",\"port\":8848}";
        resp << "HTTP/1.1 200 OK\r\n"
             << "Access-Control-Allow-Origin: " << allowOriginStr << "\r\n"
             << "Content-Type: application/json; charset=utf-8\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    } else if (method == "GET" && (path == "/bridge/peers" || path == "/peers")) {
        std::vector<BridgePeerInfo> peers;
        if (m_peerProvider) {
            peers = m_peerProvider();
        }

        std::ostringstream jsonStream;
        jsonStream << "[";
        for (size_t i = 0; i < peers.size(); ++i) {
            const auto& p = peers[i];
            if (i > 0) jsonStream << ",";
            jsonStream << "{\"id\":\"" << escapeJsonString(p.id) << "\","
                       << "\"name\":\"" << escapeJsonString(p.name) << "\","
                       << "\"ip\":\"" << escapeJsonString(p.ip) << "\","
                       << "\"port\":" << p.port << ","
                       << "\"deviceType\":\"" << escapeJsonString(p.deviceType) << "\"}";
        }
        jsonStream << "]";
        std::string body = jsonStream.str();

        resp << "HTTP/1.1 200 OK\r\n"
             << "Access-Control-Allow-Origin: " << allowOriginStr << "\r\n"
             << "Content-Type: application/json; charset=utf-8\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    } else {
        std::string body = "{\"error\":\"Not Found\"}";
        resp << "HTTP/1.1 404 Not Found\r\n"
             << "Access-Control-Allow-Origin: " << allowOriginStr << "\r\n"
             << "Content-Type: application/json\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    }

    std::string respStr = resp.str();
    send(clientFd, respStr.data(), static_cast<int>(respStr.size()), 0);
    closeSocket(clientFd);
}

} // namespace landrop
