#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <memory>
#include "landrop/utils/SocketCompat.hpp"

namespace landrop {

struct BridgePeerInfo {
    std::string id;
    std::string name;
    std::string ip;
    uint16_t port{8848};
    std::string deviceType{"desktop"};
};

using BridgePeerProvider = std::function<std::vector<BridgePeerInfo>()>;

class LocalWebBridge {
public:
    explicit LocalWebBridge(uint16_t listenPort = 8849);
    ~LocalWebBridge();

    // Prevent copying
    LocalWebBridge(const LocalWebBridge&) = delete;
    LocalWebBridge& operator=(const LocalWebBridge&) = delete;

    bool start(BridgePeerProvider peerProvider = nullptr);
    void stop();
    bool isRunning() const { return m_running.load(); }
    uint16_t getPort() const { return m_port; }

    static bool isOriginAllowed(const std::string& origin);

    void setStorageDirectory(const std::string& dir);
    std::string getStorageDirectory() const;

private:
    uint16_t m_port{8849};
    std::string m_storageDir;
    std::atomic<bool> m_running{false};
    BridgePeerProvider m_peerProvider;
    std::unique_ptr<std::thread> m_serverThread;
    socket_handle_t m_listenFd{INVALID_SOCKET_HANDLE};

    void serverLoop();
    void handleClient(socket_handle_t clientFd);
};

} // namespace landrop
