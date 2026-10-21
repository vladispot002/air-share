#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "landrop/utils/Types.hpp"
#include "landrop/core/ChunkBuffer.hpp"
#include "landrop/core/FileChunkHasher.hpp"
#include "landrop/core/SharedFolderManager.hpp"
#include "landrop/core/MulticastBeacon.hpp"
#include "landrop/peers/NetworkPeer.hpp"
#include "landrop/peers/LocalPeer.hpp"
#include "landrop/peers/CloudPeer.hpp"
#include "landrop/tasks/TransferTask.hpp"
#include "landrop/tasks/P2PTransferTask.hpp"
#include "landrop/tasks/HttpWebTransferTask.hpp"
#include "landrop/mobile/NfcPairingSession.hpp"
#include "landrop/mobile/AirDropProtocolBridge.hpp"
#include "landrop/mobile/LandropMobileBridge.h"
#include "landrop/ui/IView.hpp"
#include "landrop/ui/DiscoveryPresenter.hpp"
#include "landrop/ui/TransferPresenter.hpp"
#include "landrop/ui/StoragePresenter.hpp"
#include "landrop/ui/SettingsPresenter.hpp"

using namespace landrop;

static size_t g_passedTests = 0;
static size_t g_totalTests = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        g_totalTests++; \
        if (!(cond)) { \
            std::cerr << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } else { \
            g_passedTests++; \
        } \
    } while (0)

static bool testLocalPeer() {
    LocalPeer peer("peer-01", "MacBook Pro", "192.168.1.42", 8848, "macOS", "Apple M3", "255.255.255.0", true);
    TEST_ASSERT(peer.getId() == "peer-01", "Peer ID matches");
    TEST_ASSERT(peer.getDisplayName() == "MacBook Pro", "Display name matches");
    TEST_ASSERT(peer.getIpAddress() == "192.168.1.42", "IP matches");
    TEST_ASSERT(peer.getPort() == 8848, "Port matches");
    TEST_ASSERT(peer.getOsName() == "macOS", "OS matches");
    TEST_ASSERT(peer.getDeviceModel() == "Apple M3", "Model matches");
    TEST_ASSERT(peer.isDirectWifi() == true, "Direct Wi-Fi flag matches");
    TEST_ASSERT(peer.getPeerType() == "LocalPeer", "Peer type matches");
    TEST_ASSERT(peer.getSignalStrengthRssi() == -55, "Default RSSI");
    peer.setSignalStrengthRssi(-40);
    TEST_ASSERT(peer.getSignalStrengthRssi() == -40, "Updated RSSI");

    std::ostringstream oss;
    oss << peer;
    TEST_ASSERT(!oss.str().empty(), "Stream operator generates string");
    return true;
}

static bool testCloudPeer() {
    CloudPeer cloud("cloud-01", "RelayNode", "https://relay.landrop.io", 443, "Cloud Relay", "secret-token-123");
    TEST_ASSERT(cloud.getId() == "cloud-01", "Cloud ID matches");
    TEST_ASSERT(cloud.getCloudProvider() == "Cloud Relay", "Provider matches");
    TEST_ASSERT(cloud.getEndpointUrl() == "https://relay.landrop.io", "Relay URL matches");
    TEST_ASSERT(cloud.getAccessToken() == "secret-token-123", "Auth token matches");
    TEST_ASSERT(cloud.getPeerType() == "CloudPeer", "Peer type is Cloud");
    TEST_ASSERT(cloud.isAutoSyncEnabled() == true, "Auto sync enabled by default");

    cloud.allocateStorage(1024 * 1024);
    TEST_ASSERT(cloud.getAvailableStorageBytes() == (10ULL * 1024 * 1024 * 1024 - 1024 * 1024), "Quota reduced properly");
    return true;
}

static bool testPolymorphicFilter() {
    std::vector<std::shared_ptr<NetworkPeer>> peers;
    peers.push_back(std::make_shared<LocalPeer>("p1", "MacBook Pro", "192.168.1.42", 8848, "macOS", "Laptop"));
    peers.push_back(std::make_shared<LocalPeer>("p2", "ThinkPad X1", "192.168.1.67", 8848, "Windows", "PC"));
    peers.push_back(std::make_shared<CloudPeer>("p3", "Cloud Relay", "https://relay.landrop.io", 443, "Cloud"));

    auto localList = NetworkPeer::filterPeers<LocalPeer>(peers);
    TEST_ASSERT(localList.size() == 2, "Filtered 2 local peers");

    auto cloudList = NetworkPeer::filterPeers<CloudPeer>(peers);
    TEST_ASSERT(cloudList.size() == 1, "Filtered 1 cloud peer");
    return true;
}

static bool testChunkBuffer() {
    ChunkBuffer<std::string> buf(3, "TestTag");
    TEST_ASSERT(buf.getTag() == "TestTag", "Tag matches");
    TEST_ASSERT(buf.capacity() == 3, "Capacity matches");
    TEST_ASSERT(buf.isEmpty(), "Buffer starts empty");

    TEST_ASSERT(buf.push("item1"), "Push item 1");
    TEST_ASSERT(buf.push("item2"), "Push item 2");
    TEST_ASSERT(buf.push("item3"), "Push item 3");
    TEST_ASSERT(buf.size() == 3, "Size is 3");
    TEST_ASSERT(!buf.push("item4"), "Buffer full rejection");

    TEST_ASSERT(buf[0] == "item1", "Index 0 matches");
    TEST_ASSERT(buf[1] == "item2", "Index 1 matches");
    TEST_ASSERT(buf[2] == "item3", "Index 2 matches");
    return true;
}

static bool testFileChunkHasher() {
    FileChunkHasher hasher;
    std::vector<uint8_t> data = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd'};
    std::string hash1 = hasher.computeChunkHash(data);
    TEST_ASSERT(hash1.length() == 64, "SHA-256 hash length is 64 hex characters");

    std::string hash2 = hasher.computeChunkHash(data);
    TEST_ASSERT(hash1 == hash2, "Deterministic hash computation");

    std::string testPath = "./unit_test_temp.tmp";
    {
        std::ofstream out(testPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
    auto fullHashOpt = hasher.computeFullFileHash(testPath);
    TEST_ASSERT(fullHashOpt.has_value(), "Full file hash computed");
    TEST_ASSERT(fullHashOpt.value() == hash1, "File hash matches in-memory chunk hash");
    std::filesystem::remove(testPath);
    return true;
}

static bool testMulticastBeacon() {
    MulticastBeacon beacon("NodeAlpha", "uuid-alpha-123", 8848);
    std::string serialized = beacon.serializePayload();
    TEST_ASSERT(serialized.find("NodeAlpha") != std::string::npos, "Payload contains name");
    TEST_ASSERT(serialized.find("uuid-alpha-123") != std::string::npos, "Payload contains UUID");
    TEST_ASSERT(serialized.find("8848") != std::string::npos, "Payload contains port");

    std::string parsedName, parsedUuid;
    uint16_t parsedPort = 0;
    bool ok = MulticastBeacon::parsePayload(serialized, parsedName, parsedUuid, parsedPort);
    TEST_ASSERT(ok, "Payload parsed successfully");
    TEST_ASSERT(parsedName == "NodeAlpha", "Parsed name matches");
    TEST_ASSERT(parsedUuid == "uuid-alpha-123", "Parsed UUID matches");
    TEST_ASSERT(parsedPort == 8848, "Parsed port matches");

    beacon.simulateReceiveBeacon(serialized);
    TEST_ASSERT(beacon.getBeaconsReceived() == 1, "Beacon received count incremented");
    return true;
}

static bool testP2PTransferTask() {
    P2PTransferTask task("T-0042", "project-assets.zip", 248ULL * 1024 * 1024, TransferDirection::Upload, "peer-target");
    TEST_ASSERT(task.getTaskId() == "T-0042", "Task ID matches");
    TEST_ASSERT(task.getFileName() == "project-assets.zip", "File name matches");
    TEST_ASSERT(task.getFileSize() == 248ULL * 1024 * 1024, "File size matches");
    TEST_ASSERT(task.getDirection() == TransferDirection::Upload, "Direction matches");
    TEST_ASSERT(task.getTaskType() == "P2PTransferTask", "Task type is P2P");

    TEST_ASSERT(task.initialize(), "Task initialized");
    TEST_ASSERT(task.getState() == TransferState::Connecting, "State is Connecting");

    std::vector<uint8_t> chunk(1024 * 1024, 0x11);
    task.processChunk(0, chunk);
    TEST_ASSERT(task.getState() == TransferState::Transferring, "State is Transferring");
    TEST_ASSERT(task.getBytesTransferred() == 1024 * 1024, "Bytes transferred updated");
    TEST_ASSERT(task.getProgressPercentage() > 0.0, "Progress greater than 0");

    task.pause();
    TEST_ASSERT(task.getState() == TransferState::Paused, "State is Paused");

    task.resume();
    TEST_ASSERT(task.getState() == TransferState::Transferring, "State resumed to Transferring");

    task.cancel();
    TEST_ASSERT(task.getState() == TransferState::Cancelled, "State is Cancelled");
    return true;
}

static bool testHttpWebTransferTask() {
    HttpWebTransferTask task("T-0099", "shared_presentation.pdf", 15 * 1024 * 1024, TransferDirection::Download, 8080, "qr-auth-token-xyz");
    TEST_ASSERT(task.getHttpPort() == 8080, "Web port matches");
    TEST_ASSERT(task.getWebToken() == "qr-auth-token-xyz", "QR token matches");
    TEST_ASSERT(task.getTaskType() == "HttpWebTransferTask", "Task type is HttpWeb");
    TEST_ASSERT(task.initialize(), "Initialized");
    return true;
}

static bool testSharedFolderManager() {
    std::string testDir = "./unit_test_shared";
    std::filesystem::remove_all(testDir);

    SharedFolderManager manager(testDir);
    TEST_ASSERT(manager.getRootPath() == testDir, "Root path matches");

    std::string file1 = testDir + "/sample.txt";
    {
        std::ofstream out(file1);
        out << "Hello Shared Storage Unit Test";
    }
    manager.scanDirectory();
    TEST_ASSERT(manager.getSharedFileCount() == 1, "File count is 1");
    TEST_ASSERT(manager.getTotalSharedBytes() > 0, "Total bytes > 0");

    TEST_ASSERT(!manager.hasPasswordProtection(), "Starts without password");
    manager.setAccessPassword("9977");
    TEST_ASSERT(manager.hasPasswordProtection(), "Password protection active");
    TEST_ASSERT(manager.verifyAccessPassword("9977"), "Correct password verified");
    TEST_ASSERT(!manager.verifyAccessPassword("0000"), "Wrong password rejected");

    manager.setAccessPassword("");
    TEST_ASSERT(!manager.hasPasswordProtection(), "Password cleared");

    std::filesystem::remove_all(testDir);
    return true;
}

static bool testNfcPairingSession() {
    mobile::NfcPairingSession sender("sender-uuid", "Phone Alpha");
    std::string payload = sender.generateNdefPayload("192.168.1.180", 8848);
    TEST_ASSERT(payload.find("LANDROP:NFC:v1") != std::string::npos, "Payload has header");
    TEST_ASSERT(payload.find("Phone Alpha") != std::string::npos, "Payload has device name");

    mobile::NfcPairingSession receiver("receiver-uuid", "Desktop Beta");
    auto parsedOpt = receiver.parseNdefPayload(payload);
    TEST_ASSERT(parsedOpt.has_value(), "Payload successfully parsed");
    TEST_ASSERT(parsedOpt->deviceName == "Phone Alpha", "Parsed device name matches");
    TEST_ASSERT(parsedOpt->ipAddress == "192.168.1.180", "Parsed IP matches");
    TEST_ASSERT(parsedOpt->port == 8848, "Parsed port matches");
    TEST_ASSERT(!parsedOpt->sessionPin.empty(), "Session PIN generated");
    TEST_ASSERT(!parsedOpt->ephemeralPublicKey.empty(), "Ephemeral key generated");

    auto invalidOpt = receiver.parseNdefPayload("CORRUPTED_PAYLOAD_STRING");
    TEST_ASSERT(!invalidOpt.has_value(), "Corrupted payload rejected");
    return true;
}

static bool testAirDropBridge() {
    mobile::AirDropProtocolBridge airdrop("macbook-uuid", "MacBook Air");
    airdrop.startAdvertising(8770);

    std::string record = airdrop.generateBonjourRecord();
    TEST_ASSERT(record.find("_airdrop._tcp.local") != std::string::npos, "Bonjour service type correct");
    TEST_ASSERT(record.find("MacBook Air") != std::string::npos, "Bonjour contains device name");

    std::string disc = airdrop.buildDiscoverResponse();
    TEST_ASSERT(disc.find("200 OK") != std::string::npos, "Discover response is HTTP 200");
    TEST_ASSERT(disc.find("MacBook Air") != std::string::npos, "Discover response contains name");

    mobile::AirDropAsset asset{"contract.pdf", 524288, "application/pdf", "sha256-hash-mock"};
    std::string askReq = airdrop.buildAskRequest(asset);
    TEST_ASSERT(askReq.find("POST /Discover HTTP/1.1") != std::string::npos, "Ask payload matches format");
    TEST_ASSERT(askReq.find("contract.pdf") != std::string::npos, "Ask payload contains file name");

    mobile::AirDropAsset incoming;
    bool askHandled = airdrop.handleIncomingAsk(askReq, incoming);
    TEST_ASSERT(askHandled, "Ask request parsed correctly");
    TEST_ASSERT(incoming.fileName == "contract.pdf", "Incoming file name matches");
    TEST_ASSERT(incoming.fileSize == 524288, "Incoming file size matches");
    return true;
}

static bool testMobileFfiBridge() {
    LandropHandle handle = landrop_mobile_create("FFI-Device", "ffi-uuid-99");
    TEST_ASSERT(handle != nullptr, "Handle created");

    landrop_mobile_start_discovery(handle, 8848);
    int count = landrop_mobile_get_peer_count(handle);
    TEST_ASSERT(count > 0, "Discovered peers via FFI");

    char name[64], ip[32];
    uint16_t port = 0;
    int res = landrop_mobile_get_peer_info(handle, 0, name, sizeof(name), ip, sizeof(ip), &port);
    TEST_ASSERT(res == 0, "Peer info retrieved successfully");
    TEST_ASSERT(std::string(name).length() > 0, "Peer name not empty");
    TEST_ASSERT(std::string(ip).length() > 0, "Peer IP not empty");

    char nfcOut[256];
    landrop_mobile_nfc_generate_payload(handle, "192.168.1.99", 8848, nfcOut, sizeof(nfcOut));
    TEST_ASSERT(std::string(nfcOut).find("LANDROP:NFC:v1") != std::string::npos, "NFC payload generated via FFI");

    landrop_mobile_destroy(handle);
    return true;
}

class MockView : public ui::IDiscoveryView,
                 public ui::ITransferView,
                 public ui::IStorageView,
                 public ui::ISettingsView {
public:
    std::vector<ui::DeviceViewModel> devices;
    std::vector<ui::TransferViewModel> transfers;
    std::string notification;
    bool isScanning{false};
    uint16_t loadedPort{0};

    void updateDeviceList(const std::vector<ui::DeviceViewModel>& d) override { devices = d; }
    void setScanningStatus(bool scanning) override { isScanning = scanning; }
    void showNfcPairResult(bool, const std::string&) override {}
    void showNotification(const std::string& n) override { notification = n; }

    void updateTransferTable(const std::vector<ui::TransferViewModel>& t) override { transfers = t; }
    void updateSelectedTransferDetails(const ui::TransferViewModel&) override {}
    void showTransferError(const std::string&, const std::string&) override {}

    void setSharedPath(const std::string&) override {}
    void updateFileList(const std::vector<FileMetadata>&) override {}
    void setStorageStats(uint64_t, size_t) override {}
    void setPasswordProtectionStatus(bool) override {}

    void loadSettings(uint16_t port, uint32_t, bool, bool) override { loadedPort = port; }
    void appendDiagnosticLog(const std::string&) override {}
    void showSaveSuccess() override {}
};

static bool testPresenters() {
    MockView mock;

    ui::DiscoveryPresenter disc(&mock);
    disc.initialize();
    TEST_ASSERT(mock.devices.size() == 4, "Discovery presenter initialized with 4 devices");
    disc.onToggleScanClicked(false);
    TEST_ASSERT(!mock.isScanning, "Scan toggled off");

    ui::TransferPresenter trans(&mock);
    trans.initialize();
    TEST_ASSERT(mock.transfers.size() == 6, "Transfer presenter initialized with 6 tasks");
    trans.onPauseTransferClicked("T-0042");
    bool foundPaused = false;
    for (const auto& task : trans.getTasks()) {
        if (task && task->getTaskId() == "T-0042") {
            foundPaused = (task->getState() == TransferState::Paused);
            break;
        }
    }
    TEST_ASSERT(foundPaused, "T-0042 paused");

    ui::StoragePresenter stor(&mock);
    stor.initialize();

    ui::SettingsPresenter sett(&mock);
    sett.initialize();
    TEST_ASSERT(mock.loadedPort == 8848, "Settings loaded port 8848");
    sett.onPortChanged(9090);
    return true;
}

static bool testReceiverSecurityValidation() {
    TEST_ASSERT(!isSafeFileName("../../escaped.txt"), "Rejects parent relative traversal");
    TEST_ASSERT(!isSafeFileName("../escaped.txt"), "Rejects single parent traversal");
    TEST_ASSERT(!isSafeFileName("sub/../../escaped.txt"), "Rejects nested traversal");
    TEST_ASSERT(!isSafeFileName("/tmp/escaped.txt"), "Rejects root-based absolute path");
    TEST_ASSERT(!isSafeFileName("/etc/passwd"), "Rejects system absolute path");
    TEST_ASSERT(!isSafeFileName("C:\\Windows\\system32"), "Rejects backslash paths");
    TEST_ASSERT(!isSafeFileName(""), "Rejects empty filename");
    TEST_ASSERT(!isSafeFileName("."), "Rejects dot");
    TEST_ASSERT(!isSafeFileName(".."), "Rejects double dot");
    TEST_ASSERT(!isSafeFileName(std::string("file\0evil.sh", 12)), "Rejects null byte");
    TEST_ASSERT(!isSafeFileName(std::string(256, 'a')), "Rejects name length > 255");
    TEST_ASSERT(isSafeFileName("valid_report.pdf"), "Accepts valid filename");
    TEST_ASSERT(isSafeFileName("my-image.tar.gz"), "Accepts multi-dot extension");
    TEST_ASSERT(isSafeFileName("document (final).docx"), "Accepts filename with spaces and parentheses");

    constexpr uint32_t kMaxChunk = 4 * 1024 * 1024;
    uint64_t fileSize = 1000;
    uint64_t totalReceived = 0;

    TEST_ASSERT(!isValidChunkSize(0, fileSize, totalReceived, kMaxChunk), "Rejects 0 chunk size");
    TEST_ASSERT(!isValidChunkSize(0xFFFFFFFF, fileSize, totalReceived, kMaxChunk), "Rejects uint32_max chunk");
    TEST_ASSERT(!isValidChunkSize(8 * 1024 * 1024, fileSize, totalReceived, kMaxChunk), "Rejects >4MB chunk");
    TEST_ASSERT(!isValidChunkSize(1500, fileSize, totalReceived, kMaxChunk), "Rejects chunk exceeding remaining file size");
    TEST_ASSERT(isValidChunkSize(500, fileSize, totalReceived, kMaxChunk), "Accepts valid 500B chunk within bounds");
    TEST_ASSERT(isValidChunkSize(500, fileSize, 500, kMaxChunk), "Accepts exact remainder chunk");
    TEST_ASSERT(!isValidChunkSize(501, fileSize, 500, kMaxChunk), "Rejects chunk exceeding exact remainder");
    return true;
}

int main() {
    std::cout << "==========================================\n";
    std::cout << "  LAN-Drop: Comprehensive Unit Test Suite \n";
    std::cout << "==========================================\n\n";

    struct TestCase {
        std::string name;
        bool (*func)();
    };

    std::vector<TestCase> cases = {
        {"LocalPeer & Inheritance", testLocalPeer},
        {"CloudPeer & Quota Management", testCloudPeer},
        {"Static Polymorphic Filter (filterPeers<T>)", testPolymorphicFilter},
        {"Template ChunkBuffer<T>", testChunkBuffer},
        {"FileChunkHasher SHA-256", testFileChunkHasher},
        {"MulticastBeacon Protocol", testMulticastBeacon},
        {"P2PTransferTask Lifecycle", testP2PTransferTask},
        {"HttpWebTransferTask Properties", testHttpWebTransferTask},
        {"SharedFolderManager & Storage Security", testSharedFolderManager},
        {"NfcPairingSession NDEF Protocol", testNfcPairingSession},
        {"AirDropProtocolBridge Protocol", testAirDropBridge},
        {"LandropMobileBridge C-ABI FFI", testMobileFfiBridge},
        {"MVP Presenters & View Integration", testPresenters},
        {"Receiver Security & Boundary Validation", testReceiverSecurityValidation}
    };

    size_t passedCases = 0;
    for (const auto& tc : cases) {
        std::cout << "[RUN] " << tc.name << "... ";
        if (tc.func()) {
            std::cout << "PASSED\n";
            passedCases++;
        } else {
            std::cout << "FAILED\n";
        }
    }

    std::cout << "\n==========================================\n";
    std::cout << "Summary: " << passedCases << "/" << cases.size() << " test suites passed ("
              << g_passedTests << "/" << g_totalTests << " assertions passed).\n";
    std::cout << "==========================================\n";

    return (passedCases == cases.size()) ? 0 : 1;
}
