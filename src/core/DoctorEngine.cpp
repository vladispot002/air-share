#include "landrop/core/DoctorEngine.hpp"
#include "landrop/core/HttpClient.hpp"
#include "landrop/utils/SocketCompat.hpp"
#include "landrop/utils/CryptoFallback.hpp"
#include "nlohmann/json.hpp"

#include <iostream>
#include <sstream>
#include <random>
#include <cstring>
#include <chrono>
#include <iomanip>

#if !defined(_WIN32)
#include <ifaddrs.h>
#include <net/if.h>
#endif

namespace landrop {

DoctorEngine::DoctorEngine(DoctorOptions options)
    : m_options(std::move(options))
{
    initSocketSubsystem();
}

std::vector<uint8_t> DoctorEngine::buildStunBindingRequest(const std::array<uint8_t, 12>& transactionId) {
    std::vector<uint8_t> req(20, 0);
    // Message Type: 0x0001 (Binding Request)
    req[0] = static_cast<uint8_t>((STUN_BINDING_REQUEST >> 8) & 0xFF);
    req[1] = static_cast<uint8_t>(STUN_BINDING_REQUEST & 0xFF);

    // Message Length: 0x0000 (No attributes)
    req[2] = 0x00;
    req[3] = 0x00;

    // Magic Cookie: 0x2112A442
    req[4] = static_cast<uint8_t>((STUN_MAGIC_COOKIE >> 24) & 0xFF);
    req[5] = static_cast<uint8_t>((STUN_MAGIC_COOKIE >> 16) & 0xFF);
    req[6] = static_cast<uint8_t>((STUN_MAGIC_COOKIE >> 8) & 0xFF);
    req[7] = static_cast<uint8_t>(STUN_MAGIC_COOKIE & 0xFF);

    // Transaction ID: 12 bytes
    std::memcpy(req.data() + 8, transactionId.data(), 12);

    return req;
}

bool DoctorEngine::parseStunBindingResponse(
    const uint8_t* data,
    size_t len,
    const std::array<uint8_t, 12>& expectedTxId,
    std::string& outIp,
    uint16_t& outPort,
    std::string* outError
) {
    if (len < 20) {
        if (outError) *outError = "STUN response packet too short (< 20 bytes)";
        return false;
    }

    uint16_t msgType = (static_cast<uint16_t>(data[0]) << 8) | static_cast<uint16_t>(data[1]);
    uint16_t msgLen = (static_cast<uint16_t>(data[2]) << 8) | static_cast<uint16_t>(data[3]);
    uint32_t cookie = (static_cast<uint32_t>(data[4]) << 24) |
                      (static_cast<uint32_t>(data[5]) << 16) |
                      (static_cast<uint32_t>(data[6]) << 8)  |
                      static_cast<uint32_t>(data[7]);

    if (msgType != STUN_BINDING_RESPONSE) {
        if (outError) *outError = "Unexpected STUN message type: " + std::to_string(msgType);
        return false;
    }

    if (cookie != STUN_MAGIC_COOKIE) {
        if (outError) *outError = "Invalid STUN magic cookie";
        return false;
    }

    if (std::memcmp(data + 8, expectedTxId.data(), 12) != 0) {
        if (outError) *outError = "STUN transaction ID mismatch";
        return false;
    }

    if (static_cast<size_t>(20 + msgLen) > len) {
        if (outError) *outError = "STUN attributes truncated";
        return false;
    }

    size_t offset = 20;
    size_t endOffset = static_cast<size_t>(20 + msgLen);
    bool foundAddress = false;

    while (offset + 4 <= endOffset) {
        uint16_t attrType = (static_cast<uint16_t>(data[offset]) << 8) | static_cast<uint16_t>(data[offset + 1]);
        uint16_t attrLen = (static_cast<uint16_t>(data[offset + 2]) << 8) | static_cast<uint16_t>(data[offset + 3]);
        const uint8_t* val = data + offset + 4;

        if (offset + 4 + attrLen > endOffset) {
            break;
        }

        if (attrType == ATTR_XOR_MAPPED_ADDRESS && attrLen >= 8) {
            uint8_t family = val[1];
            uint16_t xPort = (static_cast<uint16_t>(val[2]) << 8) | static_cast<uint16_t>(val[3]);
            uint16_t port = xPort ^ static_cast<uint16_t>(STUN_MAGIC_COOKIE >> 16);

            if (family == 0x01) { // IPv4
                uint32_t xAddr = (static_cast<uint32_t>(val[4]) << 24) |
                                 (static_cast<uint32_t>(val[5]) << 16) |
                                 (static_cast<uint32_t>(val[6]) << 8)  |
                                 static_cast<uint32_t>(val[7]);
                uint32_t addr = xAddr ^ STUN_MAGIC_COOKIE;

                std::ostringstream ss;
                ss << ((addr >> 24) & 0xFF) << "."
                   << ((addr >> 16) & 0xFF) << "."
                   << ((addr >> 8) & 0xFF) << "."
                   << (addr & 0xFF);

                outIp = ss.str();
                outPort = port;
                foundAddress = true;
                break;
            }
        } else if (attrType == ATTR_MAPPED_ADDRESS && attrLen >= 8 && outIp.empty()) {
            uint8_t family = val[1];
            uint16_t port = (static_cast<uint16_t>(val[2]) << 8) | static_cast<uint16_t>(val[3]);

            if (family == 0x01) { // IPv4
                uint32_t addr = (static_cast<uint32_t>(val[4]) << 24) |
                                (static_cast<uint32_t>(val[5]) << 16) |
                                (static_cast<uint32_t>(val[6]) << 8)  |
                                static_cast<uint32_t>(val[7]);

                std::ostringstream ss;
                ss << ((addr >> 24) & 0xFF) << "."
                   << ((addr >> 16) & 0xFF) << "."
                   << ((addr >> 8) & 0xFF) << "."
                   << (addr & 0xFF);

                outIp = ss.str();
                outPort = port;
                foundAddress = true;
            }
        }

        size_t padding = (4 - (attrLen % 4)) % 4;
        offset += 4 + attrLen + padding;
    }

    if (!foundAddress) {
        if (outError) *outError = "No supported (XOR-)MAPPED-ADDRESS attribute found in STUN response";
        return false;
    }

    return true;
}

std::vector<NetworkInterfaceInfo> DoctorEngine::inspectInterfaces() {
    std::vector<NetworkInterfaceInfo> list;

#if !defined(_WIN32)
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == 0 && ifaddr != nullptr) {
        for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
                continue;
            }

            char host[NI_MAXHOST];
            if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST) == 0) {
                NetworkInterfaceInfo info;
                info.name = ifa->ifa_name ? ifa->ifa_name : "unknown";
                info.ipAddress = host;
                info.isLoopback = (ifa->ifa_flags & IFF_LOOPBACK) != 0;
                info.isMulticastCapable = (ifa->ifa_flags & IFF_MULTICAST) != 0;
                info.isUp = (ifa->ifa_flags & IFF_UP) != 0;
                list.push_back(std::move(info));
            }
        }
        freeifaddrs(ifaddr);
    }
#else
    // Fallback on Windows
    NetworkInterfaceInfo loop;
    loop.name = "loopback";
    loop.ipAddress = "127.0.0.1";
    loop.isLoopback = true;
    loop.isMulticastCapable = false;
    loop.isUp = true;
    list.push_back(loop);
#endif

    if (list.empty()) {
        NetworkInterfaceInfo loop;
        loop.name = "lo";
        loop.ipAddress = "127.0.0.1";
        loop.isLoopback = true;
        loop.isMulticastCapable = true;
        loop.isUp = true;
        list.push_back(loop);
    }

    return list;
}

PortStatus DoctorEngine::checkPort(uint16_t port, const std::string& protocol) {
    PortStatus status;
    status.port = port;
    status.protocol = protocol;

    int type = (protocol == "UDP") ? SOCK_DGRAM : SOCK_STREAM;
    socket_handle_t sock = ::socket(AF_INET, type, 0);
    if (!isValidSocket(sock)) {
        status.available = false;
        status.details = "Socket creation failed";
        return status;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
        status.available = true;
        status.details = "Available for binding";
    } else {
        status.available = false;
        status.details = "In use or restricted (EADDRINUSE)";
    }

    closeSocket(sock);
    return status;
}

SignalingCheckResult DoctorEngine::probeSignaling(const std::string& url) {
    SignalingCheckResult result;
    result.url = url;

    auto tStart = std::chrono::steady_clock::now();
    HttpClient http;
    HttpResponse resp = http.get(url, 3);
    auto tEnd = std::chrono::steady_clock::now();

    result.latencyMs = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();
    result.statusCode = resp.statusCode;

    if (resp.isOk()) {
        result.reachable = true;
        result.details = "Operational (HTTP " + std::to_string(resp.statusCode) + ")";
    } else {
        result.reachable = false;
        if (!resp.errorMessage.empty()) {
            result.details = resp.errorMessage;
        } else if (resp.statusCode > 0) {
            result.details = "HTTP error " + std::to_string(resp.statusCode);
        } else {
            result.details = "Connection refused or unreachable";
        }
    }

    return result;
}

StunCheckResult DoctorEngine::probeStun(const std::string& hostAndPort, int timeoutMs) {
    StunCheckResult result;
    result.stunServer = hostAndPort;

    std::string host = hostAndPort;
    std::string portStr = "3478";
    auto colonPos = hostAndPort.rfind(':');
    if (colonPos != std::string::npos) {
        host = hostAndPort.substr(0, colonPos);
        portStr = hostAndPort.substr(colonPos + 1);
    }

    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || res == nullptr) {
        result.reachable = false;
        result.details = "DNS resolution failed for STUN server " + host;
        return result;
    }

    socket_handle_t sock = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (!isValidSocket(sock)) {
        freeaddrinfo(res);
        result.reachable = false;
        result.details = "Failed to allocate UDP socket";
        return result;
    }

    int timeoutSec = (timeoutMs > 0 ? (timeoutMs + 999) / 1000 : 3);
    setSocketTimeout(sock, timeoutSec);

    std::array<uint8_t, 12> txId{};
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0, 255);
    for (size_t i = 0; i < 12; ++i) {
        txId[i] = static_cast<uint8_t>(dis(gen));
    }

    std::vector<uint8_t> req = buildStunBindingRequest(txId);

    auto tStart = std::chrono::steady_clock::now();
    ssize_t sent = ::sendto(sock, reinterpret_cast<const char*>(req.data()), req.size(), 0, res->ai_addr, res->ai_addrlen);
    if (sent < 0) {
        closeSocket(sock);
        freeaddrinfo(res);
        result.reachable = false;
        result.details = "sendto failed to STUN server";
        return result;
    }

    uint8_t buf[1024];
    ssize_t bytesRecv = ::recvfrom(sock, reinterpret_cast<char*>(buf), sizeof(buf), 0, nullptr, nullptr);
    auto tEnd = std::chrono::steady_clock::now();

    closeSocket(sock);
    freeaddrinfo(res);

    result.latencyMs = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();

    if (bytesRecv <= 0) {
        result.reachable = false;
        result.details = "Timed out waiting for STUN response (" + std::to_string(timeoutMs) + " ms)";
        return result;
    }

    std::string err;
    if (parseStunBindingResponse(buf, static_cast<size_t>(bytesRecv), txId, result.reflectedIp, result.reflectedPort, &err)) {
        result.reachable = true;
        result.details = "Public reflexive endpoint: " + result.reflectedIp + ":" + std::to_string(result.reflectedPort);
    } else {
        result.reachable = false;
        result.details = "STUN parse error: " + err;
    }

    return result;
}

DoctorReport DoctorEngine::runAllDiagnostics() {
    DoctorReport report;
    report.timestamp = std::chrono::system_clock::now();

    report.interfaces = inspectInterfaces();
    report.ports.push_back(checkPort(m_options.tcpPort, "TCP"));
    report.ports.push_back(checkPort(m_options.udpPort, "UDP"));

    if (m_options.checkSignaling) {
        report.signaling = probeSignaling(m_options.signalingUrl);
    }

    if (m_options.checkStun) {
        report.stun = probeStun(m_options.stunServer, m_options.timeoutMs);
    }

    // Determine overall diagnostic health status
    bool hasValidInterface = false;
    for (const auto& iface : report.interfaces) {
        if (iface.isUp && !iface.isLoopback) {
            hasValidInterface = true;
            break;
        }
    }

    bool portsAvailable = true;
    for (const auto& p : report.ports) {
        if (!p.available) {
            portsAvailable = false;
            break;
        }
    }

    if (!hasValidInterface && report.interfaces.empty()) {
        report.overallStatus = DiagnosticStatus::Fail;
    } else if (!portsAvailable) {
        report.overallStatus = DiagnosticStatus::Warning;
    } else if (m_options.checkSignaling && !report.signaling.reachable) {
        report.overallStatus = DiagnosticStatus::Warning;
    } else if (m_options.checkStun && !report.stun.reachable) {
        report.overallStatus = DiagnosticStatus::Warning;
    } else {
        report.overallStatus = DiagnosticStatus::Pass;
    }

    return report;
}

std::string DoctorReport::toFormattedText(bool useColor) const {
    const std::string cReset  = useColor ? "\033[0m" : "";
    const std::string cBold   = useColor ? "\033[1m" : "";
    const std::string cGreen  = useColor ? "\033[32m" : "";
    const std::string cYellow = useColor ? "\033[33m" : "";
    const std::string cRed    = useColor ? "\033[31m" : "";
    const std::string cCyan   = useColor ? "\033[36m" : "";

    auto passTag = cGreen + "[PASS]" + cReset;
    auto warnTag = cYellow + "[WARN]" + cReset;
    auto failTag = cRed + "[FAIL]" + cReset;

    std::ostringstream ss;
    ss << cBold << "========================================================\n"
       << "  LAN-Drop System & Network Diagnostics (Doctor)\n"
       << "========================================================\n" << cReset;

    // 1. Interfaces
    ss << "\n" << cBold << "[1] Network Interfaces:" << cReset << "\n";
    if (interfaces.empty()) {
        ss << "  " << failTag << " No active network interfaces detected.\n";
    } else {
        for (const auto& iface : interfaces) {
            std::string tags;
            if (iface.isLoopback) tags += "[loopback] ";
            if (iface.isMulticastCapable) tags += "[multicast] ";
            if (iface.isUp) tags += "[up] ";

            ss << "  " << passTag << " " << cCyan << iface.name << cReset << ": "
               << iface.ipAddress << " " << tags << "\n";
        }
    }

    // 2. Ports
    ss << "\n" << cBold << "[2] Transport & Discovery Ports:" << cReset << "\n";
    for (const auto& port : ports) {
        if (port.available) {
            ss << "  " << passTag << " " << port.protocol << " port " << port.port << " is " << port.details << "\n";
        } else {
            ss << "  " << warnTag << " " << port.protocol << " port " << port.port << " is " << port.details << "\n";
        }
    }

    // 3. Signaling
    if (!signaling.url.empty()) {
        ss << "\n" << cBold << "[3] Signaling Server Reachability:" << cReset << "\n";
        if (signaling.reachable) {
            ss << "  " << passTag << " " << signaling.url << " -> " << signaling.details
               << " (RTT " << signaling.latencyMs << " ms)\n";
        } else {
            ss << "  " << warnTag << " " << signaling.url << " -> " << signaling.details << "\n"
               << "         " << cYellow << "Notice: Local LAN direct transfers operate 100% offline without signaling." << cReset << "\n";
        }
    }

    // 4. STUN Probe
    if (!stun.stunServer.empty()) {
        ss << "\n" << cBold << "[4] NAT Reflexive Probe (RFC 5389 STUN):" << cReset << "\n";
        if (stun.reachable) {
            ss << "  " << passTag << " " << stun.stunServer << " -> " << stun.details
               << " (RTT " << stun.latencyMs << " ms)\n";
        } else {
            ss << "  " << warnTag << " " << stun.stunServer << " -> " << stun.details << "\n"
               << "         " << cYellow << "Notice: STUN is only required for global P2P NAT traversal across the internet." << cReset << "\n";
        }
    }

    // 5. Assessment
    ss << "\n" << cBold << "--------------------------------------------------------\n"
       << "  Overall Diagnostic Assessment: ";
    if (overallStatus == DiagnosticStatus::Pass) {
        ss << cGreen << "HEALTHY - ALL SYSTEMS GO" << cReset << "\n";
    } else if (overallStatus == DiagnosticStatus::Warning) {
        ss << cYellow << "DEGRADED / LAN-ONLY (Offline transfer ready)" << cReset << "\n";
    } else {
        ss << cRed << "CRITICAL (Networking unavailable)" << cReset << "\n";
    }
    ss << cBold << "========================================================\n" << cReset;

    return ss.str();
}

std::string DoctorReport::toJson() const {
    nlohmann::json j;
    j["overall_status"] = (overallStatus == DiagnosticStatus::Pass ? "PASS" : (overallStatus == DiagnosticStatus::Warning ? "WARNING" : "FAIL"));

    nlohmann::json ifacesArray = nlohmann::json::array();
    for (const auto& iface : interfaces) {
        ifacesArray.push_back({
            {"name", iface.name},
            {"ip", iface.ipAddress},
            {"loopback", iface.isLoopback},
            {"multicast", iface.isMulticastCapable},
            {"up", iface.isUp}
        });
    }
    j["interfaces"] = ifacesArray;

    nlohmann::json portsArray = nlohmann::json::array();
    for (const auto& p : ports) {
        portsArray.push_back({
            {"port", p.port},
            {"protocol", p.protocol},
            {"available", p.available},
            {"details", p.details}
        });
    }
    j["ports"] = portsArray;

    j["signaling"] = {
        {"url", signaling.url},
        {"reachable", signaling.reachable},
        {"status_code", signaling.statusCode},
        {"latency_ms", signaling.latencyMs},
        {"details", signaling.details}
    };

    j["stun"] = {
        {"server", stun.stunServer},
        {"reachable", stun.reachable},
        {"reflected_ip", stun.reflectedIp},
        {"reflected_port", stun.reflectedPort},
        {"latency_ms", stun.latencyMs},
        {"details", stun.details}
    };

    return j.dump(2);
}

} // namespace landrop
