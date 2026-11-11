#include "landrop/core/LocalWebBridge.hpp"
#include "landrop/core/FileChunkHasher.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <filesystem>

namespace landrop {

LocalWebBridge::LocalWebBridge(uint16_t listenPort)
    : m_port(listenPort) {
}

LocalWebBridge::~LocalWebBridge() {
    stop();
}

void LocalWebBridge::setStorageDirectory(const std::string& dir) {
    m_storageDir = dir;
}

std::string LocalWebBridge::getStorageDirectory() const {
    return m_storageDir;
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

static std::string urlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            int value = 0;
            std::istringstream is(in.substr(i + 1, 2));
            if (is >> std::hex >> value) {
                out += static_cast<char>(value);
                i += 2;
            } else {
                out += in[i];
            }
        } else if (in[i] == '+') {
            out += ' ';
        } else {
            out += in[i];
        }
    }
    return out;
}

void LocalWebBridge::handleClient(socket_handle_t clientFd) {
    std::vector<char> headerBuf;
    headerBuf.resize(4096);
    int bytesRead = recv(clientFd, headerBuf.data(), static_cast<int>(headerBuf.size() - 1), 0);
    if (bytesRead <= 0) {
        closeSocket(clientFd);
        return;
    }
    headerBuf[bytesRead] = '\0';

    std::string initialData(headerBuf.data(), bytesRead);
    size_t headerEnd = initialData.find("\r\n\r\n");
    while (headerEnd == std::string::npos && bytesRead > 0 && initialData.size() < 65536) {
        char moreBuf[2048];
        int n = recv(clientFd, moreBuf, sizeof(moreBuf), 0);
        if (n <= 0) break;
        initialData.append(moreBuf, n);
        headerEnd = initialData.find("\r\n\r\n");
    }

    if (headerEnd == std::string::npos) {
        closeSocket(clientFd);
        return;
    }

    std::string headerStr = initialData.substr(0, headerEnd);
    std::string bodyPrefix = initialData.substr(headerEnd + 4);

    std::istringstream reqStream(headerStr);
    std::string method, fullPath, protocol;
    reqStream >> method >> fullPath >> protocol;

    // Parse path and query
    std::string pathOnly = fullPath;
    std::string queryString;
    size_t qPos = fullPath.find('?');
    if (qPos != std::string::npos) {
        pathOnly = fullPath.substr(0, qPos);
        queryString = fullPath.substr(qPos + 1);
    }

    // Parse headers
    std::string originHeader;
    std::string filenameHeader;
    uint64_t contentLength = 0;
    std::string line;
    std::getline(reqStream, line); // consume remainder of first line

    while (std::getline(reqStream, line) && line != "\r" && !line.empty()) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("Origin:", 0) == 0 || line.rfind("origin:", 0) == 0) {
            originHeader = line.substr(7);
            while (!originHeader.empty() && (originHeader.front() == ' ' || originHeader.front() == '\t')) {
                originHeader.erase(originHeader.begin());
            }
        } else if (line.rfind("Content-Length:", 0) == 0 || line.rfind("content-length:", 0) == 0) {
            std::string clStr = line.substr(15);
            try {
                contentLength = std::stoull(clStr);
            } catch (...) {
                contentLength = 0;
            }
        } else if (line.rfind("X-Filename:", 0) == 0 || line.rfind("x-filename:", 0) == 0) {
            filenameHeader = line.substr(11);
            while (!filenameHeader.empty() && (filenameHeader.front() == ' ' || filenameHeader.front() == '\t')) {
                filenameHeader.erase(filenameHeader.begin());
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
        std::string s = resp.str();
        send(clientFd, s.data(), static_cast<int>(s.size()), 0);
        closeSocket(clientFd);
        return;
    }

    if (method == "OPTIONS") {
        resp << "HTTP/1.1 204 No Content\r\n"
             << "Access-Control-Allow-Origin: " << allowOriginStr << "\r\n"
             << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
             << "Access-Control-Allow-Headers: Content-Type, Authorization, X-Filename\r\n"
             << "Access-Control-Max-Age: 86400\r\n"
             << "Connection: close\r\n\r\n";
        std::string s = resp.str();
        send(clientFd, s.data(), static_cast<int>(s.size()), 0);
        closeSocket(clientFd);
        return;
    }

    if (method == "GET" && (pathOnly == "/bridge/status" || pathOnly == "/status")) {
        std::string body = "{\"status\":\"ok\",\"service\":\"LAN-Drop Local Bridge\",\"version\":\"1.0.2\",\"port\":"
            + std::to_string(m_port) + ",\"transferPort\":8848}";
        resp << "HTTP/1.1 200 OK\r\n"
             << "Access-Control-Allow-Origin: " << allowOriginStr << "\r\n"
             << "Content-Type: application/json; charset=utf-8\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
        std::string s = resp.str();
        send(clientFd, s.data(), static_cast<int>(s.size()), 0);
        closeSocket(clientFd);
        return;
    }

    if (method == "GET" && (pathOnly == "/bridge/peers" || pathOnly == "/peers")) {
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
        std::string s = resp.str();
        send(clientFd, s.data(), static_cast<int>(s.size()), 0);
        closeSocket(clientFd);
        return;
    }

    // Determine storage directory
    std::string baseStorage = m_storageDir;
    if (baseStorage.empty()) {
        std::error_code ec;
        baseStorage = (std::filesystem::temp_directory_path() / "landrop_bridge_transfers").string();
    }
    std::error_code ec;
    std::filesystem::create_directories(baseStorage, ec);

    if (method == "POST" && (pathOnly == "/bridge/upload" || pathOnly == "/upload")) {
        // Extract filename from query string or header
        std::string rawFilename = filenameHeader;
        if (rawFilename.empty() && !queryString.empty()) {
            size_t fnPos = queryString.find("filename=");
            if (fnPos == std::string::npos) fnPos = queryString.find("name=");
            if (fnPos != std::string::npos) {
                size_t valStart = queryString.find('=', fnPos) + 1;
                size_t valEnd = queryString.find('&', valStart);
                rawFilename = (valEnd == std::string::npos) ? queryString.substr(valStart) : queryString.substr(valStart, valEnd - valStart);
                rawFilename = urlDecode(rawFilename);
            }
        }
        if (rawFilename.empty()) {
            rawFilename = "upload_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".bin";
        }

        // Strict defensive path sanitization
        std::string sanitized = std::filesystem::path(rawFilename).filename().string();
        if (sanitized.empty() || sanitized == "." || sanitized == "..") {
            sanitized = "bridge_upload.bin";
        }

        std::filesystem::path destPath = std::filesystem::path(baseStorage) / sanitized;
        std::ofstream outFile(destPath, std::ios::binary | std::ios::trunc);
        if (!outFile.is_open()) {
            std::string errBody = "{\"error\":\"Failed to open destination file for writing\"}";
            resp << "HTTP/1.1 500 Internal Server Error\r\n"
                 << "Access-Control-Allow-Origin: " << allowOriginStr << "\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Content-Length: " << errBody.size() << "\r\n"
                 << "Connection: close\r\n\r\n"
                 << errBody;
            std::string s = resp.str();
            send(clientFd, s.data(), static_cast<int>(s.size()), 0);
            closeSocket(clientFd);
            return;
        }

        uint64_t bytesWritten = 0;
        if (!bodyPrefix.empty()) {
            size_t toWrite = bodyPrefix.size();
            if (contentLength > 0 && toWrite > contentLength) {
                toWrite = static_cast<size_t>(contentLength);
            }
            outFile.write(bodyPrefix.data(), toWrite);
            bytesWritten += toWrite;
        }

        char streamChunk[65536];
        while (contentLength == 0 || bytesWritten < contentLength) {
            size_t want = sizeof(streamChunk);
            if (contentLength > 0 && (contentLength - bytesWritten) < want) {
                want = static_cast<size_t>(contentLength - bytesWritten);
            }
            int r = recv(clientFd, streamChunk, static_cast<int>(want), 0);
            if (r <= 0) break;
            outFile.write(streamChunk, r);
            bytesWritten += r;
        }
        outFile.close();

        FileChunkHasher hasher;
        auto hashOpt = hasher.computeFullFileHash(destPath.string());
        std::string sha256Hex = hashOpt.has_value() ? *hashOpt : "";

        std::ostringstream jsonOut;
        jsonOut << "{\"status\":\"ok\",\"filename\":\"" << escapeJsonString(sanitized)
                << "\",\"path\":\"" << escapeJsonString(destPath.string())
                << "\",\"bytes\":" << bytesWritten
                << ",\"sha256\":\"" << sha256Hex << "\"}";
        std::string body = jsonOut.str();

        resp << "HTTP/1.1 200 OK\r\n"
             << "Access-Control-Allow-Origin: " << allowOriginStr << "\r\n"
             << "Content-Type: application/json; charset=utf-8\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
        std::string s = resp.str();
        send(clientFd, s.data(), static_cast<int>(s.size()), 0);
        closeSocket(clientFd);
        return;
    }

    if (method == "GET" && (pathOnly == "/bridge/download" || pathOnly == "/download")) {
        // Extract file parameter from query string
        std::string rawFilename;
        if (!queryString.empty()) {
            size_t fnPos = queryString.find("file=");
            if (fnPos == std::string::npos) fnPos = queryString.find("filename=");
            if (fnPos != std::string::npos) {
                size_t valStart = queryString.find('=', fnPos) + 1;
                size_t valEnd = queryString.find('&', valStart);
                rawFilename = (valEnd == std::string::npos) ? queryString.substr(valStart) : queryString.substr(valStart, valEnd - valStart);
                rawFilename = urlDecode(rawFilename);
            }
        }

        std::string sanitized = std::filesystem::path(rawFilename).filename().string();
        if (sanitized.empty() || sanitized == "." || sanitized == "..") {
            std::string errBody = "{\"error\":\"Invalid filename parameter\"}";
            resp << "HTTP/1.1 400 Bad Request\r\n"
                 << "Access-Control-Allow-Origin: " << allowOriginStr << "\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Content-Length: " << errBody.size() << "\r\n"
                 << "Connection: close\r\n\r\n"
                 << errBody;
            std::string s = resp.str();
            send(clientFd, s.data(), static_cast<int>(s.size()), 0);
            closeSocket(clientFd);
            return;
        }

        std::filesystem::path targetFile = std::filesystem::path(baseStorage) / sanitized;
        if (!std::filesystem::exists(targetFile, ec) || !std::filesystem::is_regular_file(targetFile, ec)) {
            std::string errBody = "{\"error\":\"File not found\"}";
            resp << "HTTP/1.1 404 Not Found\r\n"
                 << "Access-Control-Allow-Origin: " << allowOriginStr << "\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Content-Length: " << errBody.size() << "\r\n"
                 << "Connection: close\r\n\r\n"
                 << errBody;
            std::string s = resp.str();
            send(clientFd, s.data(), static_cast<int>(s.size()), 0);
            closeSocket(clientFd);
            return;
        }

        uint64_t fileSize = std::filesystem::file_size(targetFile, ec);
        std::ifstream inFile(targetFile, std::ios::binary);
        if (!inFile.is_open()) {
            std::string errBody = "{\"error\":\"Failed to open file for reading\"}";
            resp << "HTTP/1.1 500 Internal Server Error\r\n"
                 << "Access-Control-Allow-Origin: " << allowOriginStr << "\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Content-Length: " << errBody.size() << "\r\n"
                 << "Connection: close\r\n\r\n"
                 << errBody;
            std::string s = resp.str();
            send(clientFd, s.data(), static_cast<int>(s.size()), 0);
            closeSocket(clientFd);
            return;
        }

        resp << "HTTP/1.1 200 OK\r\n"
             << "Access-Control-Allow-Origin: " << allowOriginStr << "\r\n"
             << "Content-Type: application/octet-stream\r\n"
             << "Content-Length: " << fileSize << "\r\n"
             << "Content-Disposition: attachment; filename=\"" << sanitized << "\"\r\n"
             << "Connection: close\r\n\r\n";
        std::string headerOut = resp.str();
        send(clientFd, headerOut.data(), static_cast<int>(headerOut.size()), 0);

        char sendChunk[65536];
        while (inFile.read(sendChunk, sizeof(sendChunk)) || inFile.gcount() > 0) {
            std::streamsize bytes = inFile.gcount();
            if (bytes <= 0) break;
            send(clientFd, sendChunk, static_cast<int>(bytes), 0);
        }
        inFile.close();
        closeSocket(clientFd);
        return;
    }

    std::string body = "{\"error\":\"Not Found\"}";
    resp << "HTTP/1.1 404 Not Found\r\n"
         << "Access-Control-Allow-Origin: " << allowOriginStr << "\r\n"
         << "Content-Type: application/json\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Connection: close\r\n\r\n"
         << body;
    std::string respStr = resp.str();
    send(clientFd, respStr.data(), static_cast<int>(respStr.size()), 0);
    closeSocket(clientFd);
}

} // namespace landrop
