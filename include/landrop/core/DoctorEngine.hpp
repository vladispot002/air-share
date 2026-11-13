#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <array>
#include <chrono>

namespace landrop {

struct NetworkInterfaceInfo {
    std::string name;
    std::string ipAddress;
    bool isLoopback{false};
    bool isMulticastCapable{false};
    bool isUp{false};
};

struct PortStatus {
    uint16_t port{0};
    std::string protocol{"TCP"}; // "TCP" or "UDP"
    bool available{false};
    std::string details;
};

struct SignalingCheckResult {
    bool reachable{false};
    int statusCode{0};
    int64_t latencyMs{0};
    std::string url;
    std::string details;
};

struct StunCheckResult {
    bool reachable{false};
    std::string stunServer;
    std::string reflectedIp;
    uint16_t reflectedPort{0};
    int64_t latencyMs{0};
    std::string details;
};

enum class DiagnosticStatus {
    Pass,
    Warning,
    Fail
};

struct DoctorReport {
    DiagnosticStatus overallStatus{DiagnosticStatus::Pass};
    std::vector<NetworkInterfaceInfo> interfaces;
    std::vector<PortStatus> ports;
    SignalingCheckResult signaling;
    StunCheckResult stun;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};

    std::string toFormattedText(bool useColor = true) const;
    std::string toJson() const;
};

struct DoctorOptions {
    std::string stunServer{"stun.l.google.com:19302"};
    std::string signalingUrl{"https://landrop-server.duckdns.org/healthz"};
    uint16_t tcpPort{8848};
    uint16_t udpPort{52637};
    int timeoutMs{2500};
    bool checkSignaling{true};
    bool checkStun{true};
};

class DoctorEngine {
public:
    explicit DoctorEngine(DoctorOptions options = DoctorOptions{});
    ~DoctorEngine() = default;

    DoctorReport runAllDiagnostics();

    // Individual diagnostic components
    std::vector<NetworkInterfaceInfo> inspectInterfaces();
    PortStatus checkPort(uint16_t port, const std::string& protocol = "TCP");
    SignalingCheckResult probeSignaling(const std::string& url);
    StunCheckResult probeStun(const std::string& hostAndPort, int timeoutMs = 2500);

    // RFC 5389 STUN protocol encoding/decoding primitives
    static constexpr uint16_t STUN_BINDING_REQUEST = 0x0001;
    static constexpr uint16_t STUN_BINDING_RESPONSE = 0x0101;
    static constexpr uint32_t STUN_MAGIC_COOKIE = 0x2112A442;
    static constexpr uint16_t ATTR_MAPPED_ADDRESS = 0x0001;
    static constexpr uint16_t ATTR_XOR_MAPPED_ADDRESS = 0x0020;

    static std::vector<uint8_t> buildStunBindingRequest(const std::array<uint8_t, 12>& transactionId);
    static bool parseStunBindingResponse(
        const uint8_t* data,
        size_t len,
        const std::array<uint8_t, 12>& expectedTxId,
        std::string& outIp,
        uint16_t& outPort,
        std::string* outError = nullptr
    );

    const DoctorOptions& getOptions() const { return m_options; }
    void setOptions(const DoctorOptions& opts) { m_options = opts; }

private:
    DoctorOptions m_options;
};

} // namespace landrop
