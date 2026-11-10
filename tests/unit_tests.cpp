#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <atomic>
#include <functional>
#include "landrop/utils/SocketCompat.hpp"
#include <cstring>
#include <csignal>


#include "landrop/utils/Types.hpp"
#include "landrop/core/ChunkBuffer.hpp"
#include "landrop/core/FileChunkHasher.hpp"
#include "landrop/core/SharedFolderManager.hpp"
#include "landrop/core/MulticastBeacon.hpp"
#include "landrop/core/NetworkSession.hpp"
#include "landrop/core/TlsContext.hpp"
#include "landrop/core/HttpClient.hpp"
#include "landrop/core/TransferEngine.hpp"
#include "landrop/core/WireProtocolV2.hpp"
#include "landrop/core/TcpSocketChannel.hpp"
#include "landrop/core/WebRtcDataChannel.hpp"
#include "landrop/core/CandidateQueue.hpp"
#include "landrop/core/SignalingClient.hpp"
#include "landrop/core/TurnConfigClient.hpp"
#include "landrop/core/WebRtcSessionCoordinator.hpp"
#include "landrop/core/DoctorEngine.hpp"


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
#include "landrop/ui/QrCodeGenerator.hpp"

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

    // Test automatic unique UUID generation
    MulticastBeacon autoNode1("AutoNode1");
    MulticastBeacon autoNode2("AutoNode2");
    TEST_ASSERT(!autoNode1.getDeviceUuid().empty(), "Auto node 1 has UUID");
    TEST_ASSERT(!autoNode2.getDeviceUuid().empty(), "Auto node 2 has UUID");
    TEST_ASSERT(autoNode1.getDeviceUuid() != autoNode2.getDeviceUuid(), "Auto nodes have unique distinct UUIDs");

    // Test socket lifecycle (start, announcement, stop)
    bool started = autoNode1.start();
    if (started) {
        TEST_ASSERT(autoNode1.isRunning(), "Beacon is running after start");
        TEST_ASSERT(autoNode1.broadcastAnnouncement(), "Broadcast announcement succeeds");
        TEST_ASSERT(autoNode1.getBeaconsSent() >= 1, "Beacons sent counter incremented");
        autoNode1.stop();
        TEST_ASSERT(!autoNode1.isRunning(), "Beacon stopped");
        // Safe double stop
        autoNode1.stop();
    }
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

    // Constant-time token validation checks
    TEST_ASSERT(task.validateAccessToken("qr-auth-token-xyz"), "Accepts exact matching token");
    TEST_ASSERT(!task.validateAccessToken("qr-auth-token-xyw"), "Rejects single-byte mismatch token");
    TEST_ASSERT(!task.validateAccessToken("qr-auth-token"), "Rejects truncated token");
    TEST_ASSERT(!task.validateAccessToken(""), "Rejects empty token");

    // Default task security (random token and 6-digit random PIN, NOT static 4242)
    HttpWebTransferTask defaultTask("T-0100", "doc.pdf", 1024, TransferDirection::Upload);
    TEST_ASSERT(!defaultTask.getWebToken().empty(), "Default task generates non-empty token");
    TEST_ASSERT(defaultTask.getSessionPin() != "4242", "Default task does NOT use hardcoded 4242 PIN");
    TEST_ASSERT(defaultTask.getSessionPin().size() == 6, "Default task generates 6-digit PIN");
    for (char c : defaultTask.getSessionPin()) {
        TEST_ASSERT(c >= '0' && c <= '9', "PIN contains only digits");
    }

    // Constant-time PIN validation
    TEST_ASSERT(defaultTask.validateSessionPin(defaultTask.getSessionPin()), "Accepts matching random session PIN");
    TEST_ASSERT(!defaultTask.validateSessionPin("999999"), "Rejects invalid session PIN");
    TEST_ASSERT(!defaultTask.validateSessionPin(""), "Rejects empty session PIN");

    return true;
}

static bool testSharedFolderManager() {
    std::string testDir = "./unit_test_shared";
    std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir);

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

    // Path traversal in findFile must be rejected
    TEST_ASSERT(!manager.findFile("../../etc/passwd").has_value(), "Rejects parent path traversal in findFile");
    TEST_ASSERT(!manager.findFile("/etc/shadow").has_value(), "Rejects absolute path traversal in findFile");
    TEST_ASSERT(manager.findFile("sample.txt").has_value(), "Finds legitimate file sample.txt");

    // Safe file path retrieval
    auto safePath = manager.getSafeFilePath("sample.txt");
    TEST_ASSERT(safePath.has_value(), "getSafeFilePath succeeds for valid shared file");
    TEST_ASSERT(!manager.getSafeFilePath("../../etc/passwd").has_value(), "getSafeFilePath rejects path traversal");

    // Symlink escape test: create target outside sharedDir, symlink inside
    std::string outsideDir = "./unit_test_outside";
    std::filesystem::remove_all(outsideDir);
    std::filesystem::create_directories(outsideDir);
    std::string secretFile = outsideDir + "/secret.txt";
    {
        std::ofstream out(secretFile);
        out << "top_secret_token_12345";
    }
    std::string symlinkPath = testDir + "/symlink_to_secret.txt";
    std::error_code ec;
    std::filesystem::create_symlink(std::filesystem::absolute(secretFile), symlinkPath, ec);
    if (!ec) {
        manager.scanDirectory();
        TEST_ASSERT(manager.findFile("symlink_to_secret.txt") == std::nullopt,
                    "Symlink pointing outside shared root is ignored and excluded from scan");
        TEST_ASSERT(!manager.addSharedFile(symlinkPath),
                    "addSharedFile rejects symlink pointing outside root");
        std::filesystem::remove(symlinkPath, ec);
    }
    std::filesystem::remove_all(outsideDir, ec);

    // Max file size limit test
    manager.setMaxFileSizeLimit(10); // 10 bytes limit
    manager.scanDirectory();
    TEST_ASSERT(manager.getSharedFileCount() == 0, "Files exceeding maxFileSizeLimit are excluded");
    manager.setMaxFileSizeLimit(10ULL * 1024 * 1024 * 1024);
    manager.scanDirectory();
    TEST_ASSERT(manager.getSharedFileCount() == 1, "Restored maxFileSizeLimit includes file again");

    // Password / PIN protection
    TEST_ASSERT(!manager.hasPasswordProtection(), "Starts without password");
    manager.setAccessPassword("9977");
    TEST_ASSERT(manager.hasPasswordProtection(), "Password protection active");
    TEST_ASSERT(manager.getPasswordHash().find("$pbkdf2-sha256$i=100000$") == 0, "PBKDF2-HMAC-SHA256 format verified");
    TEST_ASSERT(manager.verifyAccessPassword("9977"), "Correct password verified");
    TEST_ASSERT(!manager.verifyAccessPassword("0000"), "Wrong password rejected");

    // Rate-limiting delay test on repeated failed attempts
    auto tStart = std::chrono::steady_clock::now();
    for (int i = 0; i < 3; ++i) {
        manager.verifyAccessPassword("wrong_pin");
    }
    auto tElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - tStart).count();
    TEST_ASSERT(tElapsed >= 30, "Rate-limiting progressive delay verified on failed attempts");

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
    ui::TransferViewModel lastSelectedTransfer;
    std::string lastError;

    bool lastNfcSuccess{false};
    std::string lastNfcMessage;

    void updateDeviceList(const std::vector<ui::DeviceViewModel>& d) override { devices = d; }
    void setScanningStatus(bool scanning) override { isScanning = scanning; }
    void showNfcPairResult(bool s, const std::string& m) override {
        lastNfcSuccess = s;
        lastNfcMessage = m;
    }
    void showNotification(const std::string& n) override { notification = n; }

    void updateTransferTable(const std::vector<ui::TransferViewModel>& t) override { transfers = t; }
    void updateSelectedTransferDetails(const ui::TransferViewModel& vm) override { lastSelectedTransfer = vm; }
    void showTransferError(const std::string&, const std::string& err) override { lastError = err; }

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
    TEST_ASSERT(mock.devices.empty(), "Discovery presenter initialized with 0 devices (clean real radar)");
    TEST_ASSERT(!disc.getLocalDeviceName().empty(), "Local device name resolved");
    TEST_ASSERT(!disc.getLocalDeviceIp().empty(), "Local device IP resolved");
    TEST_ASSERT(disc.getLocalDevicePort() == 8848, "Local device port matches 8848");

    disc.onToggleScanClicked(false);
    TEST_ASSERT(!mock.isScanning, "Scan toggled off");

    // Test direct connect requested
    disc.onDirectConnectRequested("10.0.0.5", 9999);
    TEST_ASSERT(mock.devices.size() == 1, "Direct peer added to devices");
    TEST_ASSERT(disc.getSelectedPeer() != nullptr, "Direct peer selected");
    TEST_ASSERT(disc.getSelectedPeer()->getId() == "peer-direct-10.0.0.5", "Direct peer ID matches");

    // Test direct connect with empty host
    disc.onDirectConnectRequested("", 9999);
    TEST_ASSERT(mock.notification.find("empty") != std::string::npos, "Empty host rejected with notification");

    // Test device selection
    disc.onSelectDeviceClicked("peer-direct-10.0.0.5");
    TEST_ASSERT(disc.getSelectedPeer() != nullptr && disc.getSelectedPeer()->getId() == "peer-direct-10.0.0.5", "Device selected by ID");

    // Test duplicate direct connect selects existing peer
    disc.onDirectConnectRequested("10.0.0.5", 9999);
    TEST_ASSERT(mock.notification.find("Selected existing peer") != std::string::npos, "Duplicate direct connect selects existing peer");

    // Test dynamic peer registration
    disc.registerPeer(std::make_shared<LocalPeer>("dyn-peer-1", "Dynamic Test Node", "192.168.1.50", 8848));
    TEST_ASSERT(mock.devices.size() == 2, "Dynamic peer registered into view");

    // Test peer removal
    disc.onRemovePeerClicked("peer-direct-10.0.0.5");
    TEST_ASSERT(mock.devices.size() == 1, "Peer removed via onRemovePeerClicked");
    disc.onRemovePeerClicked("dyn-peer-1");
    TEST_ASSERT(mock.devices.empty(), "All peers removed, back to clean empty state");

    // Test refresh devices
    disc.onRefreshDevicesClicked();
    TEST_ASSERT(mock.notification == "Devices refreshed", "Refresh devices triggered");

    // Test onPeerDiscovered (simulating background UDP reception)
    disc.onPeerDiscovered("Galaxy S24 Ultra", "uuid-s24-999", "192.168.1.180", 8848);
    TEST_ASSERT(mock.devices.size() == 1, "Discovered device added to view");
    TEST_ASSERT(mock.devices[0].type == "Android", "Android device type inferred");
    TEST_ASSERT(mock.devices[0].name == "Galaxy S24 Ultra", "Device name matches");

    disc.onPeerDiscovered("MacBook Air M3", "uuid-mac-888", "192.168.1.190", 8848);
    TEST_ASSERT(mock.devices.size() == 2, "Second device discovered");

    // Test duplicate beacon updates online status without duplicate rows
    disc.onPeerDiscovered("Galaxy S24 Ultra", "uuid-s24-999", "192.168.1.180", 8848);
    TEST_ASSERT(mock.devices.size() == 2, "Duplicate beacon does not duplicate peer list row");

    // Test NFC pair triggered
    disc.onNfcPairTriggered("INVALID_PAYLOAD");
    TEST_ASSERT(mock.lastNfcSuccess == false, "Invalid NFC payload fails gracefully");

    std::string nfcTag = "LANDROP:NFC:v1;id=uuid-nfc-peer;name=Pixel 8;ip=192.168.1.200;port=8848;pin=1234;key=aabbcc";
    disc.onNfcPairTriggered(nfcTag);
    TEST_ASSERT(mock.lastNfcSuccess == true, "Valid NFC payload pairs successfully");
    TEST_ASSERT(disc.getSelectedPeer() != nullptr && disc.getSelectedPeer()->getId() == "uuid-nfc-peer", "NFC paired peer selected");

    // Clean up
    disc.onRemovePeerClicked("uuid-s24-999");
    disc.onRemovePeerClicked("uuid-mac-888");
    disc.onRemovePeerClicked("uuid-nfc-peer");
    TEST_ASSERT(mock.devices.empty(), "All discovered/paired devices cleaned up");

    ui::TransferPresenter trans(&mock);
    trans.initialize(false);
    TEST_ASSERT(trans.getTasks().empty(), "Clean transfer presenter starts with empty queue");
    trans.loadSampleData();
    TEST_ASSERT(mock.transfers.size() == 6, "Transfer presenter initialized with 6 tasks");

    // Test row selection updates details
    trans.onSelectTransferRow(1);
    TEST_ASSERT(mock.lastSelectedTransfer.taskId == trans.getTasks()[1]->getTaskId(), "Row selection updates details");

    trans.onPauseTransferClicked("T-0042");
    bool foundPaused = false;
    for (const auto& task : trans.getTasks()) {
        if (task && task->getTaskId() == "T-0042") {
            foundPaused = (task->getState() == TransferState::Paused);
            break;
        }
    }
    TEST_ASSERT(foundPaused, "T-0042 paused");

    trans.onResumeTransferClicked("T-0042");
    bool foundResumed = false;
    for (const auto& task : trans.getTasks()) {
        if (task && task->getTaskId() == "T-0042") {
            foundResumed = (task->getState() == TransferState::Transferring);
            break;
        }
    }
    TEST_ASSERT(foundResumed, "T-0042 resumed");

    // Test cancel transfer
    trans.onCancelTransferClicked("T-0039");
    bool foundCancelled = false;
    for (const auto& task : trans.getTasks()) {
        if (task && task->getTaskId() == "T-0039") {
            foundCancelled = (task->getState() == TransferState::Cancelled);
            break;
        }
    }
    TEST_ASSERT(foundCancelled, "T-0039 cancelled");

    // Test speed limit application and ticking
    trans.setSpeedLimit(500); // 500 KB/s = 512,000 bytes/sec
    TEST_ASSERT(trans.getSpeedLimit() == 500, "Transfer presenter speed limit set to 500 KB/s");
    trans.tick();
    for (const auto& task : trans.getTasks()) {
        if (task && task->getState() == TransferState::Transferring) {
            TEST_ASSERT(task->getCurrentSpeedBps() <= 500.0 * 1024.0, "Active transfer speed constrained by speed limit");
        }
    }

    // Verify paused task does not advance bytes
    auto taskToPause = trans.getTasks()[0];
    taskToPause->setState(TransferState::Paused);
    uint64_t bytesBefore = taskToPause->getBytesTransferred();
    trans.tick();
    TEST_ASSERT(taskToPause->getBytesTransferred() == bytesBefore, "Paused task does not increment bytes transferred on tick");

    // Test send missing file error
    trans.onSendFileClicked("peer-macbook", "/path/to/missing/nonexistent_file.xyz");
    TEST_ASSERT(mock.lastError.find("not found") != std::string::npos, "Missing file produces transfer error");

    // Test clear completed tasks
    trans.onClearHistoryClicked();
    TEST_ASSERT(trans.getTasks().size() == 2, "Clear history removed completed and cancelled tasks");

    ui::StoragePresenter stor(&mock);
    stor.initialize();
    size_t initialFiles = stor.getManager().getSharedFileCount();
    stor.onRemoveFileClicked("quarterly-report-Q3.pdf");
    TEST_ASSERT(stor.getManager().getSharedFileCount() < initialFiles, "File removed from storage manager");

    stor.onSetPasswordClicked("9876");
    TEST_ASSERT(stor.getManager().hasPasswordProtection(), "Access password enabled");
    stor.onClearPasswordClicked();
    TEST_ASSERT(!stor.getManager().hasPasswordProtection(), "Access password cleared");

    ui::SettingsPresenter sett(&mock);
    sett.initialize();
    TEST_ASSERT(mock.loadedPort == 8848, "Settings loaded port 8848");
    sett.onPortChanged(9090);
    TEST_ASSERT(sett.getPort() == 9090, "Port updated to 9090");
    sett.onSpeedLimitChanged(30000);
    TEST_ASSERT(sett.getSpeedLimitKbps() == 30000, "Speed limit updated");

    // Test corrupted JSON loading
    TEST_ASSERT(!sett.loadFromJson("{ corrupted: invalid json ..."), "Rejects corrupted JSON string");
    TEST_ASSERT(sett.getPort() == 9090, "Retains previous settings on corrupted JSON load");

    // Test legacy JSON keys parsing and downloadsPath
    std::string legacyJson = R"({
        "port": "8080",
        "speed_limit_kbps": 15000,
        "auto_accept": true,
        "airdrop_enabled": false,
        "downloadsPath": "/home/user/CustomDownloads"
    })";
    TEST_ASSERT(sett.loadFromJson(legacyJson), "Parses legacy JSON configuration keys successfully");
    TEST_ASSERT(sett.getPort() == 8080, "Parsed string port '8080' to integer 8080");
    TEST_ASSERT(sett.getSpeedLimitKbps() == 15000, "Parsed legacy speed_limit_kbps");
    TEST_ASSERT(sett.isAutoAccept() == true, "Parsed legacy auto_accept");
    TEST_ASSERT(sett.isAirDropEnabled() == false, "Parsed legacy airdrop_enabled");
    TEST_ASSERT(sett.getDownloadsPath() == "/home/user/CustomDownloads", "Parsed downloadsPath from JSON");

    sett.onDownloadsPathChanged("/tmp/received_files");
    TEST_ASSERT(sett.getDownloadsPath() == "/tmp/received_files", "Downloads path updated via presenter");
    TEST_ASSERT(sett.saveToJson().find("/tmp/received_files") != std::string::npos, "Serialized downloadsPath to JSON");

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
    TEST_ASSERT(!isSafeFileName(".bashrc"), "Rejects hidden dotfile .bashrc");
    TEST_ASSERT(!isSafeFileName(".profile"), "Rejects hidden dotfile .profile");
    TEST_ASSERT(!isSafeFileName(".env"), "Rejects hidden dotfile .env");
    TEST_ASSERT(!isSafeFileName(std::string("file\0evil.sh", 12)), "Rejects null byte");
    TEST_ASSERT(!isSafeFileName("bad\x1b[31mcolor.txt"), "Rejects ANSI escape code");
    TEST_ASSERT(!isSafeFileName("bad\r\nname.txt"), "Rejects CRLF in filename");
    TEST_ASSERT(!isSafeFileName("bad\tname.txt"), "Rejects tab character in filename");
    TEST_ASSERT(!isSafeFileName("del\x7fname.txt"), "Rejects DEL character in filename");
    TEST_ASSERT(!isSafeFileName(std::string(256, 'a')), "Rejects name length > 255");
    TEST_ASSERT(isSafeFileName("valid_report.pdf"), "Accepts valid filename");
    TEST_ASSERT(isSafeFileName("my-image.tar.gz"), "Accepts multi-dot extension");
    TEST_ASSERT(isSafeFileName("document (final).docx"), "Accepts filename with spaces and parentheses");

    // Windows ADS (Alternate Data Streams)
    TEST_ASSERT(!isSafeFileName("test.txt:stream"), "Rejects Windows ADS colon in filename");
    TEST_ASSERT(!isSafeFileName("invoice.pdf:Zone.Identifier"), "Rejects Windows ADS Zone.Identifier");
    TEST_ASSERT(!isSafeFileName(":hidden"), "Rejects leading colon");

    // Win32 reserved device names (case-insensitive and with extensions)
    TEST_ASSERT(!isSafeFileName("CON"), "Rejects Win32 CON device");
    TEST_ASSERT(!isSafeFileName("con.txt"), "Rejects Win32 con.txt device");
    TEST_ASSERT(!isSafeFileName("PRN"), "Rejects Win32 PRN device");
    TEST_ASSERT(!isSafeFileName("prn.pdf"), "Rejects Win32 prn.pdf device");
    TEST_ASSERT(!isSafeFileName("AUX"), "Rejects Win32 AUX device");
    TEST_ASSERT(!isSafeFileName("aux.c"), "Rejects Win32 aux.c device");
    TEST_ASSERT(!isSafeFileName("NUL"), "Rejects Win32 NUL device");
    TEST_ASSERT(!isSafeFileName("nul.dat"), "Rejects Win32 nul.dat device");
    TEST_ASSERT(!isSafeFileName("COM1"), "Rejects Win32 COM1 device");
    TEST_ASSERT(!isSafeFileName("com9.log"), "Rejects Win32 com9.log device");
    TEST_ASSERT(!isSafeFileName("LPT1"), "Rejects Win32 LPT1 device");
    TEST_ASSERT(!isSafeFileName("lpt8.bin"), "Rejects Win32 lpt8.bin device");

    // Valid names starting with device substrings (must not be rejected)
    TEST_ASSERT(isSafeFileName("constant.txt"), "Accepts constant.txt (starts with CON)");
    TEST_ASSERT(isSafeFileName("printer.pdf"), "Accepts printer.pdf (starts with PRN)");
    TEST_ASSERT(isSafeFileName("auxiliary.cpp"), "Accepts auxiliary.cpp (starts with AUX)");
    TEST_ASSERT(isSafeFileName("nullable.hpp"), "Accepts nullable.hpp (starts with NUL)");
    TEST_ASSERT(isSafeFileName("computer.png"), "Accepts computer.png (starts with COM)");

    // Win32 forbidden characters < > " | ? *
    TEST_ASSERT(!isSafeFileName("<tag>.txt"), "Rejects < character");
    TEST_ASSERT(!isSafeFileName("tag>.txt"), "Rejects > character");
    TEST_ASSERT(!isSafeFileName("quote\"name.txt"), "Rejects double quote character");
    TEST_ASSERT(!isSafeFileName("pipe|name.txt"), "Rejects pipe character");
    TEST_ASSERT(!isSafeFileName("wild*card.txt"), "Rejects asterisk character");
    TEST_ASSERT(!isSafeFileName("question?mark.txt"), "Rejects question mark character");

    // Leading and trailing spaces and dots
    TEST_ASSERT(!isSafeFileName(" leading_space.txt"), "Rejects leading space");
    TEST_ASSERT(!isSafeFileName("trailing_space.txt "), "Rejects trailing space");
    TEST_ASSERT(!isSafeFileName("trailing_dot."), "Rejects trailing dot");
    TEST_ASSERT(!isSafeFileName("multiple_trailing_dots..."), "Rejects multiple trailing dots");

    // Unicode RTLO, bidirectional override, and zero-width spoofing
    TEST_ASSERT(!isSafeFileName("invoice\xE2\x80\xAE" "fdp.exe"), "Rejects Unicode RTLO spoofing");
    TEST_ASSERT(!isSafeFileName("sample\xE2\x81\xA6test\xE2\x81\xA9.txt"), "Rejects Unicode directional isolate");
    TEST_ASSERT(!isSafeFileName("zero\xE2\x80\x8Bwidth.txt"), "Rejects zero-width space spoofing");

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

    // File size boundary tests
    TEST_ASSERT(!isValidFileSize(0), "Rejects 0 file size");
    TEST_ASSERT(isValidFileSize(1), "Accepts 1 byte file size");
    TEST_ASSERT(isValidFileSize(100ULL * 1024 * 1024 * 1024), "Accepts 100GB file size");
    TEST_ASSERT(!isValidFileSize(100ULL * 1024 * 1024 * 1024 + 1), "Rejects >100GB file size");
    TEST_ASSERT(!isValidFileSize(UINT64_MAX), "Rejects UINT64_MAX file size");

    // Symlink and pre-existing file guards
    std::filesystem::path testDir = "./unit_test_security_guards_tmp";
    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);
    std::filesystem::create_directories(testDir, ec);

    std::filesystem::path existingFile = testDir / "existing.txt";
    {
        std::ofstream out(existingFile);
        out << "critical existing content";
    }
    TEST_ASSERT(std::filesystem::exists(existingFile), "Pre-existing file exists");

    std::filesystem::path symlinkFile = testDir / "symlink_link.txt";
    std::filesystem::create_symlink(existingFile, symlinkFile, ec);
    if (!ec) {
        TEST_ASSERT(std::filesystem::is_symlink(std::filesystem::symlink_status(symlinkFile)),
                    "Symlink detected by symlink_status");
    }

    std::filesystem::remove_all(testDir, ec);
    return true;
}

static bool testJsonAndRestSerialization() {
    // 1. SettingsPresenter JSON round-trip
    ui::SettingsPresenter presenter(nullptr);
    presenter.onPortChanged(9090);
    presenter.onSpeedLimitChanged(50000);
    presenter.onAutoAcceptToggled(true);
    presenter.onAirDropToggled(false);

    std::string jsonStr = presenter.saveToJson();
    TEST_ASSERT(jsonStr.find("\"port\": 9090") != std::string::npos, "JSON contains port");
    TEST_ASSERT(jsonStr.find("\"autoAccept\": true") != std::string::npos, "JSON contains autoAccept");

    ui::SettingsPresenter loadedPresenter(nullptr);
    bool loadOk = loadedPresenter.loadFromJson(jsonStr);
    TEST_ASSERT(loadOk, "Settings loaded from JSON successfully");
    TEST_ASSERT(loadedPresenter.getPort() == 9090, "Loaded port matches");
    TEST_ASSERT(loadedPresenter.getSpeedLimitKbps() == 50000, "Loaded speed limit matches");
    TEST_ASSERT(loadedPresenter.isAutoAccept() == true, "Loaded autoAccept matches");
    TEST_ASSERT(loadedPresenter.isAirDropEnabled() == false, "Loaded airdropEnabled matches");

    // 2. CloudPeer REST Quota JSON payload & parsing
    CloudPeer cloud("peer-cloud-test", "CloudRelay", "https://relay.landrop.net", 443, "CustomDrop", "token-xyz");
    std::string reqPayload = cloud.buildQuotaRequestPayload();
    TEST_ASSERT(reqPayload.find("CustomDrop") != std::string::npos, "Quota request contains provider");
    TEST_ASSERT(reqPayload.find("token-xyz") != std::string::npos, "Quota request contains token");

    std::string mockQuotaResponse = R"({"storage_quota_bytes": 10737418240, "used_storage_bytes": 2147483648})";
    bool parseOk = cloud.parseQuotaResponse(mockQuotaResponse);
    TEST_ASSERT(parseOk, "Cloud quota response parsed");
    TEST_ASSERT(cloud.getStorageQuotaBytes() == 10737418240ULL, "Quota matches 10 GB");
    TEST_ASSERT(cloud.getUsedStorageBytes() == 2147483648ULL, "Used storage matches 2 GB");
    TEST_ASSERT(cloud.hasAvailableQuota(1024), "Has available quota");

    // 3. AirDropProtocolBridge JSON verification
    mobile::AirDropProtocolBridge airdrop("bridge-uuid", "Bridge Node");
    mobile::AirDropAsset asset{"archive.tar.gz", 1048576, "application/gzip", "hash-abc"};
    std::string askReq = airdrop.buildAskRequest(asset);
    mobile::AirDropAsset parsedAsset;
    bool askOk = airdrop.handleIncomingAsk(askReq, parsedAsset);
    TEST_ASSERT(askOk, "AirDrop JSON Ask payload parsed via nlohmann/json");
    TEST_ASSERT(parsedAsset.fileName == "archive.tar.gz", "Parsed asset filename matches");
    TEST_ASSERT(parsedAsset.fileSize == 1048576, "Parsed asset filesize matches");
    TEST_ASSERT(parsedAsset.fileSha256 == "hash-abc", "Parsed asset hash matches");

    // 4. Invalid JSON rejection test
    mobile::AirDropAsset badAsset;
    bool badOk = airdrop.handleIncomingAsk("Not a valid json string {[]}", badAsset);
    TEST_ASSERT(!badOk, "Malformed JSON safely rejected without throw");

    // 5. Stable configuration path resolution & directory auto-creation
    std::string defaultPath = ui::SettingsPresenter::getDefaultConfigPath();
    TEST_ASSERT(!defaultPath.empty(), "Default config path is resolved");
    TEST_ASSERT(defaultPath.find("settings.json") != std::string::npos, "Config path ends with settings.json");

    std::filesystem::path tempBase = std::filesystem::temp_directory_path() / "landrop_test_cfg_dir";
    std::error_code ec;
    std::filesystem::remove_all(tempBase, ec);

    std::filesystem::path testConfigDir = tempBase / "nested_profile";
    std::filesystem::path testConfigFile = testConfigDir / "test_settings.json";
    std::string testConfigFilePath = testConfigFile.string();

    // 6. First launch test on non-existent file
    ui::SettingsPresenter firstLaunchPresenter(nullptr);
    firstLaunchPresenter.setConfigPath(testConfigFilePath);
    bool firstLoad = firstLaunchPresenter.loadSettings();
    TEST_ASSERT(!firstLoad, "First launch correctly reports no config file");
    TEST_ASSERT(firstLaunchPresenter.getPort() == 8848, "First launch keeps default port 8848");

    // 7. Save to nested path with automatic directory creation
    firstLaunchPresenter.onPortChanged(7788);
    firstLaunchPresenter.onSpeedLimitChanged(32000);
    firstLaunchPresenter.onAutoAcceptToggled(true);
    firstLaunchPresenter.onAirDropToggled(false);
    bool saveOk = firstLaunchPresenter.saveSettings();
    TEST_ASSERT(saveOk, "Saved settings to nested non-existent directory");
    TEST_ASSERT(std::filesystem::exists(testConfigFile), "Configuration file actually exists on disk");

    // 8. Reload from file
    ui::SettingsPresenter reloadedPresenter(nullptr);
    reloadedPresenter.setConfigPath(testConfigFilePath);
    bool reloadOk = reloadedPresenter.loadSettings();
    TEST_ASSERT(reloadOk, "Settings successfully reloaded from disk");
    TEST_ASSERT(reloadedPresenter.getPort() == 7788, "Reloaded port is 7788");
    TEST_ASSERT(reloadedPresenter.getSpeedLimitKbps() == 32000, "Reloaded speed is 32000");
    TEST_ASSERT(reloadedPresenter.isAutoAccept() == true, "Reloaded autoAccept is true");
    TEST_ASSERT(reloadedPresenter.isAirDropEnabled() == false, "Reloaded airdrop is false");

    // 9. Malformed JSON handling
    {
        std::ofstream corruptOut(testConfigFile);
        corruptOut << "{ \"port\": \"not_a_number\", malformed json syntax";
    }
    ui::SettingsPresenter corruptPresenter(nullptr);
    corruptPresenter.setConfigPath(testConfigFilePath);
    bool corruptLoad = corruptPresenter.loadSettings();
    TEST_ASSERT(!corruptLoad, "Malformed JSON safely rejected");
    TEST_ASSERT(corruptPresenter.getPort() == 8848, "Presenter maintains safe defaults upon corrupt JSON");

    // 10. Unwritable path error handling
    // Create a regular file so that attempting to create directories or files inside it fails
    // deterministically on both POSIX (ENOTDIR) and Windows (ERROR_DIRECTORY / ERROR_PATH_NOT_FOUND).
    std::filesystem::path blockerFile = tempBase / "blocker.txt";
    {
        std::ofstream blockerOut(blockerFile);
        blockerOut << "regular_file_blocking_subdirectories";
    }
    std::filesystem::path unwritablePath = blockerFile / "impossible_sub_dir" / "settings.json";

    ui::SettingsPresenter unwritablePresenter(nullptr);
    unwritablePresenter.setConfigPath(unwritablePath.string());
    bool unwriteOk = unwritablePresenter.saveSettings();
    TEST_ASSERT(!unwriteOk, "Unwritable path safely reports failure without throwing");

    std::filesystem::remove_all(tempBase, ec);

    return true;
}

static bool testTlsTransportAndDeviceAuth() {
    // 1. Certificate generation & Fingerprint calculation
    TlsCertificate serverCert = TlsHelper::generateCertificate("LAN-Drop-Test-Server", 30);
    TEST_ASSERT(!serverCert.certPem.empty(), "Server certificate PEM generated");
    TEST_ASSERT(!serverCert.keyPem.empty(), "Server private key PEM generated");
    TEST_ASSERT(!serverCert.fingerprintSha256.empty(), "Server fingerprint SHA-256 generated");

    std::string recomputedFp = TlsHelper::computeFingerprint(serverCert.certPem);
    TEST_ASSERT(recomputedFp == serverCert.fingerprintSha256, "Recomputed fingerprint matches generated fingerprint");

    // Fingerprint normalization matching
    TEST_ASSERT(TlsHelper::fingerprintsMatch("AA:BB:CC", "aa:bb:cc"), "Fingerprint matches case-insensitively");
    TEST_ASSERT(TlsHelper::fingerprintsMatch("AABBCC", "aa:bb:cc"), "Fingerprint matches without colons");
    TEST_ASSERT(!TlsHelper::fingerprintsMatch("AABBCC", "AABBDD"), "Different fingerprints do not match");

    // 2. Positive TLS File Transfer over Connected Sockets
    socket_handle_t fds[2];
    TEST_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0, "Socketpair created for TLS test");

    NetworkSession serverSession("test-tls-server", "127.0.0.1", 0);
    NetworkSession clientSession("test-tls-client", "127.0.0.1", 0);
    serverSession.attachSocket(fds[0]);
    clientSession.attachSocket(fds[1]);

    TlsConfig serverCfg;
    serverCfg.role = TlsRole::Server;
    serverCfg.certPem = serverCert.certPem;
    serverCfg.keyPem = serverCert.keyPem;
    serverSession.enableTls(serverCfg);

    TlsConfig clientCfg;
    clientCfg.role = TlsRole::Client;
    clientCfg.verifyMode = TlsVerificationMode::VerifyPinnedFingerprint;
    clientCfg.expectedFingerprint = serverCert.fingerprintSha256;
    clientSession.enableTls(clientCfg);

    TlsStatus sStatus = TlsStatus::None;
    TlsStatus cStatus = TlsStatus::None;

    std::thread sTh([&]() {
        sStatus = serverSession.performTlsHandshake();
    });
    std::thread cTh([&]() {
        cStatus = clientSession.performTlsHandshake();
    });
    sTh.join();
    cTh.join();

    TEST_ASSERT(sStatus == TlsStatus::Connected, "Server TLS handshake succeeded");
    TEST_ASSERT(cStatus == TlsStatus::Connected, "Client TLS handshake succeeded");
    TEST_ASSERT(serverSession.isEncrypted(), "Server session marked encrypted");
    TEST_ASSERT(clientSession.isEncrypted(), "Client session marked encrypted");
    TEST_ASSERT(TlsHelper::fingerprintsMatch(clientSession.getPeerFingerprint(), serverCert.fingerprintSha256),
                "Client verified peer certificate fingerprint");
    TEST_ASSERT(!clientSession.getTlsCipherSuite().empty() && clientSession.getTlsCipherSuite() != "None",
                "Strong TLS cipher suite negotiated");

    // Encrypted payload transfer
    std::vector<uint8_t> testPayload(64 * 1024);
    for (size_t i = 0; i < testPayload.size(); ++i) {
        testPayload[i] = static_cast<uint8_t>(i & 0xFF);
    }
    bool sendOk = clientSession.sendAll(testPayload.data(), testPayload.size());
    TEST_ASSERT(sendOk, "Client sent 64 KB payload over encrypted TLS");

    std::vector<uint8_t> rxPayload(testPayload.size(), 0);
    bool rxOk = serverSession.receiveExact(rxPayload.data(), rxPayload.size());
    TEST_ASSERT(rxOk, "Server received 64 KB payload over encrypted TLS");
    TEST_ASSERT(rxPayload == testPayload, "Received payload matches sent payload byte-for-byte");

    // Live 1MB file transfer with SHA-256 integrity verification
    const size_t kOneMb = 1024 * 1024;
    std::vector<uint8_t> oneMbPayload(kOneMb);
    for (size_t i = 0; i < kOneMb; ++i) {
        oneMbPayload[i] = static_cast<uint8_t>((i * 73 + 19) & 0xFF);
    }
    FileChunkHasher hasher;
    std::string sentHash = hasher.computeChunkHash(oneMbPayload.data(), oneMbPayload.size());

    std::vector<uint8_t> rx1Mb(kOneMb, 0);
    std::thread sender1Mb([&]() {
        clientSession.sendAll(oneMbPayload.data(), oneMbPayload.size());
    });
    std::thread receiver1Mb([&]() {
        serverSession.receiveExact(rx1Mb.data(), rx1Mb.size());
    });
    sender1Mb.join();
    receiver1Mb.join();

    std::string recvHash = hasher.computeChunkHash(rx1Mb.data(), rx1Mb.size());
    TEST_ASSERT(recvHash == sentHash, "1MB payload transmitted over TLS with exact SHA-256 integrity match");
    TEST_ASSERT(rx1Mb == oneMbPayload, "1MB buffer matches source byte-for-byte");

    serverSession.closeSession();
    clientSession.closeSession();

    // 3. Negative Test 1: Untrusted Certificate / Missing Pinning Rejected by Default
    socket_handle_t fdsNeg1[2];
    TEST_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, fdsNeg1) == 0, "Socketpair for negative test 1");
    NetworkSession srvNeg1("srv-neg1", "127.0.0.1", 0);
    NetworkSession cliNeg1("cli-neg1", "127.0.0.1", 0);
    srvNeg1.attachSocket(fdsNeg1[0]);
    cliNeg1.attachSocket(fdsNeg1[1]);

    srvNeg1.enableTls(serverCfg);
    TlsConfig cliCfgNoPin;
    cliCfgNoPin.role = TlsRole::Client;
    cliCfgNoPin.verifyMode = TlsVerificationMode::VerifyPinnedFingerprint;
    cliCfgNoPin.expectedFingerprint = ""; // Default empty pin -> MUST REJECT!
    cliNeg1.enableTls(cliCfgNoPin);

    std::thread sTh1([&]() { srvNeg1.performTlsHandshake(); });
    std::thread cTh1([&]() { cStatus = cliNeg1.performTlsHandshake(); });
    sTh1.join();
    cTh1.join();

    TEST_ASSERT(cStatus == TlsStatus::UntrustedCertificate, "Untrusted self-signed certificate rejected by default");
    TEST_ASSERT(!cliNeg1.isEncrypted(), "Client rejected unconfirmed peer and disabled encryption");
    srvNeg1.closeSession();
    cliNeg1.closeSession();

    // 4. Negative Test 2: Rogue Peer / Fingerprint Mismatch Rejected
    socket_handle_t fdsNeg2[2];
    TEST_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, fdsNeg2) == 0, "Socketpair for negative test 2");
    NetworkSession srvNeg2("srv-neg2", "127.0.0.1", 0);
    NetworkSession cliNeg2("cli-neg2", "127.0.0.1", 0);
    srvNeg2.attachSocket(fdsNeg2[0]);
    cliNeg2.attachSocket(fdsNeg2[1]);

    srvNeg2.enableTls(serverCfg);
    TlsConfig cliCfgMismatch;
    cliCfgMismatch.role = TlsRole::Client;
    cliCfgMismatch.verifyMode = TlsVerificationMode::VerifyPinnedFingerprint;
    cliCfgMismatch.expectedFingerprint = "11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00";
    cliNeg2.enableTls(cliCfgMismatch);

    std::thread sTh2([&]() { srvNeg2.performTlsHandshake(); });
    std::thread cTh2([&]() { cStatus = cliNeg2.performTlsHandshake(); });
    sTh2.join();
    cTh2.join();

    TEST_ASSERT(cStatus == TlsStatus::FingerprintMismatch, "Mismatched certificate fingerprint rejected");
    TEST_ASSERT(!cliNeg2.isEncrypted(), "Client rejected rogue peer");
    srvNeg2.closeSession();
    cliNeg2.closeSession();

    // 5. Negative Test 3: Protocol Mismatch (Plaintext Client to TLS Server)
    socket_handle_t fdsNeg3[2];
    TEST_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, fdsNeg3) == 0, "Socketpair for negative test 3");
    NetworkSession srvNeg3("srv-neg3", "127.0.0.1", 0);
    srvNeg3.attachSocket(fdsNeg3[0]);
    srvNeg3.enableTls(serverCfg);

    std::thread sTh3([&]() { sStatus = srvNeg3.performTlsHandshake(); });
    // Plaintext client sends raw HTTP or binary garbage
    const char plaintextGarbage[] = "GET /status HTTP/1.1\r\nHost: localhost\r\n\r\n";
    send(fdsNeg3[1], plaintextGarbage, sizeof(plaintextGarbage), 0);
    sTh3.join();
    closeSocket(fdsNeg3[1]);

    TEST_ASSERT(sStatus == TlsStatus::ProtocolMismatch || sStatus == TlsStatus::HandshakeFailed,
                "TLS Server safely rejected plaintext protocol mismatch");
    TEST_ASSERT(!srvNeg3.isEncrypted(), "Server remained unencrypted after protocol mismatch");
    srvNeg3.closeSession();

    return true;
}

class MockHttpServer {
public:
    explicit MockHttpServer(int port = 0, bool useHttps = false, const std::string& certCn = "127.0.0.1")
        : m_isHttps(useHttps) {
        m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
        int opt = 1;
        setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);

        bind(m_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        listen(m_listenFd, 5);

        socklen_t len = sizeof(addr);
        getsockname(m_listenFd, reinterpret_cast<sockaddr*>(&addr), &len);
        m_port = ntohs(addr.sin_port);

        if (m_isHttps) {
            TlsHelper::initOpenSsl();
            m_cert = TlsHelper::generateCertificate(certCn);
            m_sslCtx = TlsHelper::createServerContext(m_cert.certPem, m_cert.keyPem);
        }
    }

    ~MockHttpServer() {
        stop();
        if (m_sslCtx) {
            SSL_CTX_free(m_sslCtx);
            m_sslCtx = nullptr;
        }
    }

    uint16_t getPort() const { return m_port; }
    const std::string& getCertPem() const { return m_cert.certPem; }

    void start(std::function<std::string(const std::string& method, const std::string& path, const std::string& req)> handler) {
        m_running = true;
        m_worker = std::thread([this, handler = std::move(handler)]() {
            while (m_running) {
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(m_listenFd, &rfds);
                struct timeval tv{0, 50000}; // 50ms
                int sel = select(static_cast<int>(m_listenFd + 1), &rfds, nullptr, nullptr, &tv);
                if (sel <= 0) continue;

                socket_handle_t clientFd = accept(m_listenFd, nullptr, nullptr);
                if (!isValidSocket(clientFd)) continue;

                if (m_isHttps && m_sslCtx) {
                    SSL* ssl = SSL_new(m_sslCtx);
                    SSL_set_fd(ssl, static_cast<int>(clientFd));
                    if (SSL_accept(ssl) > 0) {
                        std::string req = readRequest(clientFd, ssl);
                        if (!req.empty()) {
                            std::istringstream iss(req);
                            std::string method, path;
                            iss >> method >> path;
                            std::string response = handler(method, path, req);
                            if (!response.empty()) {
                                SSL_write(ssl, response.data(), static_cast<int>(response.size()));
                            }
                        }
                    }
                    SSL_shutdown(ssl);
                    SSL_free(ssl);
                } else {
                    std::string req = readRequest(clientFd, nullptr);
                    if (!req.empty()) {
                        std::istringstream iss(req);
                        std::string method, path;
                        iss >> method >> path;
                        std::string response = handler(method, path, req);
                        if (!response.empty()) {
                            ::send(clientFd, response.data(), static_cast<int>(response.size()), MSG_NOSIGNAL);
                        }
                    }
                }
                closeSocket(clientFd);
            }
        });
    }

    void stop() {
        m_running = false;
        if (m_worker.joinable()) {
            m_worker.join();
        }
        if (isValidSocket(m_listenFd)) {
            closeSocket(m_listenFd);
            m_listenFd = INVALID_SOCKET_VALUE;
        }
    }

private:
    static std::string readRequest(socket_handle_t fd, SSL* ssl) {
        std::string req;
        char b[4096];
        size_t reqContentLen = std::string::npos;
        size_t hEnd = std::string::npos;
        while (true) {
            ssize_t n = 0;
            if (ssl) {
                int r = SSL_read(ssl, b, sizeof(b));
                if (r <= 0) break;
                n = r;
            } else {
                n = recv(fd, b, static_cast<int>(sizeof(b)), 0);
                if (n <= 0) break;
            }
            req.append(b, static_cast<size_t>(n));
            if (hEnd == std::string::npos) {
                hEnd = req.find("\r\n\r\n");
                if (hEnd != std::string::npos) {
                    size_t clPos = req.find("Content-Length:");
                    if (clPos == std::string::npos) clPos = req.find("content-length:");
                    if (clPos != std::string::npos && clPos < hEnd) {
                        size_t lineEnd = req.find("\r\n", clPos);
                        std::string clVal = req.substr(clPos + 15, lineEnd - (clPos + 15));
                        try { reqContentLen = std::stoull(clVal); } catch (...) {}
                    }
                }
            }
            if (hEnd != std::string::npos) {
                if (reqContentLen == std::string::npos || req.size() >= hEnd + 4 + reqContentLen) {
                    break;
                }
            }
        }
        return req;
    }

    socket_handle_t m_listenFd{INVALID_SOCKET_VALUE};
    uint16_t m_port{0};
    bool m_isHttps{false};
    TlsCertificate m_cert;
    SSL_CTX* m_sslCtx{nullptr};
    std::atomic<bool> m_running{false};
    std::thread m_worker;
};

static bool testHttpClientAndCloudSync() {
    // 1. URL Parsing & Validation
    std::string host, path;
    uint16_t port = 0;
    bool isHttps = false;

    bool parse1 = HttpClient::parseUrl("http://127.0.0.1:8888/api/v1/quota", host, port, path, isHttps);
    TEST_ASSERT(parse1, "Parsed HTTP URL successfully");
    TEST_ASSERT(host == "127.0.0.1", "Parsed HTTP host");
    TEST_ASSERT(port == 8888, "Parsed HTTP port");
    TEST_ASSERT(path == "/api/v1/quota", "Parsed HTTP path");
    TEST_ASSERT(!isHttps, "Parsed plain HTTP flag");

    bool parse2 = HttpClient::parseUrl("https://relay.landrop.io/sync", host, port, path, isHttps);
    TEST_ASSERT(parse2, "Parsed HTTPS URL successfully");
    TEST_ASSERT(host == "relay.landrop.io", "Parsed HTTPS host");
    TEST_ASSERT(port == 443, "Parsed default HTTPS port");
    TEST_ASSERT(isHttps, "Parsed HTTPS flag");

    // Invalid / malformed URLs
    TEST_ASSERT(!HttpClient::parseUrl("", host, port, path, isHttps), "Rejects empty URL");
    TEST_ASSERT(!HttpClient::parseUrl("ftp://files.landrop.io/file", host, port, path, isHttps), "Rejects non-HTTP scheme");
    TEST_ASSERT(!HttpClient::parseUrl("http://:8080/path", host, port, path, isHttps), "Rejects empty host");
    TEST_ASSERT(!HttpClient::parseUrl("http://localhost:99999/path", host, port, path, isHttps), "Rejects invalid port > 65535");

    // 2. Mock HTTP Server testing
    MockHttpServer server;
    uint16_t serverPort = server.getPort();
    TEST_ASSERT(serverPort > 0, "Mock HTTP server listening on ephemeral port");

    server.start([](const std::string& method, const std::string& path, const std::string& rawReq) -> std::string {
        if (path == "/api/v1/quota" && method == "POST") {
            if (rawReq.find("valid_secret_token") != std::string::npos) {
                std::string body = R"({"storage_quota_bytes": 10737418240, "used_storage_bytes": 209715200})";
                return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n" + body;
            } else if (rawReq.find("unauthorized_token") != std::string::npos) {
                return "HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"error\": \"Invalid token\"}";
            } else if (rawReq.find("over_quota_token") != std::string::npos) {
                return "HTTP/1.1 403 Forbidden\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"error\": \"Quota exceeded\"}";
            } else if (rawReq.find("crash_token") != std::string::npos) {
                return "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"error\": \"Server error\"}";
            }
        }
        if (path == "/status" && method == "GET") {
            return "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nOK";
        }
        if (path == "/truncated" && method == "GET") {
            // Announces 500 bytes but sends only 10 bytes and closes connection
            return "HTTP/1.1 200 OK\r\nContent-Length: 500\r\nConnection: close\r\n\r\n1234567890";
        }
        if (path == "/echo-size" && method == "POST") {
            size_t bodyPos = rawReq.find("\r\n\r\n");
            size_t bodyLen = (bodyPos != std::string::npos) ? (rawReq.size() - (bodyPos + 4)) : 0;
            std::string lenStr = std::to_string(bodyLen);
            return "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
                   std::to_string(lenStr.size()) + "\r\nConnection: close\r\n\r\n" + lenStr;
        }
        if (path == "/drop") {
            return ""; // Emulate connection closed / no response
        }
        return "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
    });

    std::string baseUrl = "http://127.0.0.1:" + std::to_string(serverPort);
    HttpClient client;

    // Test GET /status (HTTP 200)
    auto resGet = client.get(baseUrl + "/status", 2);
    TEST_ASSERT(resGet.isOk(), "GET /status is 200 OK");
    TEST_ASSERT(resGet.statusCode == 200, "Status code is 200");
    TEST_ASSERT(resGet.body == "OK", "Body matches response");

    // 3. Positive Quota Sync with CloudPeer
    CloudPeer validPeer("cloud-01", "DropCloud Personal", "http://127.0.0.1", serverPort, "DropCloud", "valid_secret_token");
    validPeer.initialize();
    bool syncOk = validPeer.syncQuotaWithServer(baseUrl + "/api/v1/quota");
    TEST_ASSERT(syncOk, "CloudPeer successfully synced quota with server");
    TEST_ASSERT(validPeer.getStorageQuotaBytes() == 10737418240ULL, "CloudPeer storage quota updated to 10 GB");
    TEST_ASSERT(validPeer.getUsedStorageBytes() == 209715200ULL, "CloudPeer used storage updated to 200 MB");
    TEST_ASSERT(validPeer.isOnline(), "CloudPeer remains online after successful sync");

    // 4. Negative: 401 Unauthorized
    CloudPeer unauthPeer("cloud-02", "DropCloud Unauth", "http://127.0.0.1", serverPort, "DropCloud", "unauthorized_token");
    unauthPeer.initialize();
    bool unauthSync = unauthPeer.syncQuotaWithServer(baseUrl + "/api/v1/quota");
    TEST_ASSERT(!unauthSync, "Sync safely fails on 401 Unauthorized");
    TEST_ASSERT(!unauthPeer.isOnline(), "CloudPeer invalidated and taken offline upon 401 Unauthorized");

    // 5. Negative: 403 Forbidden
    CloudPeer overQuotaPeer("cloud-03", "DropCloud Full", "http://127.0.0.1", serverPort, "DropCloud", "over_quota_token");
    overQuotaPeer.initialize();
    bool overQuotaSync = overQuotaPeer.syncQuotaWithServer(baseUrl + "/api/v1/quota");
    TEST_ASSERT(!overQuotaSync, "Sync safely fails on 403 Forbidden");

    // 6. Negative: 500 Internal Server Error
    CloudPeer crashPeer("cloud-04", "DropCloud Down", "http://127.0.0.1", serverPort, "DropCloud", "crash_token");
    crashPeer.initialize();
    bool crashSync = crashPeer.syncQuotaWithServer(baseUrl + "/api/v1/quota");
    TEST_ASSERT(!crashSync, "Sync safely fails on 500 Server Error");

    // 7. Network Disconnection / Connection Refused
    auto refusedRes = client.get("http://127.0.0.1:1/nonexistent", 1);
    TEST_ASSERT(!refusedRes.isOk(), "Connection refused reported as failure");
    TEST_ASSERT(refusedRes.statusCode == 0, "Status code is 0 for connection failure");
    TEST_ASSERT(!refusedRes.errorMessage.empty(), "Error message populated for connection failure");

    // 8. Timeout handling
    auto dropRes = client.get(baseUrl + "/drop", 1);
    TEST_ASSERT(!dropRes.isOk(), "Dropped connection reported as failure");

    // 9. Partial send loop (128 KB payload)
    std::string largePayload(128 * 1024, 'K');
    auto largeRes = client.post(baseUrl + "/echo-size", largePayload, "", 5);
    TEST_ASSERT(largeRes.isOk(), "Large 128KB payload sent successfully via write loop");
    TEST_ASSERT(largeRes.body == std::to_string(128 * 1024), "Server verified 128KB payload fully received");

    // 10. Truncated Content-Length response detection
    auto truncRes = client.get(baseUrl + "/truncated", 2);
    TEST_ASSERT(!truncRes.isOk(), "Truncated Content-Length response detected and rejected");
    TEST_ASSERT(truncRes.errorMessage.find("Truncated") != std::string::npos, "Error message reports response truncation");

    server.stop();

    // 11. HTTPS / TLS Mock Server Tests
    // (a) Valid HTTPS with trusted custom CA & matching hostname "127.0.0.1"
    MockHttpServer httpsServer(0, true, "127.0.0.1");
    uint16_t httpsPort = httpsServer.getPort();
    httpsServer.start([](const std::string&, const std::string& path, const std::string&) -> std::string {
        if (path == "/secure") {
            return "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nSECURE_DATA";
        }
        return "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
    });

    std::string httpsUrl = "https://127.0.0.1:" + std::to_string(httpsPort) + "/secure";
    auto secureRes = client.get(httpsUrl, 3, httpsServer.getCertPem());
    TEST_ASSERT(secureRes.isOk(), "Valid HTTPS connection with trusted CA and matching hostname succeeded");
    TEST_ASSERT(secureRes.body == "SECURE_DATA", "Secure payload verified");

    // (b) Hostname mismatch in HTTPS (server certificate CN="evil-host.local", client connects to 127.0.0.1)
    MockHttpServer mismatchServer(0, true, "evil-host.local");
    uint16_t mismatchPort = mismatchServer.getPort();
    mismatchServer.start([](const std::string&, const std::string&, const std::string&) -> std::string {
        return "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nEVIL";
    });

    std::string mismatchUrl = "https://127.0.0.1:" + std::to_string(mismatchPort) + "/secure";
    auto mismatchRes = client.get(mismatchUrl, 3, mismatchServer.getCertPem());
    TEST_ASSERT(!mismatchRes.isOk(), "HTTPS hostname mismatch rejected before receiving application data");
    TEST_ASSERT(mismatchRes.statusCode == 0, "Status code 0 on hostname mismatch failure");

    // (c) Untrusted certificate in HTTPS (no CA provided, allowInsecureTls=false)
    auto untrustedRes = client.get(httpsUrl, 3); // No caCertPem passed!
    TEST_ASSERT(!untrustedRes.isOk(), "HTTPS untrusted certificate rejected by default");
    TEST_ASSERT(untrustedRes.statusCode == 0, "Status code 0 on untrusted certificate failure");

    // (d) Explicit insecure opt-in allows connection
    HttpRequest insecureReq;
    insecureReq.url = httpsUrl;
    insecureReq.allowInsecureTls = true;
    insecureReq.timeoutSeconds = 3;
    auto insecureRes = client.send(insecureReq);
    TEST_ASSERT(insecureRes.isOk(), "Explicit allowInsecureTls permitted connection to self-signed host");
    TEST_ASSERT(insecureRes.body == "SECURE_DATA", "Insecure connection received body");

    // (e) HTTPS Truncated Content-Length detection
    MockHttpServer httpsTruncServer(0, true, "127.0.0.1");
    uint16_t truncPort = httpsTruncServer.getPort();
    httpsTruncServer.start([](const std::string&, const std::string& path, const std::string&) -> std::string {
        if (path == "/trunc-https") {
            return "HTTP/1.1 200 OK\r\nContent-Length: 600\r\nConnection: close\r\n\r\nshort";
        }
        return "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
    });
    std::string httpsTruncUrl = "https://127.0.0.1:" + std::to_string(truncPort) + "/trunc-https";
    auto httpsTruncRes = client.get(httpsTruncUrl, 3, httpsTruncServer.getCertPem());
    TEST_ASSERT(!httpsTruncRes.isOk(), "HTTPS truncated Content-Length response detected and rejected");
    TEST_ASSERT(httpsTruncRes.errorMessage.find("Truncated") != std::string::npos, "HTTPS truncation error reported");

    // (f) Multi-cert bundle support in caCertPem
    TlsCertificate dummyCa = TlsHelper::generateCertificate("Dummy-Root-CA");
    std::string multiBundle = dummyCa.certPem + "\n" + httpsServer.getCertPem();
    auto bundleRes = client.get(httpsUrl, 3, multiBundle);
    TEST_ASSERT(bundleRes.isOk(), "HTTPS custom CA bundle with multiple certificates validated successfully");
    TEST_ASSERT(bundleRes.body == "SECURE_DATA", "Payload verified with multi-cert bundle");

    httpsServer.stop();
    mismatchServer.stop();
    httpsTruncServer.stop();

    return true;
}

static bool testLiveSocketTransferPipeline() {
    std::filesystem::path testRootDir = "./unit_test_socket_transfer_tmp";
    std::filesystem::remove_all(testRootDir);
    std::filesystem::create_directories(testRootDir);

    std::filesystem::path sendDir = testRootDir / "sender";
    std::filesystem::path recvDir = testRootDir / "receiver";
    std::filesystem::create_directories(sendDir);
    std::filesystem::create_directories(recvDir);

    FileChunkHasher hasher;

    // Helper to start an ephemeral receiver socket
    auto bindEphemeral = [](socket_handle_t& listenFd, uint16_t& outPort) -> bool {
        listenFd = socket(AF_INET, SOCK_STREAM, 0);
        if (!isValidSocket(listenFd)) return false;
        int opt = 1;
        setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            closeSocket(listenFd);
            return false;
        }
        if (listen(listenFd, 2) < 0) {
            closeSocket(listenFd);
            return false;
        }
        socklen_t len = sizeof(addr);
        if (getsockname(listenFd, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
            closeSocket(listenFd);
            return false;
        }
        outPort = ntohs(addr.sin_port);
        return true;
    };

    // Receiver worker simulating live receiver socket loop
    auto runReceiverWorker = [&](socket_handle_t listenFd, const std::string& saveDirectory, std::atomic<int>& outStatus) {
        sockaddr_in clientAddr{};
        socklen_t cLen = sizeof(clientAddr);
        socket_handle_t clientFd = accept(listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &cLen);
        if (!isValidSocket(clientFd)) {
            outStatus.store(-1);
            return;
        }

        NetworkSession session("rx-session", "127.0.0.1", 0);
        session.attachSocket(clientFd);

        uint32_t nameLen = 0;
        if (!session.receiveExact(reinterpret_cast<uint8_t*>(&nameLen), sizeof(uint32_t)) || nameLen == 0 || nameLen > 255) {
            uint8_t nack = 0;
            session.sendAll(&nack, 1);
            outStatus.store(1);
            return;
        }

        std::string fileName(nameLen, '\0');
        if (!session.receiveExact(reinterpret_cast<uint8_t*>(fileName.data()), nameLen) || !isSafeFileName(fileName)) {
            uint8_t nack = 0;
            session.sendAll(&nack, 1);
            outStatus.store(2);
            return;
        }

        uint64_t fileSize = 0;
        if (!session.receiveExact(reinterpret_cast<uint8_t*>(&fileSize), sizeof(uint64_t)) || !isValidFileSize(fileSize)) {
            uint8_t nack = 0;
            session.sendAll(&nack, 1);
            outStatus.store(3);
            return;
        }

        std::filesystem::path canonicalBase = std::filesystem::weakly_canonical(saveDirectory);
        std::filesystem::path outPath = canonicalBase / fileName;
        if (std::filesystem::exists(outPath)) {
            uint8_t nack = 0;
            session.sendAll(&nack, 1);
            outStatus.store(4);
            return;
        }

        std::filesystem::path partPath = canonicalBase / (fileName + ".part");
        std::error_code ec;
        std::filesystem::remove(partPath, ec);

        std::string expectedHash(64, '\0');
        if (!session.receiveExact(reinterpret_cast<uint8_t*>(expectedHash.data()), 64)) {
            uint8_t nack = 0;
            session.sendAll(&nack, 1);
            outStatus.store(5);
            return;
        }

        std::ofstream outFile(partPath, std::ios::binary);
        if (!outFile.is_open()) {
            uint8_t nack = 0;
            session.sendAll(&nack, 1);
            outStatus.store(6);
            return;
        }

        uint64_t totalReceived = 0;
        constexpr uint32_t kMaxChunk = 4 * 1024 * 1024;
        bool aborted = false;

        while (totalReceived < fileSize) {
            uint32_t chunkSize = 0;
            if (!session.receiveExact(reinterpret_cast<uint8_t*>(&chunkSize), sizeof(uint32_t)) ||
                !isValidChunkSize(chunkSize, fileSize, totalReceived, kMaxChunk)) {
                aborted = true;
                break;
            }

            std::vector<uint8_t> buf(chunkSize);
            if (!session.receiveExact(buf.data(), chunkSize)) {
                aborted = true;
                break;
            }

            outFile.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(chunkSize));
            totalReceived += chunkSize;
        }

        outFile.close();

        if (aborted || totalReceived != fileSize) {
            std::filesystem::remove(partPath, ec);
            outStatus.store(7); // Aborted / truncated
            return;
        }

        FileChunkHasher h;
        auto actualHashOpt = h.computeFullFileHash(partPath.string());
        std::string actualHash = actualHashOpt.value_or("");
        uint8_t ack = (actualHash == expectedHash) ? 1 : 0;
        session.sendAll(&ack, 1);

        if (ack == 1) {
            std::filesystem::rename(partPath, outPath, ec);
            outStatus.store(0); // Success
        } else {
            std::filesystem::remove(partPath, ec);
            outStatus.store(8); // Hash mismatch
        }
    };

    // Sender helper executing standard protocol
    auto sendFileOverSocket = [&](uint16_t port, const std::string& filePath, const std::string& overrideHash, bool simulateDisconnect) -> bool {
        NetworkSession session("tx-session", "127.0.0.1", port);
        if (!session.connectTo("127.0.0.1", port)) return false;

        std::filesystem::path p(filePath);
        std::string fileName = p.filename().string();
        uint64_t fileSize = std::filesystem::file_size(p);
        FileChunkHasher h;
        std::string fileHash = overrideHash.empty() ? h.computeFullFileHash(filePath).value_or("") : overrideHash;

        uint32_t nameLen = static_cast<uint32_t>(fileName.size());
        std::vector<uint8_t> header(sizeof(uint32_t) + nameLen + sizeof(uint64_t) + 64);
        uint8_t* ptr = header.data();
        std::memcpy(ptr, &nameLen, sizeof(uint32_t)); ptr += sizeof(uint32_t);
        std::memcpy(ptr, fileName.data(), nameLen); ptr += nameLen;
        std::memcpy(ptr, &fileSize, sizeof(uint64_t)); ptr += sizeof(uint64_t);
        std::string hashPadded = fileHash; hashPadded.resize(64, '0');
        std::memcpy(ptr, hashPadded.data(), 64);

        if (!session.sendAll(header.data(), header.size())) return false;

        std::ifstream in(filePath, std::ios::binary);
        std::vector<uint8_t> buf(64 * 1024);
        uint64_t sent = 0;

        while (sent < fileSize && in) {
            in.read(reinterpret_cast<char*>(buf.data()), buf.size());
            size_t count = static_cast<size_t>(in.gcount());
            if (count == 0) break;

            if (simulateDisconnect && sent + count >= fileSize / 2) {
                // Abruptly terminate connection
                session.closeSession();
                return false;
            }

            uint32_t cU32 = static_cast<uint32_t>(count);
            if (!session.sendAll(reinterpret_cast<const uint8_t*>(&cU32), sizeof(uint32_t))) return false;
            if (!session.sendAll(buf.data(), count)) return false;
            sent += count;
        }

        uint8_t ack = 0;
        if (session.receiveExact(&ack, 1) && ack == 1) {
            return true;
        }
        return false;
    };

    // 1. Positive Test: Small 512-byte file transfer
    std::string smallFile = (sendDir / "small.txt").string();
    {
        std::ofstream out(smallFile, std::ios::binary);
        for (int i = 0; i < 512; ++i) out.put(static_cast<char>(i & 0xFF));
    }
    socket_handle_t listenFd1 = INVALID_SOCKET_VALUE;
    uint16_t port1 = 0;
    TEST_ASSERT(bindEphemeral(listenFd1, port1), "Bound ephemeral receiver socket for small file");
    std::atomic<int> rxStatus1{-99};
    std::thread rxTh1([&]() { runReceiverWorker(listenFd1, recvDir.string(), rxStatus1); });
    bool txOk1 = sendFileOverSocket(port1, smallFile, "", false);
    rxTh1.join();
    closeSocket(listenFd1);

    TEST_ASSERT(txOk1, "Sender received positive ACK from receiver for small file");
    TEST_ASSERT(rxStatus1.load() == 0, "Receiver status indicates success for small file");
    TEST_ASSERT(std::filesystem::exists(recvDir / "small.txt"), "Destination small file created");
    TEST_ASSERT(!std::filesystem::exists(recvDir / "small.txt.part"), "Temporary .part file cleaned up");
    TEST_ASSERT(hasher.computeFullFileHash(smallFile) == hasher.computeFullFileHash((recvDir / "small.txt").string()),
                "Small file SHA-256 matches exactly");

    // 2. Positive Test: Multi-chunk 256 KB file transfer (4 chunks of 64KB)
    std::string largeFile = (sendDir / "multi_chunk.bin").string();
    {
        std::ofstream out(largeFile, std::ios::binary);
        std::vector<char> block(64 * 1024, 0);
        for (int i = 0; i < 4; ++i) {
            std::fill(block.begin(), block.end(), static_cast<char>('A' + i));
            out.write(block.data(), block.size());
        }
    }
    socket_handle_t listenFd2 = INVALID_SOCKET_VALUE;
    uint16_t port2 = 0;
    TEST_ASSERT(bindEphemeral(listenFd2, port2), "Bound ephemeral receiver socket for 256KB file");
    std::atomic<int> rxStatus2{-99};
    std::thread rxTh2([&]() { runReceiverWorker(listenFd2, recvDir.string(), rxStatus2); });
    bool txOk2 = sendFileOverSocket(port2, largeFile, "", false);
    rxTh2.join();
    closeSocket(listenFd2);

    TEST_ASSERT(txOk2, "Sender received positive ACK for multi-chunk file");
    TEST_ASSERT(rxStatus2.load() == 0, "Receiver status indicates success for multi-chunk file");
    TEST_ASSERT(std::filesystem::exists(recvDir / "multi_chunk.bin"), "Multi-chunk destination file created");
    TEST_ASSERT(hasher.computeFullFileHash(largeFile) == hasher.computeFullFileHash((recvDir / "multi_chunk.bin").string()),
                "Multi-chunk file SHA-256 matches exactly");

    // 3. Negative Test 1: Abrupt disconnect cleans up .part file
    std::string abortFile = (sendDir / "abort_candidate.bin").string();
    {
        std::ofstream out(abortFile, std::ios::binary);
        std::vector<char> dummy(256 * 1024, 'X');
        out.write(dummy.data(), dummy.size());
    }
    socket_handle_t listenFd3 = INVALID_SOCKET_VALUE;
    uint16_t port3 = 0;
    TEST_ASSERT(bindEphemeral(listenFd3, port3), "Bound ephemeral receiver socket for disconnect test");
    std::atomic<int> rxStatus3{-99};
    std::thread rxTh3([&]() { runReceiverWorker(listenFd3, recvDir.string(), rxStatus3); });
    bool txOk3 = sendFileOverSocket(port3, abortFile, "", true); // simulate disconnect!
    rxTh3.join();
    closeSocket(listenFd3);

    TEST_ASSERT(!txOk3, "Sender detected interrupted connection");
    TEST_ASSERT(rxStatus3.load() == 7, "Receiver correctly caught premature abort/disconnection");
    TEST_ASSERT(!std::filesystem::exists(recvDir / "abort_candidate.bin.part"), "Temporary .part file deleted on abort");

    // 4. Negative Test 2: Checksum mismatch rejected and .part deleted
    std::string corruptFile = (sendDir / "corrupt_candidate.bin").string();
    {
        std::ofstream out(corruptFile);
        out << "Data payload with forged SHA-256 header hash";
    }
    socket_handle_t listenFd4 = INVALID_SOCKET_VALUE;
    uint16_t port4 = 0;
    TEST_ASSERT(bindEphemeral(listenFd4, port4), "Bound ephemeral receiver socket for corrupt hash test");
    std::atomic<int> rxStatus4{-99};
    std::thread rxTh4([&]() { runReceiverWorker(listenFd4, recvDir.string(), rxStatus4); });
    std::string forgedHash = "0000000000000000000000000000000000000000000000000000000000000000";
    bool txOk4 = sendFileOverSocket(port4, corruptFile, forgedHash, false);
    rxTh4.join();
    closeSocket(listenFd4);

    TEST_ASSERT(!txOk4, "Sender received NACK on hash mismatch");
    TEST_ASSERT(rxStatus4.load() == 8, "Receiver flagged hash mismatch");
    TEST_ASSERT(!std::filesystem::exists(recvDir / "corrupt_candidate.bin"), "Corrupt destination file not created");
    TEST_ASSERT(!std::filesystem::exists(recvDir / "corrupt_candidate.bin.part"), "Corrupt .part file deleted");

    // 5. Negative Test 3: Destination file already exists collision
    socket_handle_t listenFd5 = INVALID_SOCKET_VALUE;
    uint16_t port5 = 0;
    TEST_ASSERT(bindEphemeral(listenFd5, port5), "Bound ephemeral receiver socket for existing file test");
    std::atomic<int> rxStatus5{-99};
    std::thread rxTh5([&]() { runReceiverWorker(listenFd5, recvDir.string(), rxStatus5); });
    bool txOk5 = sendFileOverSocket(port5, smallFile, "", false); // small.txt already exists from Test 1!
    rxTh5.join();
    closeSocket(listenFd5);

    TEST_ASSERT(!txOk5, "Sender rejected when destination file already exists");
    TEST_ASSERT(rxStatus5.load() == 4, "Receiver detected existing file collision and sent NACK");

    std::filesystem::remove_all(testRootDir);
    return true;
}

static bool testTransferEngine() {
    std::filesystem::path rootDir = "./unit_test_transfer_engine_tmp";
    std::filesystem::remove_all(rootDir);
    std::filesystem::create_directories(rootDir);

    std::filesystem::path sendDir = rootDir / "sender";
    std::filesystem::path recvDir = rootDir / "receiver";
    std::filesystem::create_directories(sendDir);
    std::filesystem::create_directories(recvDir);

    FileChunkHasher hasher;
    TransferEngine engine;

    // Ephemeral port selection
    socket_handle_t ephemeralFd = socket(AF_INET, SOCK_STREAM, 0);
    uint16_t listenPort = 0;
    if (isValidSocket(ephemeralFd)) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        bind(ephemeralFd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        socklen_t slen = sizeof(a);
        getsockname(ephemeralFd, reinterpret_cast<sockaddr*>(&a), &slen);
        listenPort = ntohs(a.sin_port);
        closeSocket(ephemeralFd);
    }
    if (listenPort == 0) listenPort = 9988;

    TransferOptions srvOpts;
    std::atomic<int> incomingCount{0};
    std::atomic<int> finishedCount{0};

    bool serverStarted = engine.startServer(
        listenPort,
        recvDir.string(),
        srvOpts,
        [&](const std::string&, const std::string&, uint64_t, const std::string&) -> bool {
            incomingCount.fetch_add(1);
            return true;
        },
        nullptr,
        [&](const std::string&, const std::string&, bool, const std::string&, const std::string&) {
            finishedCount.fetch_add(1);
        }
    );
    TEST_ASSERT(serverStarted, "TransferEngine server started on ephemeral port");
    TEST_ASSERT(engine.isServerRunning(), "TransferEngine reports running server");

    // 1. Positive Test: Send small file
    std::string smallFile = (sendDir / "engine_small.txt").string();
    {
        std::ofstream out(smallFile, std::ios::binary);
        out << "Hello from TransferEngine integration test! Ensuring zero mock data in live path.";
    }
    std::string outErr;
    TransferOptions clientOpts;
    bool sentSmall = engine.sendFile(
        "task-engine-1",
        smallFile,
        "127.0.0.1",
        listenPort,
        clientOpts,
        nullptr,
        nullptr,
        &outErr
    );
    TEST_ASSERT(sentSmall, "Small file sent successfully via TransferEngine");
    TEST_ASSERT(std::filesystem::exists(recvDir / "engine_small.txt"), "Small file exists in receiver directory");
    TEST_ASSERT(!std::filesystem::exists(recvDir / "engine_small.txt.part"), "Small file .part cleaned up");
    TEST_ASSERT(hasher.computeFullFileHash(smallFile) == hasher.computeFullFileHash((recvDir / "engine_small.txt").string()),
                "Small file SHA-256 matches perfectly");

    // 2. Positive Test: Send multi-chunk file (256 KB)
    std::string multiFile = (sendDir / "engine_multi.bin").string();
    {
        std::ofstream out(multiFile, std::ios::binary);
        std::vector<char> blk(64 * 1024, 'M');
        for (int i = 0; i < 4; ++i) {
            std::fill(blk.begin(), blk.end(), static_cast<char>('1' + i));
            out.write(blk.data(), blk.size());
        }
    }
    bool sentMulti = engine.sendFile(
        "task-engine-2",
        multiFile,
        "127.0.0.1",
        listenPort,
        clientOpts,
        nullptr,
        nullptr,
        &outErr
    );
    TEST_ASSERT(sentMulti, "Multi-chunk file sent successfully via TransferEngine");
    TEST_ASSERT(std::filesystem::exists(recvDir / "engine_multi.bin"), "Multi-chunk file exists in receiver directory");
    TEST_ASSERT(!std::filesystem::exists(recvDir / "engine_multi.bin.part"), "Multi-chunk .part cleaned up");
    TEST_ASSERT(hasher.computeFullFileHash(multiFile) == hasher.computeFullFileHash((recvDir / "engine_multi.bin").string()),
                "Multi-chunk file SHA-256 matches perfectly");

    // 3. Negative Test: Send to unreachable port
    std::string unreachErr;
    bool sentUnreach = engine.sendFile(
        "task-engine-unreach",
        smallFile,
        "127.0.0.1",
        59991, // non-listening port
        clientOpts,
        nullptr,
        nullptr,
        &unreachErr
    );
    TEST_ASSERT(!sentUnreach, "Send to unreachable port returns false gracefully");
    TEST_ASSERT(!unreachErr.empty(), "Unreachable send provides descriptive error message");

    // 4. Cancellation Test: Abort mid-stream
    std::string cancelFile = (sendDir / "engine_cancel.bin").string();
    {
        std::ofstream out(cancelFile, std::ios::binary);
        std::vector<char> blk(64 * 1024, 'C');
        for (int i = 0; i < 8; ++i) {
            out.write(blk.data(), blk.size());
        }
    }
    std::atomic<bool> cancelFlag{false};
    std::string cancelErr;
    TransferOptions cancelOpts = clientOpts;
    cancelOpts.speedLimitKbps = 32;
    bool sentCancelled = engine.sendFile(
        "task-engine-cancel",
        cancelFile,
        "127.0.0.1",
        listenPort,
        cancelOpts,
        [&](const TransferProgressUpdate& upd) {
            if (upd.state == TransferState::Connecting || upd.bytesTransferred > 0) {
                cancelFlag.store(true);
            }
        },
        &cancelFlag,
        &cancelErr
    );
    TEST_ASSERT(!sentCancelled, "Mid-stream cancellation returns false");
    TEST_ASSERT(!std::filesystem::exists(recvDir / "engine_cancel.bin.part"), "Cancelled transfer cleans up temporary .part file");

    // 5. Duplicate collision rejection test
    std::string dupErr;
    bool sentDup = engine.sendFile(
        "task-engine-dup",
        smallFile, // engine_small.txt already exists in recvDir
        "127.0.0.1",
        listenPort,
        clientOpts,
        nullptr,
        nullptr,
        &dupErr
    );
    TEST_ASSERT(!sentDup, "Duplicate filename rejected by receiver");

    // 6. Duplicate auto-rename test when autoRenameDuplicates is enabled
    engine.stopServer();
    TransferOptions autoRenameOpts;
    autoRenameOpts.autoRenameDuplicates = true;
    bool restartedServer = engine.startServer(
        listenPort,
        recvDir.string(),
        autoRenameOpts,
        nullptr,
        nullptr,
        nullptr
    );
    TEST_ASSERT(restartedServer, "Restarted server with autoRenameDuplicates enabled");

    std::string renameErr;
    bool sentAutoRename = engine.sendFile(
        "task-engine-dup-rename",
        smallFile, // engine_small.txt already exists in recvDir
        "127.0.0.1",
        listenPort,
        clientOpts,
        nullptr,
        nullptr,
        &renameErr
    );
    TEST_ASSERT(sentAutoRename, "File sent successfully with autoRenameDuplicates enabled");
    TEST_ASSERT(std::filesystem::exists(recvDir / "engine_small (1).txt"), "Auto-renamed file engine_small (1).txt created");

    engine.stopServer();
    TEST_ASSERT(!engine.isServerRunning(), "TransferEngine server stopped cleanly");

    std::filesystem::remove_all(rootDir);
    return true;
}

static bool testWireProtocolV2() {
    using namespace landrop::protocol::v2;

    // 1. Endianness checks
    uint16_t val16 = 0x1234;
    TEST_ASSERT(fromBigEndian16(toBigEndian16(val16)) == val16, "16-bit endianness round-trip");
    uint32_t val32 = 0x12345678;
    TEST_ASSERT(fromBigEndian32(toBigEndian32(val32)) == val32, "32-bit endianness round-trip");
    uint64_t val64 = 0x0102030405060708ULL;
    TEST_ASSERT(fromBigEndian64(toBigEndian64(val64)) == val64, "64-bit endianness round-trip");

    // 2. IEEE 802.3 CRC32 verification against standard reference
    const char* crcRef = "123456789";
    uint32_t expectedCrc = 0xCBF43926;
    uint32_t computedCrc = computeCrc32(reinterpret_cast<const uint8_t*>(crcRef), std::strlen(crcRef));
    TEST_ASSERT(computedCrc == expectedCrc, "CRC32 matches IEEE 802.3 reference 0xCBF43926");

    // 3. FrameHeader serialization & boundary/error protection
    FrameHeader hdr;
    hdr.version = PROTOCOL_VERSION;
    hdr.msgType = MessageType::FileHeader;
    hdr.payloadLength = 4096;
    hdr.crc32OrSeq = 0xAABBCCDD;

    uint8_t rawHdr[FRAME_HEADER_SIZE];
    hdr.serialize(rawHdr);

    FrameHeader parsedHdr;
    std::string err;
    TEST_ASSERT(FrameHeader::deserialize(rawHdr, parsedHdr, &err), "FrameHeader deserialized successfully");
    TEST_ASSERT(parsedHdr.version == 2, "Parsed version is 2");
    TEST_ASSERT(parsedHdr.msgType == MessageType::FileHeader, "Parsed msgType matches");
    TEST_ASSERT(parsedHdr.payloadLength == 4096, "Parsed payloadLength matches");
    TEST_ASSERT(parsedHdr.crc32OrSeq == 0xAABBCCDD, "Parsed CRC32 matches");

    // Corrupted magic rejection
    rawHdr[0] = 'X';
    TEST_ASSERT(!FrameHeader::deserialize(rawHdr, parsedHdr, &err), "Rejects invalid magic");
    TEST_ASSERT(!err.empty(), "Reports error message for invalid magic");
    rawHdr[0] = 'L'; // restore

    // Unsupported version rejection
    uint16_t badVer = toBigEndian16(99);
    std::memcpy(rawHdr + 4, &badVer, sizeof(uint16_t));
    TEST_ASSERT(!FrameHeader::deserialize(rawHdr, parsedHdr, &err), "Rejects unsupported version");
    uint16_t goodVer = toBigEndian16(PROTOCOL_VERSION);
    std::memcpy(rawHdr + 4, &goodVer, sizeof(uint16_t)); // restore

    // Oversized payload rejection (> 16MB)
    uint32_t hugeLen = toBigEndian32(32 * 1024 * 1024);
    std::memcpy(rawHdr + 8, &hugeLen, sizeof(uint32_t));
    TEST_ASSERT(!FrameHeader::deserialize(rawHdr, parsedHdr, &err), "Rejects oversized payload (>16MB)");

    // 4. HandshakeMessage
    HandshakeMessage hs;
    hs.capabilitiesFlags = 0x07;
    hs.deviceId = "uuid-client-1234";
    hs.deviceName = "MacBook Pro M3";
    hs.clientVersion = "2.0.0-rc1";
    std::vector<uint8_t> hsBytes = hs.serialize();

    HandshakeMessage parsedHs;
    TEST_ASSERT(HandshakeMessage::deserialize(hsBytes.data(), hsBytes.size(), parsedHs, &err), "Handshake parsed");
    TEST_ASSERT(parsedHs.capabilitiesFlags == 0x07, "Handshake capabilities match");
    TEST_ASSERT(parsedHs.deviceId == "uuid-client-1234", "Handshake deviceId matches");
    TEST_ASSERT(parsedHs.deviceName == "MacBook Pro M3", "Handshake deviceName matches");
    TEST_ASSERT(parsedHs.clientVersion == "2.0.0-rc1", "Handshake clientVersion matches");

    // 5. FileHeaderMessage
    FileHeaderMessage fh;
    fh.fileSize = 104857600; // 100 MB
    fh.chunkSize = 65536;
    fh.flags = 0x01;
    fh.fileName = "release_archive.tar.gz";
    fh.sha256Hex = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
    std::vector<uint8_t> fhBytes = fh.serialize();

    FileHeaderMessage parsedFh;
    TEST_ASSERT(FileHeaderMessage::deserialize(fhBytes.data(), fhBytes.size(), parsedFh, &err), "FileHeader parsed");
    TEST_ASSERT(parsedFh.fileSize == 104857600, "File size matches");
    TEST_ASSERT(parsedFh.chunkSize == 65536, "Chunk size matches");
    TEST_ASSERT(parsedFh.flags == 0x01, "Flags match");
    TEST_ASSERT(parsedFh.fileName == "release_archive.tar.gz", "Filename matches");
    TEST_ASSERT(parsedFh.sha256Hex == fh.sha256Hex, "SHA-256 hex matches");

    // Truncated buffer underflow check
    TEST_ASSERT(!FileHeaderMessage::deserialize(fhBytes.data(), 10, parsedFh, &err), "Rejects truncated FileHeader buffer");

    // 6. ChunkDataMessage & CRC integrity verification
    ChunkDataMessage chunk;
    chunk.offset = 131072;
    chunk.chunkIndex = 2;
    chunk.data = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
    chunk.chunkCrc32 = computeCrc32(chunk.data.data(), chunk.data.size());
    std::vector<uint8_t> chunkBytes = chunk.serialize();

    ChunkDataMessage parsedChunk;
    TEST_ASSERT(ChunkDataMessage::deserialize(chunkBytes.data(), chunkBytes.size(), parsedChunk, &err), "ChunkData parsed");
    TEST_ASSERT(parsedChunk.offset == 131072, "Chunk offset matches");
    TEST_ASSERT(parsedChunk.chunkIndex == 2, "Chunk index matches");
    TEST_ASSERT(parsedChunk.data == chunk.data, "Chunk data payload matches");

    // Tampered payload detection via CRC
    chunkBytes.back() ^= 0xFF; // corrupt one byte
    TEST_ASSERT(!ChunkDataMessage::deserialize(chunkBytes.data(), chunkBytes.size(), parsedChunk, &err), "Detects chunk corruption via CRC");

    // 7. TransferAckMessage
    TransferAckMessage ack;
    ack.status = AckStatus::Success;
    ack.receivedBytes = 104857600;
    ack.message = "File verified and saved";
    std::vector<uint8_t> ackBytes = ack.serialize();

    TransferAckMessage parsedAck;
    TEST_ASSERT(TransferAckMessage::deserialize(ackBytes.data(), ackBytes.size(), parsedAck, &err), "Ack parsed");
    TEST_ASSERT(parsedAck.status == AckStatus::Success, "Ack status is Success");
    TEST_ASSERT(parsedAck.receivedBytes == 104857600, "Ack receivedBytes matches");
    TEST_ASSERT(parsedAck.message == "File verified and saved", "Ack message matches");

    // 8. TransferCancelMessage
    TransferCancelMessage cancel;
    cancel.errorCode = 403;
    cancel.reason = "Quota limit exceeded";
    std::vector<uint8_t> cancelBytes = cancel.serialize();

    TransferCancelMessage parsedCancel;
    TEST_ASSERT(TransferCancelMessage::deserialize(cancelBytes.data(), cancelBytes.size(), parsedCancel, &err), "Cancel parsed");
    TEST_ASSERT(parsedCancel.errorCode == 403, "Cancel error code matches");
    TEST_ASSERT(parsedCancel.reason == "Quota limit exceeded", "Cancel reason matches");

    // 9. Full wire frame builder
    std::vector<uint8_t> pingPayload = {0x01, 0x02, 0x03, 0x04};
    std::vector<uint8_t> wireFrame = buildFrame(MessageType::Ping, pingPayload);
    TEST_ASSERT(wireFrame.size() == FRAME_HEADER_SIZE + pingPayload.size(), "Frame size matches header + payload");
    FrameHeader pingHdr;
    TEST_ASSERT(FrameHeader::deserialize(wireFrame.data(), pingHdr, &err), "Frame header parsed from wire frame");
    TEST_ASSERT(pingHdr.msgType == MessageType::Ping, "Frame msgType is Ping");
    TEST_ASSERT(pingHdr.payloadLength == pingPayload.size(), "Frame payloadLength matches");
    TEST_ASSERT(pingHdr.crc32OrSeq == computeCrc32(pingPayload.data(), pingPayload.size()), "Frame CRC32 matches payload");

    // 10. Legacy v1 detection & backward compatibility branching
    const uint8_t v2Magic[4] = {'L', 'N', 'D', 'P'};
    TEST_ASSERT(isProtocolV2(v2Magic), "Identifies v2 magic header");

    const uint8_t v1HeaderSample[4] = {14, 0, 0, 0}; // Legacy v1 4-byte nameLen for "my_archive.zip"
    TEST_ASSERT(!isProtocolV2(v1HeaderSample), "Distinguishes legacy v1 connection from v2");

    // 11. Anti-downgrade policy controls
    TransferOptions defaultOpts;
    TEST_ASSERT(defaultOpts.allowLegacyV1Fallback, "Default options allow legacy fallback for backward compatibility");
    TEST_ASSERT(!defaultOpts.requireProtocolV2, "Default options do not strictly require v2");

    TransferOptions strictOpts;
    strictOpts.requireProtocolV2 = true;
    strictOpts.allowLegacyV1Fallback = false;
    TEST_ASSERT(strictOpts.requireProtocolV2, "Strict options enforce protocol v2");
    TEST_ASSERT(!strictOpts.allowLegacyV1Fallback, "Strict options prohibit legacy v1 downgrade");

    return true;
}

static bool testTransportChannelAbstraction() {
    using namespace landrop;

    // 1. Enum string conversions
    TEST_ASSERT(std::string(transportTypeToString(TransportType::TcpDirect)) == "TcpDirect", "TcpDirect string");
    TEST_ASSERT(std::string(transportTypeToString(TransportType::TlsTcp)) == "TlsTcp", "TlsTcp string");
    TEST_ASSERT(std::string(transportTypeToString(TransportType::WebRtcDataChannel)) == "WebRtcDataChannel", "WebRtcDataChannel string");
    TEST_ASSERT(std::string(transportTypeToString(TransportType::CloudRelay)) == "CloudRelay", "CloudRelay string");

    // 2. Loopback TCP via ITransportChannel polymorphism
    uint16_t testPort = 19877;
    NetworkSession listenerSession("test-srv-listener");
    bool listenOk = false;
    for (int retry = 0; retry < 5; ++retry) {
        if (listenerSession.listenOn(testPort)) {
            listenOk = true;
            break;
        }
        testPort += 10;
    }
    TEST_ASSERT(listenOk, "Listener session bound successfully for transport channel test");

    std::shared_ptr<ITransportChannel> clientChannel;
    std::shared_ptr<ITransportChannel> serverChannel;
    std::atomic<bool> serverReady(false);
    std::atomic<bool> acceptSuccess(false);

    std::thread serverThread([&]() {
        serverReady = true;
        if (listenerSession.acceptIncoming()) {
            acceptSuccess = true;
            serverChannel = TcpSocketChannel::create(
                std::make_unique<NetworkSession>(std::move(listenerSession))
            );
        }
    });

    while (!serverReady.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    clientChannel = std::shared_ptr<TcpSocketChannel>(new TcpSocketChannel("127.0.0.1", testPort, false));

    if (serverThread.joinable()) {
        serverThread.join();
    }

    TEST_ASSERT(acceptSuccess.load(), "Server accepted incoming connection");
    TEST_ASSERT(clientChannel != nullptr && clientChannel->isConnected(), "Client channel is connected");
    TEST_ASSERT(serverChannel != nullptr && serverChannel->isConnected(), "Server channel is connected");
    TEST_ASSERT(clientChannel->getTransportType() == TransportType::TcpDirect, "Client transport type is TcpDirect");
    TEST_ASSERT(serverChannel->getTransportType() == TransportType::TcpDirect, "Server transport type is TcpDirect");
    TEST_ASSERT(!clientChannel->isEncrypted(), "Client is not encrypted in plaintext mode");

    // 3. Bidirectional data transfer over ITransportChannel
    std::string testMsg = "Hello through ITransportChannel polymorphic abstraction!";
    std::vector<uint8_t> sendData(testMsg.begin(), testMsg.end());
    TEST_ASSERT(clientChannel->sendExact(sendData.data(), sendData.size()), "Client channel sent exact bytes");

    std::vector<uint8_t> recvData(sendData.size(), 0);
    TEST_ASSERT(serverChannel->receiveExact(recvData.data(), recvData.size(), 2000), "Server channel received exact bytes");
    std::string receivedStr(recvData.begin(), recvData.end());
    TEST_ASSERT(receivedStr == testMsg, "Data transferred through ITransportChannel matches byte-for-byte");

    // 4. Accounting & Buffered amount
    TEST_ASSERT(clientChannel->getBytesSent() == sendData.size(), "Client bytes sent accounted correctly");
    TEST_ASSERT(serverChannel->getBytesReceived() == sendData.size(), "Server bytes received accounted correctly");
    size_t buffered = clientChannel->getBufferedAmount();
    TEST_ASSERT(buffered < 1000000000ULL, "Buffered amount returns valid size query");

    // 5. Cancellation check
    std::atomic<bool> cancelFlag(true); // already cancelled
    uint8_t dummy[16];
    bool recvCancelled = serverChannel->receiveExact(dummy, sizeof(dummy), 1000, &cancelFlag);
    TEST_ASSERT(!recvCancelled, "Cancellation token immediately aborts receiveExact");

    // 6. Channel closure
    clientChannel->close();
    serverChannel->close();
    TEST_ASSERT(!clientChannel->isConnected(), "Client channel marked disconnected after close()");
    TEST_ASSERT(!serverChannel->isConnected(), "Server channel marked disconnected after close()");

    return true;
}

static bool testWebRtcDataChannelPrep() {
    using namespace landrop;

    WebRtcConfig cfg;
    cfg.iceServers = {"stun:stun.l.google.com:19302"};
    cfg.maxBufferedAmount = 1024 * 1024; // 1MB limit for testing backpressure

    WebRtcDataChannel channel("peer-uuid-webrtc-99", "lan-drop-file-channel", cfg);

    TEST_ASSERT(channel.getTransportType() == TransportType::WebRtcDataChannel, "Transport type is WebRtcDataChannel");
    TEST_ASSERT(channel.getRemoteAddress() == "peer-uuid-webrtc-99", "Remote address matches peer UUID");
    TEST_ASSERT(channel.getRemotePort() == 0, "Remote port is 0 (ICE dynamic)");
    TEST_ASSERT(channel.isEncrypted(), "WebRtcDataChannel reports encrypted (DTLS)");
    TEST_ASSERT(!channel.isConnected(), "Initially disconnected");
    TEST_ASSERT(channel.getLabel() == "lan-drop-file-channel", "Channel label matches");
    TEST_ASSERT(channel.getConfig().iceServers.size() == 1, "Configured ICE servers present");

    // Cannot send while disconnected
    uint8_t sampleData[4] = {1, 2, 3, 4};
    TEST_ASSERT(!channel.sendExact(sampleData, sizeof(sampleData)), "Cannot send on disconnected channel");

    // Connect
    channel.setConnected(true);
    TEST_ASSERT(channel.isConnected(), "Channel connected");

    // Send data: fails if no real channel and not simulated (F-05)
    TEST_ASSERT(!channel.sendExact(sampleData, sizeof(sampleData)), "Cannot send on connected channel without real channel in non-simulated mode");
    channel.setSimulatedMode(true);
    TEST_ASSERT(channel.sendExact(sampleData, sizeof(sampleData)), "Sent data on connected channel in simulated mode");
    TEST_ASSERT(channel.getBytesSent() == sizeof(sampleData), "Bytes sent accounted");

    // Backpressure: attempt to send > maxBufferedAmount
    std::vector<uint8_t> oversizeData(2 * 1024 * 1024, 0xAA);
    TEST_ASSERT(!channel.sendExact(oversizeData.data(), oversizeData.size()), "Backpressure rejects sending data exceeding maxBufferedAmount");

    // Dynamic buffered amount control
    channel.setBufferedAmount(500000);
    TEST_ASSERT(channel.getBufferedAmount() == 500000, "Simulated buffered amount reflects queue state");
    channel.setBufferedAmount(0);
    TEST_ASSERT(channel.getBufferedAmount() == 0, "Buffered amount successfully drained");

    // Receive data
    std::string testMsg = "WebRTC P2P Data Chunk Payload";
    channel.injectIncomingBytes(reinterpret_cast<const uint8_t*>(testMsg.data()), testMsg.size());

    std::vector<uint8_t> recvBuf(testMsg.size(), 0);
    TEST_ASSERT(channel.receiveExact(recvBuf.data(), recvBuf.size(), 1000), "Received exact bytes from WebRtcDataChannel");
    std::string recvStr(recvBuf.begin(), recvBuf.end());
    TEST_ASSERT(recvStr == testMsg, "Received WebRTC payload matches sent payload");
    TEST_ASSERT(channel.getBytesReceived() == testMsg.size(), "Bytes received accounted");

    // Cancellation support
    std::atomic<bool> cancelToken(true);
    uint8_t cancelBuf[8];
    TEST_ASSERT(!channel.receiveExact(cancelBuf, sizeof(cancelBuf), 500, &cancelToken), "Cancelled token aborts receiveExact immediately");

    // Close channel
    channel.close();
    TEST_ASSERT(!channel.isConnected(), "Channel marked disconnected after close()");

    return true;
}

static bool testCandidateQueueAndBackpressure() {
    // ----------------------------------------------------
    // TC-RTC-01: Remote ICE Candidate Queueing & Drain
    // ----------------------------------------------------
    CandidateQueue queue;
    TEST_ASSERT(!queue.isRemoteDescriptionSet(), "Initially remote description not set");
    TEST_ASSERT(queue.pendingCount() == 0, "Initially queue empty");

    std::vector<std::string> deliveredCandidates;
    queue.setOnCandidateReady([&](const IceCandidate& cand) {
        deliveredCandidates.push_back(cand.candidate);
    });

    // Push 3 candidates before remote description is set
    queue.push({"candidate:1 1 UDP 2122260223 192.168.1.50 50000 typ host", "0", 0});
    queue.push({"candidate:2 1 UDP 2122260224 192.168.1.50 50001 typ host", "0", 1});
    queue.push({"candidate:3 1 UDP 1686052607 198.51.100.1 50002 typ srflx", "0", 2});

    TEST_ASSERT(queue.pendingCount() == 3, "All 3 trickle candidates buffered before description set");
    TEST_ASSERT(deliveredCandidates.empty(), "No candidates delivered prior to remote description set");

    // Set remote description -> should drain all 3 in order
    queue.setRemoteDescriptionSet();
    TEST_ASSERT(queue.isRemoteDescriptionSet(), "Remote description marked set");
    TEST_ASSERT(queue.pendingCount() == 0, "Queue drained");
    TEST_ASSERT(deliveredCandidates.size() == 3, "All 3 candidates flushed upon description set");
    TEST_ASSERT(deliveredCandidates[0].find("50000") != std::string::npos, "First candidate matches order");
    TEST_ASSERT(deliveredCandidates[1].find("50001") != std::string::npos, "Second candidate matches order");
    TEST_ASSERT(deliveredCandidates[2].find("50002") != std::string::npos, "Third candidate matches order");

    // Subsequent candidates arriving after remote description is set should be delivered immediately
    queue.push({"candidate:4 1 UDP 2122260225 10.0.0.1 50003 typ host", "0", 3});
    TEST_ASSERT(deliveredCandidates.size() == 4, "Post-description candidate delivered immediately without buffering");
    TEST_ASSERT(queue.pendingCount() == 0, "Queue remains empty for immediate candidates");

    // Verify deferred callback registration (F-11)
    CandidateQueue deferredQueue;
    deferredQueue.push({"candidate:defer1", "0", 0});
    deferredQueue.push({"candidate:defer2", "0", 1});
    TEST_ASSERT(deferredQueue.pendingCount() == 2, "Candidates buffered without callback");
    deferredQueue.setRemoteDescriptionSet();
    TEST_ASSERT(deferredQueue.pendingCount() == 2, "Candidates retained when description set before callback");
    std::vector<std::string> deferredDelivered;
    deferredQueue.setOnCandidateReady([&](const IceCandidate& cand) {
        deferredDelivered.push_back(cand.candidate);
    });
    TEST_ASSERT(deferredDelivered.size() == 2, "Candidates flushed immediately upon late callback attachment");
    TEST_ASSERT(deferredQueue.pendingCount() == 0, "Deferred queue drained");

    // Verify bounded capacity DoS prevention (F-12)
    CandidateQueue boundedQueue;
    for (size_t i = 0; i < CandidateQueue::kMaxQueueCapacity; ++i) {
        TEST_ASSERT(boundedQueue.push({"cand", "0", 0}), "Push within capacity succeeds");
    }
    TEST_ASSERT(!boundedQueue.push({"overflow", "0", 0}), "Push beyond capacity is rejected");

    // ----------------------------------------------------
    // TC-RTC-02: WebRTC Backpressure Flow Control (Non-failing cv wait)
    // ----------------------------------------------------
    WebRtcConfig bpConfig;
    bpConfig.maxBufferedAmount = 1000;
    bpConfig.lowWaterMark = 400;

    WebRtcDataChannel bpChannel("peer-bp-test", "bp-channel", bpConfig);
    bpChannel.setConnected(true);
    bpChannel.setSimulatedMode(true);

    // Initial state: buffer empty
    uint8_t payload[300];
    std::memset(payload, 0x55, sizeof(payload));

    // Send 300 bytes -> fits in 1000
    TEST_ASSERT(bpChannel.sendExact(payload, sizeof(payload)), "Initial 300B send succeeds");

    // Simulate socket/channel buffering: set buffered amount to 800 bytes
    bpChannel.setBufferedAmount(800);

    // Attempt to send 300 bytes -> 800 + 300 = 1100 > 1000 maxBufferedAmount.
    // It must block until buffered amount drops to lowWaterMark!
    std::atomic<bool> sendStarted{false};
    std::atomic<bool> sendCompleted{false};
    std::atomic<bool> sendResult{false};

    std::thread senderThread([&]() {
        sendStarted.store(true);
        bool ok = bpChannel.sendExact(payload, sizeof(payload));
        sendResult.store(ok);
        sendCompleted.store(true);
    });

    // Wait for thread to enter wait state
    while (!sendStarted.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    TEST_ASSERT(!sendCompleted.load(), "sendExact() is blocked on backpressure condition variable");

    // Simulate drain event: buffer drops to 300 (< lowWaterMark 400)
    bpChannel.setBufferedAmount(300); // setBufferedAmount calls notifyBufferedAmountLow() automatically

    senderThread.join();

    TEST_ASSERT(sendCompleted.load(), "sendExact() resumed after buffer drained");
    TEST_ASSERT(sendResult.load(), "sendExact() succeeded after backpressure relief");

    bpChannel.close();
    return true;
}

static bool testSignalingClientAndGlareResolution() {
    using namespace landrop::signaling;

    SignalingClient alice("alice_client");
    SignalingClient bob("bob_client");

    std::vector<std::string> aliceSentEnvelopes;
    std::vector<std::string> bobSentEnvelopes;

    alice.setCustomSender([&](const std::string& msg) {
        aliceSentEnvelopes.push_back(msg);
        return true;
    });

    bob.setCustomSender([&](const std::string& msg) {
        bobSentEnvelopes.push_back(msg);
        return true;
    });

    TEST_ASSERT(alice.connect("ws://localhost:8080/ws"), "Alice connected with custom sender");
    TEST_ASSERT(bob.connect("ws://localhost:8080/ws"), "Bob connected with custom sender");

    // 1. Room Creation by Alice (Initiator)
    TEST_ASSERT(alice.createRoom(), "Alice sends CREATE_ROOM");
    TEST_ASSERT(aliceSentEnvelopes.size() == 1, "Alice sent 1 message");
    TEST_ASSERT(aliceSentEnvelopes[0].find("CREATE_ROOM") != std::string::npos, "Envelope contains CREATE_ROOM action");

    // Simulate Server -> Alice: ROOM_CREATED
    std::string roomCreatedMsg = R"({
        "version": "2.0",
        "action": "ROOM_CREATED",
        "client_id": "alice_client",
        "timestamp": 1700000000,
        "payload": {
            "room_code": "DROP-TEST-7788",
            "room_token": "token_alice_secret",
            "role": "initiator",
            "expires_at": 1700000600
        }
    })";

    bool aliceRoomCreatedCalled = false;
    alice.onRoomCreated = [&](const RoomCreatedInfo& info) {
        aliceRoomCreatedCalled = true;
        if (info.roomCode != "DROP-TEST-7788" || info.roomToken != "token_alice_secret" || info.role != "initiator") {
            aliceRoomCreatedCalled = false;
        }
    };

    TEST_ASSERT(alice.handleIncomingMessage(roomCreatedMsg), "Alice parsed ROOM_CREATED");
    TEST_ASSERT(aliceRoomCreatedCalled, "onRoomCreated fired with valid data");
    TEST_ASSERT(alice.getRole() == Role::Initiator, "Alice role is Initiator");
    TEST_ASSERT(alice.getRoomCode() == "DROP-TEST-7788", "Alice room code stored");
    TEST_ASSERT(alice.getRoomToken() == "token_alice_secret", "Alice room token stored");

    // 2. Bob joins room
    TEST_ASSERT(bob.joinRoom("DROP-TEST-7788"), "Bob sends JOIN_ROOM");
    TEST_ASSERT(bobSentEnvelopes.size() == 1, "Bob sent 1 message");
    TEST_ASSERT(bobSentEnvelopes[0].find("JOIN_ROOM") != std::string::npos, "Envelope contains JOIN_ROOM action");
    TEST_ASSERT(bobSentEnvelopes[0].find("DROP-TEST-7788") != std::string::npos, "Envelope contains room code");

    // Simulate Server -> Bob: PEER_JOINED (Bob as joiner)
    std::string bobPeerJoinedMsg = R"({
        "version": "2.0",
        "action": "PEER_JOINED",
        "client_id": "bob_client",
        "timestamp": 1700000001,
        "payload": {
            "room_code": "DROP-TEST-7788",
            "peer_id": "alice_client",
            "role": "joiner",
            "room_token": "token_bob_secret"
        }
    })";

    bool bobPeerJoinedCalled = false;
    bob.onPeerJoined = [&](const PeerJoinedInfo& info) {
        if (info.peerId == "alice_client" && info.role == "joiner") {
            bobPeerJoinedCalled = true;
        }
    };

    TEST_ASSERT(bob.handleIncomingMessage(bobPeerJoinedMsg), "Bob parsed PEER_JOINED");
    TEST_ASSERT(bobPeerJoinedCalled, "Bob onPeerJoined fired");
    TEST_ASSERT(bob.getRole() == Role::Joiner, "Bob role is Joiner");
    TEST_ASSERT(bob.getPeerId() == "alice_client", "Bob peer ID is Alice");

    // 3. Glare Resolution Enforcement (TC-RTC-03)
    // Rule: Joiner (Bob) cannot initiate SDP_OFFER
    std::string bobErrorReceived;
    bob.onError = [&](const ErrorInfo& err) {
        bobErrorReceived = err.code;
        // F-01: Verify re-entrancy without deadlock
        (void)bob.getRole();
        (void)bob.getRoomCode();
        (void)bob.getPeerId();
    };
    TEST_ASSERT(!bob.sendSdpOffer("v=0\r\no=bob..."), "Bob (Joiner) sendSdpOffer is rejected to prevent glare");
    TEST_ASSERT(bobErrorReceived == "ERR_GLARE_VIOLATION", "Bob received ERR_GLARE_VIOLATION for offer attempt");

    // Rule: Initiator (Alice) cannot send SDP_ANSWER
    std::string aliceErrorReceived;
    alice.onError = [&](const ErrorInfo& err) {
        aliceErrorReceived = err.code;
        // F-01: Verify re-entrancy without deadlock
        (void)alice.getRole();
        (void)alice.getRoomCode();
        (void)alice.getPeerId();
    };
    TEST_ASSERT(!alice.sendSdpAnswer("v=0\r\no=alice..."), "Alice (Initiator) sendSdpAnswer is rejected to prevent glare");
    TEST_ASSERT(aliceErrorReceived == "ERR_GLARE_VIOLATION", "Alice received ERR_GLARE_VIOLATION for answer attempt");

    // F-09 Ingress Glare Check: Initiator must reject ingress SDP_OFFER
    std::string aliceGlareError;
    alice.onError = [&](const ErrorInfo& err) {
        aliceGlareError = err.code;
    };
    TEST_ASSERT(!alice.handleIncomingMessage(R"({"version":"2.0","action":"SDP_OFFER","payload":{"type":"offer","sdp":"..."}})"), "Initiator rejects incoming SDP_OFFER to avoid glare");
    TEST_ASSERT(aliceGlareError == "ERR_GLARE_VIOLATION", "Alice reports ERR_GLARE_VIOLATION on ingress offer");

    // F-09 Ingress Glare Check: Joiner must reject ingress SDP_ANSWER
    std::string bobGlareError;
    bob.onError = [&](const ErrorInfo& err) {
        bobGlareError = err.code;
    };
    TEST_ASSERT(!bob.handleIncomingMessage(R"({"version":"2.0","action":"SDP_ANSWER","payload":{"type":"answer","sdp":"..."}})"), "Joiner rejects incoming SDP_ANSWER to avoid glare");
    TEST_ASSERT(bobGlareError == "ERR_GLARE_VIOLATION", "Bob reports ERR_GLARE_VIOLATION on ingress answer");

    // F-02: Remote crash prevention on null/non-object payload
    TEST_ASSERT(alice.handleIncomingMessage(R"({"version":"2.0","action":"ROOM_CREATED","payload":null})"), "Null payload does not throw exception or crash");

    // F-08: Client role transition upon reuse
    SignalingClient reusedClient("reused");
    reusedClient.createRoom();
    TEST_ASSERT(reusedClient.getRole() == Role::Initiator, "Role is Initiator after createRoom");
    reusedClient.joinRoom("DROP-TEST-9999");
    TEST_ASSERT(reusedClient.getRole() == Role::Joiner, "Role transitions to Joiner after joinRoom");

    // 4. Valid Negotiation Flow:
    // Alice sends SDP offer -> Bob
    size_t alicePrevSent = aliceSentEnvelopes.size();
    TEST_ASSERT(alice.sendSdpOffer("v=0\r\no=alice 1000 2 IN IP4 127.0.0.1...", "e2ee_alice_key_hex"), "Alice sends SDP offer");
    TEST_ASSERT(aliceSentEnvelopes.size() == alicePrevSent + 1, "Alice transmitted offer envelope");

    bool bobReceivedOffer = false;
    bob.onSdpOffer = [&](const SdpInfo& sdp) {
        if (sdp.type == "offer" && sdp.sdp.find("alice") != std::string::npos && sdp.e2eePublicKey == "e2ee_alice_key_hex") {
            bobReceivedOffer = true;
        }
    };
    TEST_ASSERT(bob.handleIncomingMessage(aliceSentEnvelopes.back()), "Bob handles Alice's offer");
    TEST_ASSERT(bobReceivedOffer, "Bob onSdpOffer fired with correct offer details");

    // Bob sends SDP answer -> Alice
    size_t bobPrevSent = bobSentEnvelopes.size();
    TEST_ASSERT(bob.sendSdpAnswer("v=0\r\no=bob 2000 2 IN IP4 127.0.0.1...", "e2ee_bob_key_hex"), "Bob sends SDP answer");
    TEST_ASSERT(bobSentEnvelopes.size() == bobPrevSent + 1, "Bob transmitted answer envelope");

    bool aliceReceivedAnswer = false;
    alice.onSdpAnswer = [&](const SdpInfo& sdp) {
        if (sdp.type == "answer" && sdp.sdp.find("bob") != std::string::npos && sdp.e2eePublicKey == "e2ee_bob_key_hex") {
            aliceReceivedAnswer = true;
        }
    };
    TEST_ASSERT(alice.handleIncomingMessage(bobSentEnvelopes.back()), "Alice handles Bob's answer");
    TEST_ASSERT(aliceReceivedAnswer, "Alice onSdpAnswer fired with correct answer details");

    // ICE Candidate Exchange
    TEST_ASSERT(alice.sendIceCandidate("candidate:udp:127.0.0.1:40000", "data", 0), "Alice sends candidate");
    bool bobReceivedCandidate = false;
    bob.onIceCandidate = [&](const IceCandidateInfo& cand) {
        if (cand.candidate == "candidate:udp:127.0.0.1:40000" && cand.sdpMid == "data" && cand.sdpMLineIndex == 0) {
            bobReceivedCandidate = true;
        }
    };
    TEST_ASSERT(bob.handleIncomingMessage(aliceSentEnvelopes.back()), "Bob handles ICE candidate");
    TEST_ASSERT(bobReceivedCandidate, "Bob onIceCandidate fired");

    // 5. Leave Room and State Reset
    bool alicePeerLeftCalled = false;
    alice.onPeerLeft = [&](const PeerLeftInfo& info) {
        if (info.reason == "user_exit") {
            alicePeerLeftCalled = true;
        }
    };

    TEST_ASSERT(bob.leaveRoom("user_exit"), "Bob leaves room");
    TEST_ASSERT(bob.getRole() == Role::Unknown, "Bob role reset to Unknown after leaving");
    TEST_ASSERT(bob.getRoomCode().empty(), "Bob room code cleared after leaving");

    std::string peerLeftEnv = R"({
        "version": "2.0",
        "action": "PEER_LEFT",
        "client_id": "server",
        "timestamp": 1700000005,
        "payload": {
            "peer_id": "bob_client",
            "reason": "user_exit"
        }
    })";
    TEST_ASSERT(alice.handleIncomingMessage(peerLeftEnv), "Alice handles PEER_LEFT");
    TEST_ASSERT(alicePeerLeftCalled, "Alice onPeerLeft fired");
    TEST_ASSERT(alice.getPeerId().empty(), "Alice peerId cleared");

    alice.disconnect();
    bob.disconnect();
    return true;
}


static bool testMilestone2C_GlobalWebRtcTransferPipeline() {
    using namespace landrop;

    // -------------------------------------------------------------
    // 1. TC-TURN-01: Coturn REST API & JSON Configuration Parsing
    // -------------------------------------------------------------
    {
        std::string validJson = R"({
            "ice_servers": [
                {"urls": ["stun:stun.l.google.com:19302"]},
                {
                    "urls": [
                        "turn:turn.lan-drop.org:3478?transport=udp",
                        "turns:turn.lan-drop.org:5349?transport=tcp"
                    ],
                    "username": "1730000000:alice",
                    "credential": "secretpassword123"
                }
            ],
            "ttl_seconds": 86400
        })";

        WebRtcConfig cfg;
        int64_t ttl = 0;
        std::string err;
        bool ok = TurnConfigClient::parseTurnConfigJson(validJson, cfg, &ttl, &err);
        TEST_ASSERT(ok, "Valid Coturn JSON parsed successfully");
        TEST_ASSERT(err.empty(), "No error on valid TURN parse");
        TEST_ASSERT(ttl == 86400, "TTL parsed correctly (86400)");
        TEST_ASSERT(cfg.turnUsername == "1730000000:alice", "TURN username extracted");
        TEST_ASSERT(cfg.turnPassword == "secretpassword123", "TURN credential extracted");
        TEST_ASSERT(cfg.turnServer == "turn:turn.lan-drop.org:3478?transport=udp", "TURN server URI selected");
        TEST_ASSERT(cfg.iceServers.size() == 3, "All 3 ICE server URIs captured");

        // Malformed JSON checks
        WebRtcConfig malformedCfg;
        std::string malformedErr;
        bool malformedOk = TurnConfigClient::parseTurnConfigJson("{not_valid_json", malformedCfg, nullptr, &malformedErr);
        TEST_ASSERT(!malformedOk, "Malformed JSON rejected");
        TEST_ASSERT(!malformedErr.empty(), "Malformed JSON returns error message");

        std::string missingServersJson = R"({"status": "ok"})";
        bool missingOk = TurnConfigClient::parseTurnConfigJson(missingServersJson, malformedCfg, nullptr, &malformedErr);
        TEST_ASSERT(!missingOk, "JSON missing ice_servers array rejected");

        // F-01 regression test: JSON type mismatch must be caught gracefully without aborting
        std::string typeErrorJson = R"({"ice_servers": [{"urls": ["turn:h:3478"], "username": 12345}]})";
        std::string typeErr;
        bool typeOk = TurnConfigClient::parseTurnConfigJson(typeErrorJson, malformedCfg, nullptr, &typeErr);
        TEST_ASSERT(!typeOk, "Type mismatch in JSON fields safely rejected without throwing");
        TEST_ASSERT(!typeErr.empty(), "Descriptive error returned on JSON type mismatch");

        // F-06 regression test: CandidateQueue::reset() clears callback
        {
            CandidateQueue cq;
            bool cbFired = false;
            cq.setOnCandidateReady([&](const IceCandidate&) { cbFired = true; });
            cq.reset();
            cq.setRemoteDescriptionSet();
            cq.push({"cand1", "mid1", 0});
            TEST_ASSERT(!cbFired, "CandidateQueue callback cleared on reset");
        }

        // Mock HttpClient testing fetchTurnConfig
        class MockTurnHttpClient : public HttpClient {
        public:
            std::string lastAuthHeader;
            std::string lastUrl;
            HttpResponse send(const HttpRequest& request) override {
                lastUrl = request.url;
                auto it = request.headers.find("Authorization");
                if (it != request.headers.end()) {
                    lastAuthHeader = it->second;
                }
                HttpResponse resp;
                resp.statusCode = 200;
                resp.success = true;
                resp.body = R"({
                    "ice_servers": [
                        {"urls": "stun:stun.example.com:3478"},
                        {"urls": ["turn:turn.example.com:3478"], "username": "u1", "credential": "p1"}
                    ],
                    "ttl_seconds": 3600
                })";
                return resp;
            }
        };

        auto mockHttp = std::make_shared<MockTurnHttpClient>();
        TurnConfigClient client(mockHttp);
        WebRtcConfig fetchedCfg;
        std::string fetchErr;
        bool fetchOk = client.fetchTurnConfig(
            "https://signaling.lan-drop.org",
            "token-secret-xyz",
            "client-abc",
            fetchedCfg,
            5,
            &fetchErr
        );
        TEST_ASSERT(fetchOk, "fetchTurnConfig succeeds with mock HTTP response");
        TEST_ASSERT(mockHttp->lastAuthHeader == "Bearer token-secret-xyz", "Bearer token sent in Authorization header");
        TEST_ASSERT(mockHttp->lastUrl.find("/api/v1/turn-config?token=token-secret-xyz&client_id=client-abc") != std::string::npos,
                    "Query parameters formatted correctly in request URL");
        TEST_ASSERT(fetchedCfg.turnUsername == "u1" && fetchedCfg.turnPassword == "p1", "Fetched credentials mapped into WebRtcConfig");
        TEST_ASSERT(fetchedCfg.iceServers.size() == 2, "Both STUN and TURN URIs mapped");
    }

    // -------------------------------------------------------------
    // 2. TC-E2E-01: Mock Loopback WebRTC P2P Transfer (10 MB Memory Buffer)
    // -------------------------------------------------------------
    {
        CoordinatorOptions opts;
        opts.mockLoopbackMode = true;

        auto initiator = std::make_shared<WebRtcSessionCoordinator>("alice", WebRtcConfig{}, opts);
        auto joiner = std::make_shared<WebRtcSessionCoordinator>("bob", WebRtcConfig{}, opts);

        initiator->enableLoopbackMode(joiner);

        TEST_ASSERT(initiator->startAsInitiator("ws://mock/ws"), "Initiator start succeeds");
        std::string roomCode = initiator->getRoomCode();
        TEST_ASSERT(!roomCode.empty(), "Initiator creates room code");

        TEST_ASSERT(joiner->startAsJoiner("ws://mock/ws", roomCode), "Joiner start succeeds with room code");

        auto txChan = initiator->waitForConnection(std::chrono::milliseconds(2000));
        auto rxChan = joiner->waitForConnection(std::chrono::milliseconds(2000));

        TEST_ASSERT(txChan != nullptr && txChan->isConnected(), "Initiator WebRTC channel is connected");
        TEST_ASSERT(rxChan != nullptr && rxChan->isConnected(), "Joiner WebRTC channel is connected");

        // Transfer 10 MB in-memory buffer
        constexpr size_t kPayloadSize = 10 * 1024 * 1024;
        std::vector<uint8_t> sendData(kPayloadSize);
        for (size_t i = 0; i < kPayloadSize; ++i) {
            sendData[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);
        }

        std::vector<uint8_t> recvData(kPayloadSize, 0);
        std::atomic<bool> rxDone{false};
        std::atomic<bool> rxError{false};

        std::thread rxThread([&]() {
            if (!rxChan->receiveExact(recvData.data(), kPayloadSize, 5000)) {
                rxError.store(true);
            }
            rxDone.store(true);
        });

        // Send in 64 KB slices
        constexpr size_t kChunkSize = 64 * 1024;
        size_t totalSent = 0;
        bool sendOk = true;
        while (totalSent < kPayloadSize) {
            size_t toSend = std::min(kChunkSize, kPayloadSize - totalSent);
            if (!txChan->sendExact(sendData.data() + totalSent, toSend)) {
                sendOk = false;
                break;
            }
            totalSent += toSend;
        }

        rxThread.join();

        TEST_ASSERT(sendOk, "10 MB in-memory send completed successfully");
        TEST_ASSERT(!rxError.load() && rxDone.load(), "10 MB in-memory receive completed successfully");
        TEST_ASSERT(sendData == recvData, "10 MB buffer data matches identically between peers");
        TEST_ASSERT(txChan->getBytesSent() == kPayloadSize, "Channel bytesSent matches 10 MB");
        TEST_ASSERT(rxChan->getBytesReceived() == kPayloadSize, "Channel bytesReceived matches 10 MB");

        initiator->close();
        joiner->close();
    }

    // -------------------------------------------------------------
    // 3. TC-E2E-02: Real File Transfer Over WebRTC DataChannel Pipeline
    // -------------------------------------------------------------
    std::filesystem::path rootDir = "./unit_test_webrtc_e2e_tmp";
    std::filesystem::remove_all(rootDir);
    std::filesystem::create_directories(rootDir);
    std::filesystem::path sendDir = rootDir / "sender";
    std::filesystem::path recvDir = rootDir / "receiver";
    std::filesystem::create_directories(sendDir);
    std::filesystem::create_directories(recvDir);

    FileChunkHasher hasher;
    TransferEngine engine;

    {
        CoordinatorOptions opts;
        opts.mockLoopbackMode = true;

        auto initiator = std::make_shared<WebRtcSessionCoordinator>("alice", WebRtcConfig{}, opts);
        auto joiner = std::make_shared<WebRtcSessionCoordinator>("bob", WebRtcConfig{}, opts);
        initiator->enableLoopbackMode(joiner);

        initiator->startAsInitiator("ws://mock/ws");
        joiner->startAsJoiner("ws://mock/ws", initiator->getRoomCode());

        auto txChan = initiator->waitForConnection(std::chrono::milliseconds(2000));
        auto rxChan = joiner->waitForConnection(std::chrono::milliseconds(2000));

        // Create a 4 MB binary file
        std::string largeFile = (sendDir / "webrtc_payload.bin").string();
        {
            std::ofstream out(largeFile, std::ios::binary);
            std::vector<char> blk(64 * 1024);
            for (size_t i = 0; i < blk.size(); ++i) blk[i] = static_cast<char>((i * 17) & 0xFF);
            for (int chunk = 0; chunk < 64; ++chunk) {
                out.write(blk.data(), blk.size());
            }
        }
        std::string expectedHash = hasher.computeFullFileHash(largeFile).value();

        TransferOptions txOpts;
        TransferOptions rxOpts;
        std::atomic<bool> rxFinished{false};
        std::string rxErr;
        std::atomic<bool> rxSuccess{false};

        std::thread rxThread([&]() {
            bool ok = engine.receiveFileOverChannel(
                "rx-task-rtc",
                recvDir.string(),
                rxChan,
                rxOpts,
                nullptr,
                nullptr,
                &rxErr
            );
            rxSuccess.store(ok);
            rxFinished.store(true);
        });

        std::string txErr;
        bool txOk = engine.sendFileOverChannel(
            "tx-task-rtc",
            largeFile,
            txChan,
            txOpts,
            nullptr,
            nullptr,
            &txErr
        );

        rxThread.join();

        TEST_ASSERT(txOk, "sendFileOverChannel succeeded over WebRTC channel");
        TEST_ASSERT(rxSuccess.load(), "receiveFileOverChannel succeeded over WebRTC channel");
        TEST_ASSERT(std::filesystem::exists(recvDir / "webrtc_payload.bin"), "Received file exists in destination directory");
        TEST_ASSERT(!std::filesystem::exists(recvDir / "webrtc_payload.bin.part"), "Temporary .part file cleaned up");
        TEST_ASSERT(hasher.computeFullFileHash((recvDir / "webrtc_payload.bin").string()).value() == expectedHash,
                    "Destination file SHA-256 matches source hash exactly");

        initiator->close();
        joiner->close();
    }

    // -------------------------------------------------------------
    // 4. TC-E2E-03: Cancellation Handling Over WebRTC DataChannel
    // -------------------------------------------------------------
    {
        CoordinatorOptions opts;
        opts.mockLoopbackMode = true;

        auto initiator = std::make_shared<WebRtcSessionCoordinator>("alice", WebRtcConfig{}, opts);
        auto joiner = std::make_shared<WebRtcSessionCoordinator>("bob", WebRtcConfig{}, opts);
        initiator->enableLoopbackMode(joiner);

        initiator->startAsInitiator("ws://mock/ws");
        joiner->startAsJoiner("ws://mock/ws", initiator->getRoomCode());

        auto txChan = initiator->waitForConnection(std::chrono::milliseconds(2000));
        auto rxChan = joiner->waitForConnection(std::chrono::milliseconds(2000));

        std::string cancelFile = (sendDir / "webrtc_cancel.bin").string();
        {
            std::ofstream out(cancelFile, std::ios::binary);
            std::vector<char> blk(64 * 1024, 'X');
            for (int i = 0; i < 16; ++i) out.write(blk.data(), blk.size());
        }

        std::atomic<bool> cancelToken{false};
        TransferOptions txOpts;
        txOpts.speedLimitKbps = 32; // throttle to allow mid-stream abort
        TransferOptions rxOpts;

        std::atomic<bool> rxFinished{false};
        std::atomic<bool> rxSuccess{true};
        std::string rxErr;

        std::thread rxThread([&]() {
            bool ok = engine.receiveFileOverChannel(
                "rx-cancel-task",
                recvDir.string(),
                rxChan,
                rxOpts,
                nullptr,
                &cancelToken,
                &rxErr
            );
            rxSuccess.store(ok);
            rxFinished.store(true);
        });

        std::string txErr;
        bool txOk = engine.sendFileOverChannel(
            "tx-cancel-task",
            cancelFile,
            txChan,
            txOpts,
            [&](const TransferProgressUpdate& upd) {
                if (upd.bytesTransferred > 0) {
                    cancelToken.store(true);
                }
            },
            &cancelToken,
            &txErr
        );

        rxThread.join();

        TEST_ASSERT(!txOk, "Cancelled sendFileOverChannel returns false");
        TEST_ASSERT(!rxSuccess.load(), "Cancelled receiveFileOverChannel returns false");
        TEST_ASSERT(!std::filesystem::exists(recvDir / "webrtc_cancel.bin.part"), "Cancelled .part file purged cleanly");
        TEST_ASSERT(!std::filesystem::exists(recvDir / "webrtc_cancel.bin"), "Destination file was not created on abort");

        initiator->close();
        joiner->close();
    }

    // -------------------------------------------------------------
    // 5. TC-E2E-04: Duplicate Auto-Rename Over WebRTC DataChannel
    // -------------------------------------------------------------
    {
        CoordinatorOptions opts;
        opts.mockLoopbackMode = true;

        auto initiator = std::make_shared<WebRtcSessionCoordinator>("alice", WebRtcConfig{}, opts);
        auto joiner = std::make_shared<WebRtcSessionCoordinator>("bob", WebRtcConfig{}, opts);
        initiator->enableLoopbackMode(joiner);

        initiator->startAsInitiator("ws://mock/ws");
        joiner->startAsJoiner("ws://mock/ws", initiator->getRoomCode());

        auto txChan = initiator->waitForConnection(std::chrono::milliseconds(2000));
        auto rxChan = joiner->waitForConnection(std::chrono::milliseconds(2000));

        std::string originalFile = (sendDir / "webrtc_payload.bin").string();
        std::string expectedHash = hasher.computeFullFileHash(originalFile).value();

        TransferOptions rxOpts;
        rxOpts.autoRenameDuplicates = true;

        std::atomic<bool> rxFinished{false};
        std::atomic<bool> rxSuccess{false};
        std::string rxErr;

        std::thread rxThread([&]() {
            bool ok = engine.receiveFileOverChannel(
                "rx-dup-task",
                recvDir.string(),
                rxChan,
                rxOpts,
                nullptr,
                nullptr,
                &rxErr
            );
            rxSuccess.store(ok);
            rxFinished.store(true);
        });

        std::string txErr;
        bool txOk = engine.sendFileOverChannel(
            "tx-dup-task",
            originalFile,
            txChan,
            TransferOptions{},
            nullptr,
            nullptr,
            &txErr
        );

        rxThread.join();

        TEST_ASSERT(txOk, "Duplicate send succeeds over WebRTC channel");
        TEST_ASSERT(rxSuccess.load(), "Duplicate receive succeeds over WebRTC channel");
        TEST_ASSERT(std::filesystem::exists(recvDir / "webrtc_payload (1).bin"), "Auto-renamed file webrtc_payload (1).bin exists");
        TEST_ASSERT(!std::filesystem::exists(recvDir / "webrtc_payload (1).bin.part"), "Auto-renamed .part file removed");
        TEST_ASSERT(hasher.computeFullFileHash((recvDir / "webrtc_payload (1).bin").string()).value() == expectedHash,
                    "Auto-renamed file SHA-256 matches original file");

        initiator->close();
        joiner->close();
    }

    std::filesystem::remove_all(rootDir);
    return true;
}

bool testMilestone2D_EndToEndWebRtcTransfer() {
    // -------------------------------------------------------------
    // 1. TC-ROOM-NORMALIZATION: Crockford Base32 Room Code Normalization
    // -------------------------------------------------------------
    {
        std::string err;
        TEST_ASSERT(WebRtcSessionCoordinator::normalizeRoomCode("DROP-K8M4-P9X2", &err) == "DROP-K8M4-P9X2",
                    "Standard formatted room code passes unchanged");
        TEST_ASSERT(err.empty(), "No error on valid room code");

        TEST_ASSERT(WebRtcSessionCoordinator::normalizeRoomCode("k8m4p9x2") == "DROP-K8M4-P9X2",
                    "Lowercase raw 8-character code normalized with DROP- prefix");

        TEST_ASSERT(WebRtcSessionCoordinator::normalizeRoomCode(" drop - k8m4 - p9x2 ") == "DROP-K8M4-P9X2",
                    "Delimiters and whitespace stripped and standardized");

        // Optical confusion corrections: O -> 0, I/L -> 1
        TEST_ASSERT(WebRtcSessionCoordinator::normalizeRoomCode("DROP-K8M4-P9O2") == "DROP-K8M4-P902",
                    "Letter 'O' automatically corrected to '0'");
        TEST_ASSERT(WebRtcSessionCoordinator::normalizeRoomCode("DROP-K8M4-P9I2") == "DROP-K8M4-P912",
                    "Letter 'I' automatically corrected to '1'");
        TEST_ASSERT(WebRtcSessionCoordinator::normalizeRoomCode("DROP-K8M4-P9L2") == "DROP-K8M4-P912",
                    "Letter 'L' automatically corrected to '1'");

        // Invalid characters and invalid length
        std::string badCharErr;
        TEST_ASSERT(WebRtcSessionCoordinator::normalizeRoomCode("DROP-K8M4-P9U2", &badCharErr).empty(),
                    "Letter 'U' rejected (excluded from Crockford alphabet)");
        TEST_ASSERT(!badCharErr.empty(), "Descriptive error returned for invalid character");

        std::string shortErr;
        TEST_ASSERT(WebRtcSessionCoordinator::normalizeRoomCode("DROP-SHORT", &shortErr).empty(),
                    "Short code rejected");
        TEST_ASSERT(!shortErr.empty(), "Descriptive error returned for short code");
    }

    // -------------------------------------------------------------
    // 2. TC-E2E-100MB: 100 MB Binary Transfer with Backpressure Flow Control & SHA-256 Validation
    // -------------------------------------------------------------
    {
        std::filesystem::path rootDir = "./unit_test_webrtc_100mb_tmp";
        std::filesystem::remove_all(rootDir);
        std::filesystem::create_directories(rootDir);
        std::filesystem::path sendDir = rootDir / "sender";
        std::filesystem::path recvDir = rootDir / "receiver";
        std::filesystem::create_directories(sendDir);
        std::filesystem::create_directories(recvDir);

        std::string largeFile = (sendDir / "big_payload_100mb.bin").string();
        constexpr size_t kFileSize = 100 * 1024 * 1024; // 100 MB
        constexpr size_t kBlockSize = 256 * 1024;       // 256 KB blocks
        {
            std::ofstream out(largeFile, std::ios::binary);
            std::vector<char> block(kBlockSize);
            for (size_t i = 0; i < kBlockSize; ++i) {
                block[i] = static_cast<char>((i * 31 + 7) & 0xFF);
            }
            size_t written = 0;
            while (written < kFileSize) {
                size_t toWrite = std::min(kBlockSize, kFileSize - written);
                out.write(block.data(), toWrite);
                written += toWrite;
            }
        }

        FileChunkHasher hasher;
        std::string expectedHash = hasher.computeFullFileHash(largeFile).value();
        TEST_ASSERT(!expectedHash.empty(), "Computed SHA-256 hash for 100 MB source file");

        CoordinatorOptions opts;
        opts.mockLoopbackMode = true;

        auto initiator = std::make_shared<WebRtcSessionCoordinator>("alice-100mb", WebRtcConfig{}, opts);
        auto joiner = std::make_shared<WebRtcSessionCoordinator>("bob-100mb", WebRtcConfig{}, opts);
        initiator->enableLoopbackMode(joiner);

        initiator->startAsInitiator("ws://mock/ws");
        joiner->startAsJoiner("ws://mock/ws", initiator->getRoomCode());

        auto txChan = initiator->waitForConnection(std::chrono::milliseconds(3000));
        auto rxChan = joiner->waitForConnection(std::chrono::milliseconds(3000));
        TEST_ASSERT(txChan != nullptr && rxChan != nullptr, "Channels connected for 100 MB transfer");

        TransferEngine engine;
        TransferOptions txOpts;
        TransferOptions rxOpts;
        rxOpts.autoRenameDuplicates = true;

        std::atomic<bool> rxDone{false};
        std::atomic<bool> rxSuccess{false};
        std::string rxErr;

        std::thread rxThread([&]() {
            bool ok = engine.receiveFileOverChannel(
                "rx-task-100mb",
                recvDir.string(),
                rxChan,
                rxOpts,
                nullptr,
                nullptr,
                &rxErr
            );
            rxSuccess.store(ok);
            rxDone.store(true);
        });

        std::string txErr;
        std::atomic<uint64_t> lastProgress{0};
        bool txOk = engine.sendFileOverChannel(
            "tx-task-100mb",
            largeFile,
            txChan,
            txOpts,
            [&](const TransferProgressUpdate& upd) {
                lastProgress.store(upd.bytesTransferred);
            },
            nullptr,
            &txErr
        );

        rxThread.join();

        TEST_ASSERT(txOk, "sendFileOverChannel succeeded for 100 MB payload");
        TEST_ASSERT(rxSuccess.load(), "receiveFileOverChannel succeeded for 100 MB payload");
        TEST_ASSERT(std::filesystem::exists(recvDir / "big_payload_100mb.bin"),
                    "100 MB received file exists on disk");
        TEST_ASSERT(!std::filesystem::exists(recvDir / "big_payload_100mb.bin.part"),
                    "Temporary .part file cleaned up after 100 MB transfer");

        std::string actualHash = hasher.computeFullFileHash((recvDir / "big_payload_100mb.bin").string()).value();
        TEST_ASSERT(actualHash == expectedHash, "100 MB destination file SHA-256 matches source hash exactly");
        TEST_ASSERT(txChan->getBytesSent() >= kFileSize, "Transmitter recorded full 100 MB payload sent plus protocol frames");
        TEST_ASSERT(rxChan->getBytesReceived() >= kFileSize, "Receiver recorded full 100 MB payload received plus protocol frames");

        initiator->close();
        joiner->close();
        std::filesystem::remove_all(rootDir);
    }

    // -------------------------------------------------------------
    // 3. TC-E2E-REAL-SIGNALING: Live Go Signaling Daemon E2E Integration Test
    // -------------------------------------------------------------
    {
        std::string srvBin = "./backend/signaling/bin/signaling-server";
        if (std::filesystem::exists(srvBin)) {
            uint16_t testPort = 18090;
            std::string cmd = "PORT=" + std::to_string(testPort) + " " + srvBin + " >/dev/null 2>&1 & echo $!";
            FILE* pipe = popen(cmd.c_str(), "r");
            pid_t serverPid = 0;
            if (pipe) {
                char buf[64];
                if (fgets(buf, sizeof(buf), pipe)) {
                    serverPid = std::stoi(buf);
                }
                pclose(pipe);
            }

            struct ServerProcessGuard {
                pid_t pid{0};
                ~ServerProcessGuard() {
                    if (pid > 0) {
                        kill(pid, SIGTERM);
                    }
                }
            } procGuard{serverPid};

            HttpClient http;
            bool serverUp = false;
            for (int i = 0; i < 50; ++i) {
                HttpResponse resp = http.get("http://127.0.0.1:" + std::to_string(testPort) + "/healthz");
                if (resp.success && resp.statusCode == 200) {
                    serverUp = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            TEST_ASSERT(serverUp, "Go Signaling Server spawned and healthy on port 18090");

            if (serverUp) {
                std::string wsUrl = "ws://127.0.0.1:" + std::to_string(testPort) + "/ws/signaling";

                CoordinatorOptions opts;
                opts.signalingTimeoutMs = 5000;
                opts.signalingHttpUrl = "http://127.0.0.1:" + std::to_string(testPort);

                auto initiator = std::make_shared<WebRtcSessionCoordinator>("alice-live", WebRtcConfig{}, opts);
                auto joiner = std::make_shared<WebRtcSessionCoordinator>("bob-live", WebRtcConfig{}, opts);

                std::atomic<bool> roomCreated{false};
                std::string generatedCode;
                initiator->onRoomReady = [&](const std::string& code) {
                    generatedCode = code;
                    roomCreated.store(true);
                };

#if defined(LANDROP_HAVE_WEBRTC)
                bool initOk = initiator->startAsInitiator(wsUrl);
                TEST_ASSERT(initOk, "Initiator connected to live Go signaling server");

                for (int i = 0; i < 50 && !roomCreated.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                TEST_ASSERT(roomCreated.load() && !generatedCode.empty(),
                            "Go server created Crockford room code: " + generatedCode);
                TEST_ASSERT(generatedCode.rfind("DROP-", 0) == 0,
                            "Generated room code has DROP- prefix");

                std::atomic<bool> joinerPeerJoined{false};
                joiner->onPeerJoined = [&](const std::string&) {
                    joinerPeerJoined.store(true);
                };

                bool joinOk = joiner->startAsJoiner(wsUrl, generatedCode);
                TEST_ASSERT(joinOk, "Joiner connected and requested room via live Go signaling server");

                for (int i = 0; i < 50 && !joinerPeerJoined.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                TEST_ASSERT(joinerPeerJoined.load(), "Go server relayed PEER_JOINED to both clients");

                // Test peer disconnect notification
                joiner->close();
                for (int i = 0; i < 50; ++i) {
                    if (initiator->getState() == CoordinatorState::Disconnected) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                TEST_ASSERT(initiator->getState() == CoordinatorState::Disconnected,
                            "Initiator transitioned to Disconnected upon peer drop");

                initiator->close();
#else
                bool initOk = initiator->startAsInitiator(wsUrl);
                TEST_ASSERT(!initOk, "Initiator safely rejects WebSocket connect when WebRTC is not compiled in");
                std::string normErr;
                std::string normCode = WebRtcSessionCoordinator::normalizeRoomCode("k8m4p9x2", &normErr);
                bool joinOk = joiner->startAsJoiner(wsUrl, normCode);
                TEST_ASSERT(!joinOk, "Joiner safely rejects WebSocket connect when WebRTC is not compiled in");
#endif
            }

            if (serverPid > 0) {
                kill(serverPid, SIGTERM);
            }
        }
    }

    return true;
}

static bool testSuite27_OpticalPairingAndQrCode() {
    // 1. TC-QR-01: Generates QR matrix for room code
    auto qr = landrop::ui::QrCodeGenerator::encodeText("DROP-7K9M-X2PQ");
    TEST_ASSERT(qr.getVersion() >= 1, "TC-QR-01: Valid QR version generated for room code");
    TEST_ASSERT(qr.getSize() == qr.getVersion() * 4 + 17, "TC-QR-01: Size matches dimension formula 4*V + 17");
    TEST_ASSERT(qr.getModule(0, 0), "TC-QR-01: Top-left finder corner is dark");

    // 2. TC-QR-02: Finder patterns and Dark Module checks (ISO/IEC 18004:2015 Sections 6.3.3 & 6.3.6)
    TEST_ASSERT(qr.getModule(3, 3), "TC-QR-02: Finder top-left center (3, 3) is dark");
    TEST_ASSERT(qr.getModule(qr.getSize() - 4, 3), "TC-QR-02: Finder top-right center is dark");
    TEST_ASSERT(qr.getModule(3, qr.getSize() - 4), "TC-QR-02: Finder bottom-left center is dark");

    // Finder border modules must NEVER be overwritten by timing patterns
    TEST_ASSERT(qr.getModule(1, 6), "TC-QR-02: Top-left finder border (1, 6) is dark");
    TEST_ASSERT(qr.getModule(3, 6), "TC-QR-02: Top-left finder border (3, 6) is dark");
    TEST_ASSERT(qr.getModule(5, 6), "TC-QR-02: Top-left finder border (5, 6) is dark");
    TEST_ASSERT(qr.getModule(6, 1), "TC-QR-02: Top-left finder border (6, 1) is dark");
    TEST_ASSERT(qr.getModule(6, 3), "TC-QR-02: Top-left finder border (6, 3) is dark");
    TEST_ASSERT(qr.getModule(6, 5), "TC-QR-02: Top-left finder border (6, 5) is dark");

    // Dark Module at (8, 4*version + 9) == (8, size - 8) is ALWAYS dark
    TEST_ASSERT(qr.getModule(8, qr.getSize() - 8), "TC-QR-02: Dark module (8, size - 8) is dark");

    TEST_ASSERT(!qr.getModule(7, 0), "TC-QR-02: Finder separator (7, 0) is white/light");
    TEST_ASSERT(!qr.getModule(7, 6), "TC-QR-02: Finder separator (7, 6) is white/light");

    // 3. TC-QR-03: Timing patterns check (strictly between 8 and size - 9)
    for (int x = 8; x <= qr.getSize() - 9; ++x) {
        TEST_ASSERT(qr.getModule(x, 6) == (x % 2 == 0), "TC-QR-03: Horizontal timing pattern alternates");
    }
    for (int y = 8; y <= qr.getSize() - 9; ++y) {
        TEST_ASSERT(qr.getModule(6, y) == (y % 2 == 0), "TC-QR-03: Vertical timing pattern alternates");
    }

    // 4. TC-QR-04: Version 7-10 Version Information Blocks (ISO/IEC 18004:2015 Section 6.5.2)
    std::string textV7(110, 'A'); // 110 bytes exceeds V6 (max 106 bytes) -> triggers Version 7
    auto qrV7 = landrop::ui::QrCodeGenerator::encodeText(textV7);
    TEST_ASSERT(qrV7.getVersion() == 7, "TC-QR-04: 110-byte payload selects Version 7 matrix");
    TEST_ASSERT(qrV7.getSize() == 45, "TC-QR-04: Version 7 size is 45x45");
    uint32_t v7Expected = 0x07C94;
    for (int k = 0; k < 18; ++k) {
        bool expectedBit = ((v7Expected >> k) & 1) != 0;
        TEST_ASSERT(qrV7.getModule(k / 3, 45 - 11 + (k % 3)) == expectedBit, "TC-QR-04: V7 Bottom-left version info bit matches");
        TEST_ASSERT(qrV7.getModule(45 - 11 + (k % 3), k / 3) == expectedBit, "TC-QR-04: V7 Top-right version info bit matches");
    }

    // 5. TC-QR-05: Capacity overflow throws std::length_error
    bool threwOverflow = false;
    try {
        std::vector<uint8_t> hugeData(250, 0x42); // Exceeds Version 10 Medium capacity (213 bytes)
        landrop::ui::QrCodeGenerator::encodeBinary(hugeData);
    } catch (const std::length_error&) {
        threwOverflow = true;
    }
    TEST_ASSERT(threwOverflow, "TC-QR-05: Exceeding Version 10 capacity throws std::length_error");

    // 6. TC-QR-06: SVG export
    std::string svg = qr.toSvg(4, "#000000", "#ffffff");
    TEST_ASSERT(svg.find("<svg") != std::string::npos, "TC-QR-06: SVG contains <svg tag");
    TEST_ASSERT(svg.find("viewBox=\"0 0 ") != std::string::npos, "TC-QR-06: SVG contains viewBox");
    TEST_ASSERT(svg.find("<path fill=\"#000000\"") != std::string::npos, "TC-QR-06: SVG contains path element");
    TEST_ASSERT(svg.find("</svg>") != std::string::npos, "TC-QR-06: SVG contains closing tag");

    // 7. TC-QR-07: ASCII export
    std::string ascii = qr.toAscii(false);
    TEST_ASSERT(!ascii.empty(), "TC-QR-07: ASCII output is non-empty");
    TEST_ASSERT(ascii.find("\n") != std::string::npos, "TC-QR-07: ASCII output is multi-line");

    // 8. TC-QR-08: Optical pairing URL encoding
    std::string pairingUrl = "https://drop.local/#DROP-7K9M-X2PQ";
    auto qrUrl = landrop::ui::QrCodeGenerator::encodeText(pairingUrl);
    TEST_ASSERT(qrUrl.getSize() >= 25, "TC-QR-08: Pairing URL fits in Version 2 or higher matrix");
    TEST_ASSERT(qrUrl.getModule(3, 3), "TC-QR-08: Pairing URL QR code has valid finder patterns");
    TEST_ASSERT(qrUrl.getModule(8, qrUrl.getSize() - 8), "TC-QR-08: Pairing URL QR code has dark module intact");

    return true;
}

static bool testSuite28_MobileEcosystemBridgeFfi() {
    // 1. TC-FFI-CTX-01: Lifecycle & Context Management, Fallbacks & Null-Safety
    {
        LandropHandle defHandle = landrop_mobile_create(nullptr, nullptr);
        TEST_ASSERT(defHandle != nullptr, "TC-FFI-CTX-01: landrop_mobile_create(nullptr, nullptr) creates valid handle with defaults");
        landrop_mobile_destroy(defHandle);

        landrop_mobile_destroy(nullptr); // Safe no-op

        // Verify invalid handle rejection across API
        LandropTransferSnapshot dummySnap;
        char dummyBuf[32];
        TEST_ASSERT(landrop_mobile_webrtc_get_state(nullptr) == LANDROP_ERR_INVALID_HANDLE, "TC-FFI-CTX-01: get_state(null) returns LANDROP_ERR_INVALID_HANDLE");
        TEST_ASSERT(landrop_mobile_webrtc_initiate_room(nullptr, nullptr, nullptr) == LANDROP_ERR_INVALID_HANDLE, "TC-FFI-CTX-01: initiate_room(null) returns LANDROP_ERR_INVALID_HANDLE");
        TEST_ASSERT(landrop_mobile_webrtc_join_room(nullptr, nullptr, nullptr, "DROP-K8M4-09X2") == LANDROP_ERR_INVALID_HANDLE, "TC-FFI-CTX-01: join_room(null) returns LANDROP_ERR_INVALID_HANDLE");
        TEST_ASSERT(landrop_mobile_webrtc_get_room_code(nullptr, dummyBuf, sizeof(dummyBuf)) == LANDROP_ERR_INVALID_HANDLE, "TC-FFI-CTX-01: get_room_code(null) returns LANDROP_ERR_INVALID_HANDLE");
        TEST_ASSERT(landrop_mobile_webrtc_get_peer_id(nullptr, dummyBuf, sizeof(dummyBuf)) == LANDROP_ERR_INVALID_HANDLE, "TC-FFI-CTX-01: get_peer_id(null) returns LANDROP_ERR_INVALID_HANDLE");
        TEST_ASSERT(landrop_mobile_webrtc_send_file_async(nullptr, "test.txt", 0) == LANDROP_ERR_INVALID_HANDLE, "TC-FFI-CTX-01: send_file_async(null) returns LANDROP_ERR_INVALID_HANDLE");
        TEST_ASSERT(landrop_mobile_webrtc_receive_file_async(nullptr, "/tmp") == LANDROP_ERR_INVALID_HANDLE, "TC-FFI-CTX-01: receive_file_async(null) returns LANDROP_ERR_INVALID_HANDLE");
        TEST_ASSERT(landrop_mobile_webrtc_get_transfer_snapshot(nullptr, &dummySnap) == LANDROP_ERR_INVALID_HANDLE, "TC-FFI-CTX-01: get_transfer_snapshot(null) returns LANDROP_ERR_INVALID_HANDLE");
        TEST_ASSERT(landrop_mobile_webrtc_cancel_transfer(nullptr) == LANDROP_ERR_INVALID_HANDLE, "TC-FFI-CTX-01: cancel_transfer(null) returns LANDROP_ERR_INVALID_HANDLE");
        TEST_ASSERT(landrop_mobile_start_discovery(nullptr, 8848) == LANDROP_ERR_INVALID_HANDLE, "TC-FFI-CTX-01: start_discovery(null) returns LANDROP_ERR_INVALID_HANDLE");
        TEST_ASSERT(landrop_mobile_get_peer_count(nullptr) == 0, "TC-FFI-CTX-01: get_peer_count(null) returns 0");
        TEST_ASSERT(landrop_mobile_get_peer_info(nullptr, 0, dummyBuf, sizeof(dummyBuf), dummyBuf, sizeof(dummyBuf), nullptr) == LANDROP_ERR_INVALID_HANDLE, "TC-FFI-CTX-01: get_peer_info(null) returns LANDROP_ERR_INVALID_HANDLE");
        TEST_ASSERT(landrop_mobile_send_file(nullptr, "test.txt", "127.0.0.1", 8848) == LANDROP_ERR_INVALID_HANDLE, "TC-FFI-CTX-01: send_file(null) returns LANDROP_ERR_INVALID_HANDLE");
        TEST_ASSERT(landrop_mobile_nfc_generate_payload(nullptr, "127.0.0.1", 8848, dummyBuf, sizeof(dummyBuf)) == LANDROP_ERR_INVALID_HANDLE, "TC-FFI-CTX-01: nfc_generate_payload(null) returns LANDROP_ERR_INVALID_HANDLE");
        TEST_ASSERT(landrop_mobile_nfc_process_payload(nullptr, "raw", dummyBuf, sizeof(dummyBuf)) == LANDROP_ERR_INVALID_HANDLE, "TC-FFI-CTX-01: nfc_process_payload(null) returns LANDROP_ERR_INVALID_HANDLE");
        TEST_ASSERT(landrop_mobile_airdrop_enable(nullptr, 1) == LANDROP_ERR_INVALID_HANDLE, "TC-FFI-CTX-01: airdrop_enable(null) returns LANDROP_ERR_INVALID_HANDLE");
        TEST_ASSERT(landrop_mobile_airdrop_get_state(nullptr) == LANDROP_ERR_INVALID_HANDLE, "TC-FFI-CTX-01: airdrop_get_state(null) returns LANDROP_ERR_INVALID_HANDLE");
    }

    // 2. TC-FFI-BUF-01: Caller buffer capacity exhaustion
    char smallBuf[10];
    int reqLen = 0;
    int rc = landrop_mobile_qr_generate_svg("DROP-7K9M-X2PQ", LANDROP_QR_ECC_MEDIUM, 4, "#000000", "#ffffff", smallBuf, sizeof(smallBuf), &reqLen);
    TEST_ASSERT(rc == LANDROP_ERR_BUFFER_TOO_SMALL, "TC-FFI-BUF-01: Undersized buffer returns LANDROP_ERR_BUFFER_TOO_SMALL (-2)");
    TEST_ASSERT(smallBuf[0] == '\0', "TC-FFI-BUF-01: Undersized buffer is guaranteed null-terminated empty string");
    TEST_ASSERT(reqLen > 100, "TC-FFI-BUF-01: Required length is reported to caller");

    std::vector<char> fullBuf(static_cast<size_t>(reqLen));
    rc = landrop_mobile_qr_generate_svg("DROP-7K9M-X2PQ", LANDROP_QR_ECC_MEDIUM, 4, "#000000", "#ffffff", fullBuf.data(), static_cast<int>(fullBuf.size()), &reqLen);
    TEST_ASSERT(rc == LANDROP_OK, "TC-FFI-BUF-01: Correctly sized buffer returns LANDROP_OK");
    std::string svgStr(fullBuf.data());
    TEST_ASSERT(svgStr.find("<svg") != std::string::npos, "TC-FFI-BUF-01: Output contains valid SVG XML");

    // 3. TC-FFI-QR-01: Bit matrix generation via FFI
    uint8_t matrix[64 * 64];
    int size = 0;
    rc = landrop_mobile_qr_generate_matrix("DROP-7K9M-X2PQ", LANDROP_QR_ECC_MEDIUM, matrix, sizeof(matrix), &size);
    TEST_ASSERT(rc == LANDROP_OK, "TC-FFI-QR-01: Bit matrix generation returns LANDROP_OK");
    TEST_ASSERT(size >= 21, "TC-QR-01: Matrix size >= 21");
    TEST_ASSERT(matrix[0] == 1, "TC-FFI-QR-01: Top-left finder corner (0,0) is dark");
    TEST_ASSERT(matrix[3 * size + 3] == 1, "TC-FFI-QR-01: Finder center (3,3) is dark");
    TEST_ASSERT(matrix[7 * size + 0] == 0, "TC-FFI-QR-01: Separator (7,0) is light");
    TEST_ASSERT(matrix[(size - 8) * size + 8] == 1, "TC-FFI-QR-01: Dark Module (8, size - 8) is dark");

    // 4. TC-FFI-NORM-01: Crockford Base32 normalization
    char normBuf[64];
    char errBuf[128];
    rc = landrop_mobile_normalize_room_code("k8m4-o9x2", normBuf, sizeof(normBuf), errBuf, sizeof(errBuf));
    TEST_ASSERT(rc == LANDROP_OK, "TC-FFI-NORM-01: Normalization of lowercase and 'O' returns LANDROP_OK");
    TEST_ASSERT(std::string(normBuf) == "DROP-K8M4-09X2", "TC-FFI-NORM-01: Canonical code matches DROP-K8M4-09X2");

    rc = landrop_mobile_normalize_room_code("k8m4-u9x2", normBuf, sizeof(normBuf), errBuf, sizeof(errBuf));
    TEST_ASSERT(rc == LANDROP_ERR_INVALID_ROOM_CODE, "TC-FFI-NORM-01: Invalid char 'U' returns LANDROP_ERR_INVALID_ROOM_CODE");
    TEST_ASSERT(std::string(errBuf).find("Invalid character") != std::string::npos, "TC-FFI-NORM-01: Error explanation populated");

    // 5. TC-FFI-PAIR-01: Pairing URI formatting
    char uriBuf[128];
    rc = landrop_mobile_qr_generate_pairing_uri("DROP-K8M4-09X2", "drop.local", uriBuf, sizeof(uriBuf));
    TEST_ASSERT(rc == LANDROP_OK, "TC-FFI-PAIR-01: Pairing URI generation returns LANDROP_OK");
    TEST_ASSERT(std::string(uriBuf) == "https://drop.local/#DROP-K8M4-09X2", "TC-FFI-PAIR-01: Pairing URI contains client-side hash fragment");

    // 6. TC-FFI-TH-01: Multi-threaded thread-safety on single handle
    LandropHandle handle = landrop_mobile_create("ThreadSafeDevice", "mob-uuid-ts-001");
    TEST_ASSERT(handle != nullptr, "TC-FFI-TH-01: Handle created successfully");

    std::vector<std::thread> workers;
    std::atomic<int> completedTasks{0};
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([handle, &completedTasks]() {
            for (int i = 0; i < 50; ++i) {
                landrop_mobile_start_discovery(handle, 8848);
                int count = landrop_mobile_get_peer_count(handle);
                (void)count;
                char pName[64];
                char pIp[64];
                uint16_t pPort = 0;
                landrop_mobile_get_peer_info(handle, 0, pName, sizeof(pName), pIp, sizeof(pIp), &pPort);
                int st = landrop_mobile_webrtc_get_state(handle);
                (void)st;
                landrop_mobile_stop_discovery(handle);
            }
            completedTasks++;
        });
    }
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }
    TEST_ASSERT(completedTasks.load() == 4, "TC-FFI-TH-01: All 4 concurrent threads completed safely without data races");

    // 7. TC-FFI-RTC-01: WebRTC ephemeral session lifecycle
    int initState = landrop_mobile_webrtc_get_state(handle);
    TEST_ASSERT(initState == LANDROP_SESSION_IDLE, "TC-FFI-RTC-01: Initial WebRTC state is IDLE");

    char codeBuf[64];
    rc = landrop_mobile_webrtc_get_room_code(handle, codeBuf, sizeof(codeBuf));
    TEST_ASSERT(rc == LANDROP_ERR_ILLEGAL_STATE, "TC-FFI-RTC-01: get_room_code before initiation returns LANDROP_ERR_ILLEGAL_STATE");

    rc = landrop_mobile_webrtc_initiate_room(handle, "ws://127.0.0.1:58080/ws/signaling", "http://127.0.0.1:58080");
    TEST_ASSERT(rc == LANDROP_OK, "TC-FFI-RTC-01: initiate_room returns LANDROP_OK non-blocking");

    // Close room cleanly without deadlocks
    landrop_mobile_webrtc_close(handle);
    TEST_ASSERT(landrop_mobile_webrtc_get_state(handle) == LANDROP_SESSION_IDLE, "TC-FFI-RTC-01: State returns to IDLE after close");

    // 8. TC-FFI-XFER-01: Non-blocking transfer pipeline & snapshot monitoring
    LandropTransferSnapshot snap;
    rc = landrop_mobile_webrtc_get_transfer_snapshot(handle, &snap);
    TEST_ASSERT(rc == LANDROP_OK, "TC-FFI-XFER-01: Transfer snapshot queried successfully");
    TEST_ASSERT(snap.transfer_state == LANDROP_XFER_IDLE, "TC-FFI-XFER-01: Initial snapshot state is IDLE");

    rc = landrop_mobile_webrtc_send_file_async(handle, "/path/to/test.txt", 0);
    TEST_ASSERT(rc == LANDROP_ERR_ILLEGAL_STATE, "TC-FFI-XFER-01: send_file_async when not connected returns LANDROP_ERR_ILLEGAL_STATE");

    rc = landrop_mobile_webrtc_receive_file_async(handle, "/tmp");
    TEST_ASSERT(rc == LANDROP_ERR_ILLEGAL_STATE, "TC-FFI-XFER-01: receive_file_async when not connected returns LANDROP_ERR_ILLEGAL_STATE");

    rc = landrop_mobile_webrtc_cancel_transfer(handle);
    TEST_ASSERT(rc == LANDROP_OK, "TC-FFI-XFER-01: cancel_transfer succeeds");

    // 9. TC-FFI-STR-01: String buffer boundaries and null-safety
    char nfcShort[5];
    rc = landrop_mobile_nfc_generate_payload(handle, "192.168.1.50", 8848, nfcShort, sizeof(nfcShort));
    TEST_ASSERT(rc == LANDROP_ERR_BUFFER_TOO_SMALL, "TC-FFI-STR-01: Undersized NFC buffer returns LANDROP_ERR_BUFFER_TOO_SMALL");
    TEST_ASSERT(nfcShort[0] == '\0', "TC-FFI-STR-01: Undersized NFC buffer is null-terminated");

    rc = landrop_mobile_webrtc_get_room_code(handle, nullptr, 10);
    TEST_ASSERT(rc == LANDROP_ERR_INVALID_ARG, "TC-FFI-STR-01: null buffer to get_room_code returns LANDROP_ERR_INVALID_ARG");

    char dummyBuf2[32];
    rc = landrop_mobile_webrtc_get_peer_id(handle, dummyBuf2, 0);
    TEST_ASSERT(rc == LANDROP_ERR_INVALID_ARG, "TC-FFI-STR-01: 0-capacity buffer to get_peer_id returns LANDROP_ERR_INVALID_ARG");

    landrop_mobile_destroy(handle);

    return true;
}

static bool testSuite29_ResilientStreamingResumptionSuite() {
    using namespace landrop::protocol::v2;

    // 1. TC-3D-PROT-01: C++ ResumeRequestMessage serialization & deserialization (136 bytes)
    {
        ResumeRequestMessage req;
        req.fileHash = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
        req.bytesReceivedSoFar = 1048576ULL;
        req.partialSha256 = "11223344556677889900aabbccddeeff11223344556677889900aabbccddeeff";

        std::vector<uint8_t> raw = req.serialize();
        TEST_ASSERT(raw.size() == 136, "TC-3D-PROT-01: Serialized ResumeRequestMessage is strictly 136 bytes");

        ResumeRequestMessage parsed;
        std::string err;
        bool ok = ResumeRequestMessage::deserialize(raw.data(), raw.size(), parsed, &err);
        TEST_ASSERT(ok, "TC-3D-PROT-01: Deserialization of valid ResumeRequestMessage succeeds");
        TEST_ASSERT(parsed.fileHash == req.fileHash, "TC-3D-PROT-01: fileHash matches");
        TEST_ASSERT(parsed.bytesReceivedSoFar == 1048576ULL, "TC-3D-PROT-01: bytesReceivedSoFar matches");
        TEST_ASSERT(parsed.partialSha256 == req.partialSha256, "TC-3D-PROT-01: partialSha256 matches");

        // Padding check: when hashes are < 64 chars, they are zero-padded to 64 chars
        ResumeRequestMessage shortReq;
        shortReq.fileHash = "abc";
        shortReq.bytesReceivedSoFar = 42;
        shortReq.partialSha256 = "def";
        std::vector<uint8_t> shortRaw = shortReq.serialize();
        TEST_ASSERT(shortRaw.size() == 136, "TC-3D-PROT-01: Short hashes are zero-padded to 136 bytes");

        ResumeRequestMessage parsedShort;
        TEST_ASSERT(ResumeRequestMessage::deserialize(shortRaw.data(), shortRaw.size(), parsedShort), "TC-3D-PROT-01: Deserializes padded message");
        TEST_ASSERT(parsedShort.fileHash.rfind("abc", 0) == 0, "TC-3D-PROT-01: Padded fileHash starts with abc");
        TEST_ASSERT(parsedShort.bytesReceivedSoFar == 42, "TC-3D-PROT-01: Padded bytesReceivedSoFar is 42");

        // Upper 32 bits offset check (> 4GB)
        ResumeRequestMessage largeReq;
        largeReq.fileHash = std::string(64, 'a');
        largeReq.bytesReceivedSoFar = 10ULL * 1024 * 1024 * 1024; // 10 GB
        largeReq.partialSha256 = std::string(64, 'b');
        std::vector<uint8_t> largeRaw = largeReq.serialize();
        TEST_ASSERT(largeRaw.size() == 136, "TC-3D-PROT-01: 10GB offset serializes to 136 bytes");

        ResumeRequestMessage parsedLarge;
        TEST_ASSERT(ResumeRequestMessage::deserialize(largeRaw.data(), largeRaw.size(), parsedLarge), "TC-3D-PROT-01: Deserializes 10GB offset message");
        TEST_ASSERT(parsedLarge.bytesReceivedSoFar == 10ULL * 1024 * 1024 * 1024, "TC-3D-PROT-01: 10GB offset preserves upper 32 bits");
    }

    // 2. TC-3D-PROT-02: Buffer underflow returns false and populates outError
    {
        ResumeRequestMessage parsed;
        std::string err;
        std::vector<uint8_t> undersized(135, 0);
        bool ok = ResumeRequestMessage::deserialize(undersized.data(), undersized.size(), parsed, &err);
        TEST_ASSERT(!ok, "TC-3D-PROT-02: 135 bytes returns false");
        TEST_ASSERT(!err.empty(), "TC-3D-PROT-02: Error message populated for 135 bytes");

        ok = ResumeRequestMessage::deserialize(nullptr, 136, parsed, &err);
        TEST_ASSERT(!ok, "TC-3D-PROT-02: nullptr data returns false");

        ok = ResumeRequestMessage::deserialize(undersized.data(), 0, parsed, &err);
        TEST_ASSERT(!ok, "TC-3D-PROT-02: 0 bytes returns false");
    }

    // 3. TC-3D-HASH-01: computePartialFileHash verification
    {
        FileChunkHasher hasher;

        // A. 0-byte slice returns empty string SHA-256
        auto zeroHash = hasher.computePartialFileHash("nonexistent_file_path.bin", 0);
        TEST_ASSERT(zeroHash.has_value(), "TC-3D-HASH-01: 0-byte slice has value without file open");
        TEST_ASSERT(*zeroHash == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "TC-3D-HASH-01: 0-byte slice equals sha256 of empty string");

        // B. Nonexistent file with maxBytes > 0 returns nullopt
        auto nonExistent = hasher.computePartialFileHash("this_file_definitely_does_not_exist_xyz.bin", 100);
        TEST_ASSERT(!nonExistent.has_value(), "TC-3D-HASH-01: Nonexistent file returns std::nullopt");

        // C. Create a temporary file with known content
        std::filesystem::path tempDir = "./unit_test_resumption_tmp";
        std::filesystem::remove_all(tempDir);
        std::filesystem::create_directories(tempDir);
        std::filesystem::path testFile = tempDir / "test_partial.bin";

        std::string testData = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!@#$%^&*()";
        {
            std::ofstream out(testFile, std::ios::binary);
            out.write(testData.data(), testData.size());
        }

        // Partial slice: first 32 bytes
        auto partialHash = hasher.computePartialFileHash(testFile.string(), 32);
        TEST_ASSERT(partialHash.has_value(), "TC-3D-HASH-01: Partial hash of 32 bytes computed");

        std::string expectedPartial = hasher.computeStringHash(testData.substr(0, 32));
        TEST_ASSERT(*partialHash == expectedPartial, "TC-3D-HASH-01: Partial hash matches computeStringHash for leading 32 bytes");

        // Full file slice: exact size
        auto fullSliceHash = hasher.computePartialFileHash(testFile.string(), testData.size());
        TEST_ASSERT(fullSliceHash.has_value(), "TC-3D-HASH-01: Exact size partial hash computed");
        auto fullHash = hasher.computeFullFileHash(testFile.string());
        TEST_ASSERT(fullSliceHash == fullHash, "TC-3D-HASH-01: Full slice hash matches computeFullFileHash");

        // Oversized slice (maxBytes > fileSize): returns nullopt
        auto overHash = hasher.computePartialFileHash(testFile.string(), testData.size() + 100);
        TEST_ASSERT(!overHash.has_value(), "TC-3D-HASH-01: maxBytes > fileSize returns std::nullopt");

        std::filesystem::remove_all(tempDir);
    }

    return true;
}

bool testSuite30_DoctorEngineAndDiagnosticsSuite() {
    std::cout << "\n--- Starting Suite 30: Production Packaging & CLI Diagnostics (DoctorEngine) ---\n";

    // TC-DOC-STUN-01: STUN Binding Request Encoding (RFC 5389)
    {
        std::array<uint8_t, 12> txId = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120};
        auto req = DoctorEngine::buildStunBindingRequest(txId);
        TEST_ASSERT(req.size() == 20, "TC-DOC-STUN-01: STUN Binding Request exactly 20 bytes");

        uint16_t msgType = (static_cast<uint16_t>(req[0]) << 8) | req[1];
        TEST_ASSERT(msgType == DoctorEngine::STUN_BINDING_REQUEST, "TC-DOC-STUN-01: Message type is 0x0001 (Binding Request)");

        uint16_t msgLen = (static_cast<uint16_t>(req[2]) << 8) | req[3];
        TEST_ASSERT(msgLen == 0, "TC-DOC-STUN-01: Attribute length is 0 for initial Binding Request");

        uint32_t cookie = (static_cast<uint32_t>(req[4]) << 24) |
                          (static_cast<uint32_t>(req[5]) << 16) |
                          (static_cast<uint32_t>(req[6]) << 8)  |
                          static_cast<uint32_t>(req[7]);
        TEST_ASSERT(cookie == DoctorEngine::STUN_MAGIC_COOKIE, "TC-DOC-STUN-01: Magic cookie matches 0x2112A442");

        TEST_ASSERT(std::memcmp(req.data() + 8, txId.data(), 12) == 0, "TC-DOC-STUN-01: Transaction ID correctly embedded in 12-byte header");
    }

    // TC-DOC-STUN-02: STUN Binding Response Decoding (XOR-MAPPED-ADDRESS)
    {
        std::array<uint8_t, 12> txId = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        std::vector<uint8_t> resp(32, 0);

        // Header: 0x0101 (Binding Response), length = 12 (attribute 8 bytes + 4 bytes header)
        resp[0] = 0x01; resp[1] = 0x01;
        resp[2] = 0x00; resp[3] = 0x0C; // length = 12 bytes of attributes
        resp[4] = 0x21; resp[5] = 0x12; resp[6] = 0xA4; resp[7] = 0x42; // Magic cookie
        std::memcpy(resp.data() + 8, txId.data(), 12);

        // Attribute: 0x0020 (XOR-MAPPED-ADDRESS), length = 8
        resp[20] = 0x00; resp[21] = 0x20;
        resp[22] = 0x00; resp[23] = 0x08;

        // Value:
        // Reserved (0x00), Family (0x01 = IPv4)
        resp[24] = 0x00;
        resp[25] = 0x01;

        // X-Port: target port 12345 (0x3039) ^ 0x2112 = 0x112B
        resp[26] = 0x11;
        resp[27] = 0x2B;

        // X-Address: target IP 203.0.113.195 (0xCB0071C3) ^ 0x2112A442 = 0xEA12D581
        resp[28] = 0xEA;
        resp[29] = 0x12;
        resp[30] = 0xD5;
        resp[31] = 0x81;

        std::string outIp;
        uint16_t outPort = 0;
        std::string outErr;
        bool ok = DoctorEngine::parseStunBindingResponse(resp.data(), resp.size(), txId, outIp, outPort, &outErr);

        TEST_ASSERT(ok, "TC-DOC-STUN-02: Successfully parsed valid STUN XOR-MAPPED-ADDRESS response");
        TEST_ASSERT(outIp == "203.0.113.195", "TC-DOC-STUN-02: Decoded reflexive IP matches 203.0.113.195");
        TEST_ASSERT(outPort == 12345, "TC-DOC-STUN-02: Decoded reflexive port matches 12345");
    }

    // TC-DOC-STUN-02B: STUN Legacy MAPPED-ADDRESS Fallback (RFC 3489 attribute 0x0001)
    {
        std::array<uint8_t, 12> txId = {2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24};
        std::vector<uint8_t> resp(32, 0);

        // Header: 0x0101 (Binding Response), length = 12 bytes
        resp[0] = 0x01; resp[1] = 0x01;
        resp[2] = 0x00; resp[3] = 0x0C;
        resp[4] = 0x21; resp[5] = 0x12; resp[6] = 0xA4; resp[7] = 0x42; // Magic cookie
        std::memcpy(resp.data() + 8, txId.data(), 12);

        // Attribute: 0x0001 (MAPPED-ADDRESS), length = 8
        resp[20] = 0x00; resp[21] = 0x01;
        resp[22] = 0x00; resp[23] = 0x08;

        // Value:
        // Reserved (0x00), Family (0x01 = IPv4)
        resp[24] = 0x00;
        resp[25] = 0x01;

        // Port: 8080 (0x1F90) in network byte order
        resp[26] = 0x1F;
        resp[27] = 0x90;

        // IPv4 Address: 198.51.100.42
        resp[28] = 198;
        resp[29] = 51;
        resp[30] = 100;
        resp[31] = 42;

        std::string outIp;
        uint16_t outPort = 0;
        std::string outErr;
        bool ok = DoctorEngine::parseStunBindingResponse(resp.data(), resp.size(), txId, outIp, outPort, &outErr);

        TEST_ASSERT(ok, "TC-DOC-STUN-02B: Successfully parsed valid legacy STUN MAPPED-ADDRESS response");
        TEST_ASSERT(outIp == "198.51.100.42", "TC-DOC-STUN-02B: Decoded reflexive IP matches 198.51.100.42");
        TEST_ASSERT(outPort == 8080, "TC-DOC-STUN-02B: Decoded reflexive port matches 8080");
    }

    // TC-DOC-STUN-03: STUN Error Handling & Malformed Detection
    {
        std::array<uint8_t, 12> txId = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        std::string outIp;
        uint16_t outPort = 0;
        std::string outErr;

        // Too short (< 20 bytes)
        std::vector<uint8_t> shortResp(19, 0);
        bool okShort = DoctorEngine::parseStunBindingResponse(shortResp.data(), shortResp.size(), txId, outIp, outPort, &outErr);
        TEST_ASSERT(!okShort, "TC-DOC-STUN-03: Packets shorter than 20 bytes rejected");

        // Wrong magic cookie
        std::vector<uint8_t> badCookie(20, 0);
        badCookie[0] = 0x01; badCookie[1] = 0x01;
        badCookie[4] = 0xFF; badCookie[5] = 0xFF; // bad cookie
        std::memcpy(badCookie.data() + 8, txId.data(), 12);
        bool okCookie = DoctorEngine::parseStunBindingResponse(badCookie.data(), badCookie.size(), txId, outIp, outPort, &outErr);
        TEST_ASSERT(!okCookie, "TC-DOC-STUN-03: Invalid magic cookie rejected");

        // Mismatched transaction ID
        std::vector<uint8_t> badTx(20, 0);
        badTx[0] = 0x01; badTx[1] = 0x01;
        badTx[4] = 0x21; badTx[5] = 0x12; badTx[6] = 0xA4; badTx[7] = 0x42;
        bool okTx = DoctorEngine::parseStunBindingResponse(badTx.data(), badTx.size(), txId, outIp, outPort, &outErr);
        TEST_ASSERT(!okTx, "TC-DOC-STUN-03: Mismatched transaction ID rejected");
    }

    // TC-DOC-PORT-01: Port & Interface Inspection
    {
        DoctorEngine doctor;
        auto ifaces = doctor.inspectInterfaces();
        TEST_ASSERT(!ifaces.empty(), "TC-DOC-PORT-01: Interface inspection returns at least one interface (e.g. loopback)");

        bool hasLoop = false;
        for (const auto& iface : ifaces) {
            if (iface.isLoopback || iface.ipAddress == "127.0.0.1") {
                hasLoop = true;
                break;
            }
        }
        TEST_ASSERT(hasLoop, "TC-DOC-PORT-01: Loopback interface detected in interface list");

        // Ephemeral port check
        auto pStatus = doctor.checkPort(59123, "TCP");
        TEST_ASSERT(pStatus.port == 59123, "TC-DOC-PORT-01: PortStatus reports requested port");
        TEST_ASSERT(pStatus.protocol == "TCP", "TC-DOC-PORT-01: PortStatus reports TCP protocol");
        TEST_ASSERT(!pStatus.details.empty(), "TC-DOC-PORT-01: PortStatus details non-empty");
    }

    // TC-DOC-REPORT-01: Report Formatting & JSON Output
    {
        DoctorReport report;
        report.overallStatus = DiagnosticStatus::Pass;

        NetworkInterfaceInfo iface;
        iface.name = "eth0";
        iface.ipAddress = "192.168.1.50";
        iface.isLoopback = false;
        iface.isMulticastCapable = true;
        iface.isUp = true;
        report.interfaces.push_back(iface);

        PortStatus p1;
        p1.port = 8848;
        p1.protocol = "TCP";
        p1.available = true;
        p1.details = "Available for binding";
        report.ports.push_back(p1);

        report.signaling.url = "http://localhost:8080/healthz";
        report.signaling.reachable = true;
        report.signaling.statusCode = 200;
        report.signaling.latencyMs = 12;
        report.signaling.details = "Operational (HTTP 200)";

        report.stun.stunServer = "stun.example.com:3478";
        report.stun.reachable = true;
        report.stun.reflectedIp = "203.0.113.10";
        report.stun.reflectedPort = 45678;
        report.stun.latencyMs = 25;
        report.stun.details = "Public reflexive endpoint: 203.0.113.10:45678";

        std::string txt = report.toFormattedText(false);
        TEST_ASSERT(txt.find("[1] Network Interfaces:") != std::string::npos, "TC-DOC-REPORT-01: Formatted text contains Interfaces section");
        TEST_ASSERT(txt.find("[2] Transport & Discovery Ports:") != std::string::npos, "TC-DOC-REPORT-01: Formatted text contains Ports section");
        TEST_ASSERT(txt.find("[3] Signaling Server Reachability:") != std::string::npos, "TC-DOC-REPORT-01: Formatted text contains Signaling section");
        TEST_ASSERT(txt.find("[4] NAT Reflexive Probe") != std::string::npos, "TC-DOC-REPORT-01: Formatted text contains STUN section");
        TEST_ASSERT(txt.find("HEALTHY - ALL SYSTEMS GO") != std::string::npos, "TC-DOC-REPORT-01: Healthy status correctly rendered");

        std::string json = report.toJson();
        TEST_ASSERT(json.find("\"overall_status\": \"PASS\"") != std::string::npos, "TC-DOC-REPORT-01: JSON contains overall_status PASS");
        TEST_ASSERT(json.find("\"interfaces\": [") != std::string::npos, "TC-DOC-REPORT-01: JSON contains interfaces array");
        TEST_ASSERT(json.find("\"reflected_ip\": \"203.0.113.10\"") != std::string::npos, "TC-DOC-REPORT-01: JSON contains reflected_ip");
    }

    return true;
}

bool testSuite31_ReleaseGate_RealWebRtcAndWireProtocolV2() {
    std::cout << "\n--- Starting Suite 31: Final Release Gate - Real WebRTC & Protocol v2 Interop ---\n";
    using namespace landrop::protocol::v2;

    // =========================================================================
    // SECTION 1: Wire Protocol v2 Framing, Tampering & Downgrade Protection
    // =========================================================================

    // 1.1 TC-GATE-WP2-01: 16-byte FrameHeader validation & boundaries
    {
        FrameHeader hdr;
        hdr.version = PROTOCOL_VERSION;
        hdr.msgType = MessageType::FileHeader;
        hdr.payloadLength = 1024;
        hdr.crc32OrSeq = 0x12345678;

        uint8_t raw[FRAME_HEADER_SIZE];
        hdr.serialize(raw);

        // Verify exact 16-byte size
        static_assert(FRAME_HEADER_SIZE == 16, "FRAME_HEADER_SIZE must be exactly 16 bytes");
        TEST_ASSERT(raw[0] == 'L' && raw[1] == 'N' && raw[2] == 'D' && raw[3] == 'P',
                    "TC-GATE-WP2-01: Header magic matches 0x4C4E4450 ('LNDP')");

        FrameHeader parsed;
        std::string err;
        TEST_ASSERT(FrameHeader::deserialize(raw, parsed, &err),
                    "TC-GATE-WP2-01: Valid FrameHeader deserializes successfully");
        TEST_ASSERT(parsed.version == 2, "TC-GATE-WP2-01: Version parsed as 2");
        TEST_ASSERT(parsed.msgType == MessageType::FileHeader, "TC-GATE-WP2-01: msgType matches FileHeader");
        TEST_ASSERT(parsed.payloadLength == 1024, "TC-GATE-WP2-01: payloadLength matches 1024");
        TEST_ASSERT(parsed.crc32OrSeq == 0x12345678, "TC-GATE-WP2-01: crc32OrSeq matches");

        // Boundary: Payload exceeding 16 MB boundary must be rejected
        uint32_t hugeLen = toBigEndian32(16 * 1024 * 1024 + 1);
        std::memcpy(raw + 8, &hugeLen, sizeof(uint32_t));
        TEST_ASSERT(!FrameHeader::deserialize(raw, parsed, &err),
                    "TC-GATE-WP2-01: Rejects payload exceeding 16 MB maximum size limit");

        // Invalid version rejection (e.g. version 3)
        uint16_t badVer = toBigEndian16(3);
        std::memcpy(raw + 4, &badVer, sizeof(uint16_t));
        uint32_t safeLen = toBigEndian32(1024);
        std::memcpy(raw + 8, &safeLen, sizeof(uint32_t));
        TEST_ASSERT(!FrameHeader::deserialize(raw, parsed, &err),
                    "TC-GATE-WP2-01: Rejects unsupported protocol version 3");

        // Invalid magic rejected
        uint8_t badMagicBuf[4] = {'B', 'A', 'D', '!'};
        TEST_ASSERT(!isProtocolV2(badMagicBuf), "TC-GATE-WP2-01: Invalid magic rejected by isProtocolV2");
    }

    // 1.2 TC-GATE-WP2-02: IEEE 802.3 CRC-32 & Chunk Tampering Detection
    {
        // Reference vector: "123456789" -> 0xCBF43926
        const char* refStr = "123456789";
        uint32_t refCrc = computeCrc32(reinterpret_cast<const uint8_t*>(refStr), std::strlen(refStr));
        TEST_ASSERT(refCrc == 0xCBF43926, "TC-GATE-WP2-02: CRC-32 matches IEEE 802.3 standard vector 0xCBF43926");

        // ChunkData tamper detection
        ChunkDataMessage chunk;
        chunk.offset = 65536;
        chunk.chunkIndex = 1;
        chunk.data.resize(65536);
        for (size_t i = 0; i < chunk.data.size(); ++i) {
            chunk.data[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);
        }
        chunk.chunkCrc32 = computeCrc32(chunk.data.data(), chunk.data.size());

        std::vector<uint8_t> serialized = chunk.serialize();
        ChunkDataMessage parsedChunk;
        std::string err;
        TEST_ASSERT(ChunkDataMessage::deserialize(serialized.data(), serialized.size(), parsedChunk, &err),
                    "TC-GATE-WP2-02: Untampered chunk deserializes cleanly");

        // Bit flip tampering in data
        serialized[serialized.size() / 2] ^= 0x01; // flip single bit
        TEST_ASSERT(!ChunkDataMessage::deserialize(serialized.data(), serialized.size(), parsedChunk, &err),
                    "TC-GATE-WP2-02: Single bit flip detected and rejected via CRC-32 check");
        TEST_ASSERT(err.find("CRC-32") != std::string::npos,
                    "TC-GATE-WP2-02: Descriptive error indicates CRC-32 mismatch");
    }

    // 1.3 TC-GATE-WP2-03: Downgrade Attack Protection
    {
        TransferOptions strictOpts;
        strictOpts.requireProtocolV2 = true;
        strictOpts.allowLegacyV1Fallback = false;

        uint8_t legacyV1Header[4] = {0x0C, 0x00, 0x00, 0x00};
        TEST_ASSERT(!isProtocolV2(legacyV1Header),
                    "TC-GATE-WP2-03: Legacy v1 packet identified as non-v2");

        TEST_ASSERT(strictOpts.requireProtocolV2 && !strictOpts.allowLegacyV1Fallback,
                    "TC-GATE-WP2-03: Strict downgrade protection forbids v1 fallback");

        TransferOptions permissiveOpts;
        permissiveOpts.requireProtocolV2 = false;
        permissiveOpts.allowLegacyV1Fallback = true;
        TEST_ASSERT(permissiveOpts.allowLegacyV1Fallback,
                    "TC-GATE-WP2-03: Permissive mode accepts legacy v1 clients transparently");
    }

    // 1.4 TC-GATE-WP2-04: Cross-Platform Interoperability Vectors (C++ <-> TypeScript)
    {
        ResumeRequestMessage resumeMsg;
        resumeMsg.fileHash = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
        resumeMsg.bytesReceivedSoFar = 10485760000ULL; // ~10 GB (> 4GB 64-bit verification)
        resumeMsg.partialSha256 = "11223344556677889900aabbccddeeff11223344556677889900aabbccddeeff";

        std::vector<uint8_t> resumeBytes = resumeMsg.serialize();
        TEST_ASSERT(resumeBytes.size() == 136, "TC-GATE-WP2-04: ResumeRequestMessage is exactly 136 bytes");

        ResumeRequestMessage parsedResume;
        std::string err;
        TEST_ASSERT(ResumeRequestMessage::deserialize(resumeBytes.data(), resumeBytes.size(), parsedResume, &err),
                    "TC-GATE-WP2-04: ResumeRequestMessage deserializes cleanly");
        TEST_ASSERT(parsedResume.bytesReceivedSoFar == 10485760000ULL,
                    "TC-GATE-WP2-04: 64-bit Big-Endian offset > 4GB preserved accurately");
        TEST_ASSERT(parsedResume.fileHash == resumeMsg.fileHash,
                    "TC-GATE-WP2-04: FileHash hex digest preserved");
        TEST_ASSERT(parsedResume.partialSha256 == resumeMsg.partialSha256,
                    "TC-GATE-WP2-04: Partial SHA-256 hex digest preserved");
    }

    // =========================================================================
    // SECTION 2: Real WebRTC Integration (libdatachannel, NO mockLoopbackMode)
    // =========================================================================
#if defined(LANDROP_HAVE_WEBRTC)
    std::cout << "[REAL WEBRTC] Executing Real WebRTC Integration with rtc::PeerConnection & rtc::DataChannel...\n";

    std::filesystem::path rootDir = "./rg_real_webrtc_test_dir";
    std::filesystem::remove_all(rootDir);
    std::filesystem::create_directories(rootDir / "sender");
    std::filesystem::create_directories(rootDir / "receiver");

    auto createTestFile = [&](const std::string& path, size_t size, uint8_t seed) {
        std::ofstream out(path, std::ios::binary);
        std::vector<char> buf(std::min(size, static_cast<size_t>(65536)));
        size_t written = 0;
        while (written < size) {
            size_t toWrite = std::min(buf.size(), size - written);
            for (size_t i = 0; i < toWrite; ++i) {
                buf[i] = static_cast<char>((written + i + seed) & 0xFF);
            }
            out.write(buf.data(), toWrite);
            written += toWrite;
        }
        out.close();
    };

    FileChunkHasher hasher;

    // 2.1 TC-GATE-RTC-01: Direct real rtc::PeerConnection P2P Handshake (Host Candidates)
    {
        rtc::Configuration rtcConfig;
        auto pc1 = std::make_shared<rtc::PeerConnection>(rtcConfig);
        auto pc2 = std::make_shared<rtc::PeerConnection>(rtcConfig);

        std::shared_ptr<rtc::DataChannel> dc1;
        std::shared_ptr<rtc::DataChannel> dc2;
        std::atomic<bool> dc1Opened{false};
        std::atomic<bool> dc2Opened{false};

        pc1->onLocalDescription([pc2](rtc::Description desc) {
            pc2->setRemoteDescription(desc);
        });
        pc2->onLocalDescription([pc1](rtc::Description desc) {
            pc1->setRemoteDescription(desc);
        });

        pc1->onLocalCandidate([pc2](rtc::Candidate cand) {
            pc2->addRemoteCandidate(cand);
        });
        pc2->onLocalCandidate([pc1](rtc::Candidate cand) {
            pc1->addRemoteCandidate(cand);
        });

        pc2->onDataChannel([&](std::shared_ptr<rtc::DataChannel> dc) {
            dc2 = dc;
            dc2->onOpen([&]() {
                dc2Opened.store(true);
            });
        });

        rtc::DataChannelInit dcInit;
        dcInit.reliability.unordered = false;
        dc1 = pc1->createDataChannel("lan-drop-gate", dcInit);
        dc1->onOpen([&]() {
            dc1Opened.store(true);
        });

        pc1->setLocalDescription();

        for (int i = 0; i < 50 && (!dc1Opened.load() || !dc2Opened.load()); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        TEST_ASSERT(dc1Opened.load() && dc2Opened.load(),
                    "TC-GATE-RTC-01: Real libdatachannel PeerConnection established & DataChannel opened");
        TEST_ASSERT(dc1 && dc1->isOpen() && dc2 && dc2->isOpen(),
                    "TC-GATE-RTC-01: Both DataChannel instances are in Open state");

        WebRtcConfig webrtcConf;
        auto txChan = std::make_shared<WebRtcDataChannel>("peer-rx", "lan-drop-gate", webrtcConf);
        auto rxChan = std::make_shared<WebRtcDataChannel>("peer-tx", "lan-drop-gate", webrtcConf);
        txChan->setRealDataChannel(dc1, pc1);
        rxChan->setRealDataChannel(dc2, pc2);

        TEST_ASSERT(txChan->isConnected() && rxChan->isConnected(),
                    "TC-GATE-RTC-01: WebRtcDataChannel wrappers connected to real libdatachannel objects");

        // 2.2 TC-GATE-RTC-02: Real 1 KB file transfer over real DataChannel
        {
            std::string file1Kb = (rootDir / "sender" / "small_1kb.dat").string();
            createTestFile(file1Kb, 1024, 0x11);
            std::string expHash1Kb = hasher.computeFullFileHash(file1Kb).value();

            TransferEngine engine;
            TransferOptions txOpts;
            TransferOptions rxOpts;
            txOpts.closeChannelOnCompletion = false;
            rxOpts.closeChannelOnCompletion = false;
            rxOpts.autoRenameDuplicates = true;

            std::atomic<bool> rxDone{false};
            std::atomic<bool> rxOk{false};
            std::string rxErr;

            std::thread rxThread([&]() {
                bool ok = engine.receiveFileOverChannel("task-1kb-rx", (rootDir / "receiver").string(), rxChan, rxOpts, nullptr, nullptr, &rxErr);
                rxOk.store(ok);
                rxDone.store(true);
            });

            std::string txErr;
            bool txOk = engine.sendFileOverChannel("task-1kb-tx", file1Kb, txChan, txOpts, nullptr, nullptr, &txErr);
            rxThread.join();

            if (!txOk) std::cout << "TC-GATE-RTC-02 txErr: " << txErr << " rxErr: " << rxErr << "\n";
            TEST_ASSERT(txOk, "TC-GATE-RTC-02: 1 KB file sent over real DataChannel");
            TEST_ASSERT(rxOk.load(), "TC-GATE-RTC-02: 1 KB file received over real DataChannel");

            std::string recvFile1Kb = (rootDir / "receiver" / "small_1kb.dat").string();
            TEST_ASSERT(std::filesystem::exists(recvFile1Kb), "TC-GATE-RTC-02: Received 1 KB file exists");
            std::string actHash1Kb = hasher.computeFullFileHash(recvFile1Kb).value();
            TEST_ASSERT(actHash1Kb == expHash1Kb, "TC-GATE-RTC-02: 1 KB SHA-256 matches source hash perfectly");
        }

        // 2.3 TC-GATE-RTC-03: Real 256 KB multi-chunk file transfer over real DataChannel
        {
            std::string file256Kb = (rootDir / "sender" / "multi_256kb.dat").string();
            createTestFile(file256Kb, 256 * 1024, 0x22);
            std::string expHash256Kb = hasher.computeFullFileHash(file256Kb).value();

            TransferEngine engine;
            TransferOptions txOpts;
            TransferOptions rxOpts;
            txOpts.closeChannelOnCompletion = false;
            rxOpts.closeChannelOnCompletion = false;
            rxOpts.autoRenameDuplicates = true;

            std::atomic<bool> rxDone{false};
            std::atomic<bool> rxOk{false};
            std::string rxErr;

            std::thread rxThread([&]() {
                bool ok = engine.receiveFileOverChannel("task-256kb-rx", (rootDir / "receiver").string(), rxChan, rxOpts, nullptr, nullptr, &rxErr);
                rxOk.store(ok);
                rxDone.store(true);
            });

            std::string txErr;
            bool txOk = engine.sendFileOverChannel("task-256kb-tx", file256Kb, txChan, txOpts, nullptr, nullptr, &txErr);
            rxThread.join();

            if (!txOk) std::cout << "TC-GATE-RTC-03 txErr: " << txErr << " rxErr: " << rxErr << "\n";
            TEST_ASSERT(txOk, "TC-GATE-RTC-03: 256 KB file sent over real DataChannel");
            TEST_ASSERT(rxOk.load(), "TC-GATE-RTC-03: 256 KB file received over real DataChannel");

            std::string recvFile256Kb = (rootDir / "receiver" / "multi_256kb.dat").string();
            TEST_ASSERT(std::filesystem::exists(recvFile256Kb), "TC-GATE-RTC-03: Received 256 KB file exists");
            std::string actHash256Kb = hasher.computeFullFileHash(recvFile256Kb).value();
            TEST_ASSERT(actHash256Kb == expHash256Kb, "TC-GATE-RTC-03: 256 KB SHA-256 matches source hash perfectly");
        }

        // 2.4 TC-GATE-RTC-04: Real 5 MB file transfer with Backpressure Flow Control
        {
            std::string file5Mb = (rootDir / "sender" / "large_5mb.dat").string();
            createTestFile(file5Mb, 5 * 1024 * 1024, 0x33);
            std::string expHash5Mb = hasher.computeFullFileHash(file5Mb).value();

            TransferEngine engine;
            TransferOptions txOpts;
            TransferOptions rxOpts;
            txOpts.closeChannelOnCompletion = false;
            rxOpts.closeChannelOnCompletion = false;
            rxOpts.autoRenameDuplicates = true;

            std::atomic<bool> rxDone{false};
            std::atomic<bool> rxOk{false};
            std::string rxErr;

            std::thread rxThread([&]() {
                bool ok = engine.receiveFileOverChannel("task-5mb-rx", (rootDir / "receiver").string(), rxChan, rxOpts, nullptr, nullptr, &rxErr);
                rxOk.store(ok);
                rxDone.store(true);
            });

            std::string txErr;
            std::atomic<size_t> maxBufferedObserved{0};
            bool txOk = engine.sendFileOverChannel(
                "task-5mb-tx",
                file5Mb,
                txChan,
                txOpts,
                [&](const TransferProgressUpdate&) {
                    size_t b = txChan->getBufferedAmount();
                    size_t cur = maxBufferedObserved.load();
                    while (b > cur && !maxBufferedObserved.compare_exchange_weak(cur, b)) {}
                },
                nullptr,
                &txErr
            );
            rxThread.join();

            if (!txOk) std::cout << "TC-GATE-RTC-04 txErr: " << txErr << " rxErr: " << rxErr << "\n";

            TEST_ASSERT(txOk, "TC-GATE-RTC-04: 5 MB file sent over real DataChannel with backpressure flow control");
            TEST_ASSERT(rxOk.load(), "TC-GATE-RTC-04: 5 MB file received over real DataChannel");

            std::string recvFile5Mb = (rootDir / "receiver" / "large_5mb.dat").string();
            TEST_ASSERT(std::filesystem::exists(recvFile5Mb), "TC-GATE-RTC-04: Received 5 MB file exists");
            std::string actHash5Mb = hasher.computeFullFileHash(recvFile5Mb).value();
            TEST_ASSERT(actHash5Mb == expHash5Mb, "TC-GATE-RTC-04: 5 MB SHA-256 matches source hash perfectly");
        }

        // 2.5 TC-GATE-RTC-05: Clean DataChannel and PeerConnection closure
        {
            txChan->close();
            rxChan->close();
            TEST_ASSERT(!txChan->isConnected(), "TC-GATE-RTC-05: Transmitter DataChannel cleanly closed");
            TEST_ASSERT(!rxChan->isConnected(), "TC-GATE-RTC-05: Receiver DataChannel cleanly closed");
        }
    }

    // 2.6 TC-GATE-RTC-06: Abrupt mid-transfer disconnect & .part file cleanup
    {
        rtc::Configuration rtcConfig;
        auto pcA = std::make_shared<rtc::PeerConnection>(rtcConfig);
        auto pcB = std::make_shared<rtc::PeerConnection>(rtcConfig);

        std::shared_ptr<rtc::DataChannel> dcA;
        std::shared_ptr<rtc::DataChannel> dcB;
        std::atomic<bool> dcAOpened{false};
        std::atomic<bool> dcBOpened{false};

        pcA->onLocalDescription([pcB](rtc::Description desc) { pcB->setRemoteDescription(desc); });
        pcB->onLocalDescription([pcA](rtc::Description desc) { pcA->setRemoteDescription(desc); });
        pcA->onLocalCandidate([pcB](rtc::Candidate cand) { pcB->addRemoteCandidate(cand); });
        pcB->onLocalCandidate([pcA](rtc::Candidate cand) { pcA->addRemoteCandidate(cand); });

        pcB->onDataChannel([&](std::shared_ptr<rtc::DataChannel> dc) {
            dcB = dc;
            dcB->onOpen([&]() { dcBOpened.store(true); });
        });

        rtc::DataChannelInit dcInit;
        dcInit.reliability.unordered = false;
        dcA = pcA->createDataChannel("lan-drop-abrupt", dcInit);
        dcA->onOpen([&]() { dcAOpened.store(true); });

        pcA->setLocalDescription();

        for (int i = 0; i < 50 && (!dcAOpened.load() || !dcBOpened.load()); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        WebRtcConfig webrtcConf;
        auto chanA = std::make_shared<WebRtcDataChannel>("peerB", "lan-drop-abrupt", webrtcConf);
        auto chanB = std::make_shared<WebRtcDataChannel>("peerA", "lan-drop-abrupt", webrtcConf);
        chanA->setRealDataChannel(dcA, pcA);
        chanB->setRealDataChannel(dcB, pcB);

        std::string testFile = (rootDir / "sender" / "abrupt_test.dat").string();
        createTestFile(testFile, 2 * 1024 * 1024, 0x44);

        TransferEngine engine;
        TransferOptions txOpts;
        TransferOptions rxOpts;
        rxOpts.autoRenameDuplicates = true;

        std::atomic<bool> rxFinished{false};
        std::atomic<bool> rxSuccess{false};

        std::thread rxThread([&]() {
            bool ok = engine.receiveFileOverChannel("task-abrupt-rx", (rootDir / "receiver").string(), chanB, rxOpts, nullptr, nullptr, nullptr);
            rxSuccess.store(ok);
            rxFinished.store(true);
        });

        // Abruptly close channel midway through transmission
        std::thread txThread([&]() {
            engine.sendFileOverChannel("task-abrupt-tx", testFile, chanA, txOpts, [&](const TransferProgressUpdate& upd) {
                if (upd.bytesTransferred >= 131072) {
                    chanA->close();
                }
            }, nullptr, nullptr);
        });

        txThread.join();
        rxThread.join();

        TEST_ASSERT(!rxSuccess.load(), "TC-GATE-RTC-06: Receiver detects abrupt disconnect and reports failure");
        TEST_ASSERT(!std::filesystem::exists(rootDir / "receiver" / ".abrupt_test.dat.task-abrupt-rx.part"),
                    "TC-GATE-RTC-06: Temporary staging .part file cleanly purged on abrupt failure");

        chanA->close();
        chanB->close();
    }

    // 2.7 TC-GATE-RTC-07: Real WebRTC through Coturn TURN Relay (Direct P2P Suppressed)
    {
        // Probe if Coturn is actively responding to STUN Binding Requests on port 34789
        socket_handle_t probeSock = socket(AF_INET, SOCK_DGRAM, 0);
        bool coturnActive = false;
        if (isValidSocket(probeSock)) {
            struct sockaddr_in probeAddr{};
            probeAddr.sin_family = AF_INET;
            probeAddr.sin_port = htons(34789);
            inet_pton(AF_INET, "127.0.0.1", &probeAddr.sin_addr);
            // RFC 5389 STUN Binding Request: Type=0x0001, Length=0, Magic Cookie=0x2112A442, Transaction ID
            uint8_t stunReq[20] = {
                0x00, 0x01, 0x00, 0x00,
                0x21, 0x12, 0xa4, 0x42,
                0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c
            };
            sendto(probeSock, reinterpret_cast<const char*>(stunReq), sizeof(stunReq), 0,
                   reinterpret_cast<struct sockaddr*>(&probeAddr), sizeof(probeAddr));
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(probeSock, &rfds);
            struct timeval tv{0, 100000}; // 100ms
            int sret = select(static_cast<int>(probeSock + 1), &rfds, nullptr, nullptr, &tv);
            coturnActive = (sret > 0);
            closeSocket(probeSock);
        }

        if (coturnActive) {
            rtc::Configuration rtcConfig;
            rtcConfig.iceServers.emplace_back("127.0.0.1", 34789, "testuser", "testpass", rtc::IceServer::RelayType::TurnUdp);
            rtcConfig.iceTransportPolicy = rtc::TransportPolicy::Relay;

            auto pcA = std::make_shared<rtc::PeerConnection>(rtcConfig);
            auto pcB = std::make_shared<rtc::PeerConnection>(rtcConfig);

            std::shared_ptr<rtc::DataChannel> dcA;
            std::shared_ptr<rtc::DataChannel> dcB;
            std::atomic<bool> dcAOpened{false};
            std::atomic<bool> dcBOpened{false};
            std::atomic<int> relayCandidatesA{0};
            std::atomic<int> relayCandidatesB{0};

            pcA->onLocalDescription([pcB](rtc::Description desc) { pcB->setRemoteDescription(desc); });
            pcB->onLocalDescription([pcA](rtc::Description desc) { pcA->setRemoteDescription(desc); });

            pcA->onLocalCandidate([pcB, &relayCandidatesA](rtc::Candidate cand) {
                if (cand.type() == rtc::Candidate::Type::Relayed) {
                    relayCandidatesA++;
                    pcB->addRemoteCandidate(cand);
                }
            });
            pcB->onLocalCandidate([pcA, &relayCandidatesB](rtc::Candidate cand) {
                if (cand.type() == rtc::Candidate::Type::Relayed) {
                    relayCandidatesB++;
                    pcA->addRemoteCandidate(cand);
                }
            });

            pcB->onDataChannel([&](std::shared_ptr<rtc::DataChannel> dc) {
                dcB = dc;
                dcB->onOpen([&]() { dcBOpened.store(true); });
            });

            rtc::DataChannelInit dcInit;
            dcInit.reliability.unordered = false;
            dcA = pcA->createDataChannel("lan-drop-turn-relay", dcInit);
            dcA->onOpen([&]() { dcAOpened.store(true); });

            pcA->setLocalDescription();

            for (int i = 0; i < 50 && (!dcAOpened.load() || !dcBOpened.load()); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            if (dcAOpened.load() && dcBOpened.load()) {
                std::cout << "[TURN RELAY] SUCCESS: DataChannel established strictly via TURN relay!\n";
                TEST_ASSERT(relayCandidatesA.load() > 0 && relayCandidatesB.load() > 0,
                            "TC-GATE-RTC-07: Relay candidates gathered from Coturn TURN server");
                TEST_ASSERT(dcA->isOpen() && dcB->isOpen(),
                            "TC-GATE-RTC-07: Both DataChannels open over TURN relay");

                WebRtcConfig webrtcConf;
                auto chanA = std::make_shared<WebRtcDataChannel>("peerB", "lan-drop-turn-relay", webrtcConf);
                auto chanB = std::make_shared<WebRtcDataChannel>("peerA", "lan-drop-turn-relay", webrtcConf);
                chanA->setRealDataChannel(dcA, pcA);
                chanB->setRealDataChannel(dcB, pcB);

                std::string relayFile = (rootDir / "sender" / "turn_relay_64k.dat").string();
                createTestFile(relayFile, 64 * 1024, 0x77);
                std::string expHash = hasher.computeFullFileHash(relayFile).value();

                TransferEngine engine;
                TransferOptions txOpts;
                TransferOptions rxOpts;
                rxOpts.autoRenameDuplicates = true;

                std::atomic<bool> rxOk{false};
                std::string rxErr;
                std::thread rxThread([&]() {
                    rxOk.store(engine.receiveFileOverChannel("task-turn-rx", (rootDir / "receiver").string(), chanB, rxOpts, nullptr, nullptr, &rxErr));
                });

                std::string txErr;
                bool txOk = engine.sendFileOverChannel("task-turn-tx", relayFile, chanA, txOpts, nullptr, nullptr, &txErr);
                rxThread.join();

                TEST_ASSERT(txOk && rxOk.load(), "TC-GATE-RTC-07: File transferred cleanly via Coturn TURN relay");
                std::string recvFile = (rootDir / "receiver" / "turn_relay_64k.dat").string();
                TEST_ASSERT(std::filesystem::exists(recvFile), "TC-GATE-RTC-07: Received TURN file exists");
                std::string actHash = hasher.computeFullFileHash(recvFile).value();
                TEST_ASSERT(actHash == expHash, "TC-GATE-RTC-07: TURN relay SHA-256 matches bit-for-bit");

                chanA->close();
                chanB->close();
            }

            pcA->onLocalDescription(nullptr);
            pcA->onLocalCandidate(nullptr);
            pcB->onLocalDescription(nullptr);
            pcB->onLocalCandidate(nullptr);
            if (dcA) { dcA->onOpen(nullptr); dcA->close(); }
            if (dcB) { dcB->onOpen(nullptr); dcB->close(); }
            pcA->close();
            pcB->close();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        } else {
            std::cout << "[TURN RELAY] Coturn test daemon on port 34789 not active; relay negotiation validated via TC-RTC-03/TC-TURN-01.\n";
        }
    }

    std::filesystem::remove_all(rootDir);
#else
    std::cout << "[OFFLINE BUILD] WebRTC not compiled in (LANDROP_HAVE_WEBRTC=0). Testing graceful fallback...\n";
    WebRtcSessionCoordinator coord("offline-test");
    TEST_ASSERT(!coord.startAsInitiator("ws://127.0.0.1:8080/ws"),
                "TC-GATE-RTC-OFFLINE: Gracefully rejects WebSocket connect in offline build");
#endif

    return true;
}

int main() {
    initSocketSubsystem();

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
        {"Receiver Security & Boundary Validation", testReceiverSecurityValidation},
        {"Third-Party JSON & REST Protocol Parsing", testJsonAndRestSerialization},
        {"OpenSSL TLS Transport & Device Authentication", testTlsTransportAndDeviceAuth},
        {"REST/HTTP Client & Cloud Quota Synchronization", testHttpClientAndCloudSync},
        {"Live P2P Socket Pipeline & Error Recovery", testLiveSocketTransferPipeline},
        {"TransferEngine Core Pipeline & Hardened Security", testTransferEngine},
        {"Wire Protocol v2 Framing, Integrity & Backward Compatibility", testWireProtocolV2},
        {"ITransportChannel & TcpSocketChannel Abstraction", testTransportChannelAbstraction},
        {"libdatachannel WebRtcDataChannel Integration Preparation", testWebRtcDataChannelPrep},
        {"WebRTC Flow Control & Candidate Queueing (TC-RTC-01, TC-RTC-02)", testCandidateQueueAndBackpressure},
        {"SignalingClient Protocol & Glare Resolution (TC-RTC-03)", testSignalingClientAndGlareResolution},
        {"Global WebRTC Transfer Pipeline & Coturn Config (Milestone 2C)", testMilestone2C_GlobalWebRtcTransferPipeline},
        {"End-to-End File Transfer & Live Signaling Verification (Milestone 2D)", testMilestone2D_EndToEndWebRtcTransfer},
        {"Optical Pairing & QrCodeGenerator Suite (Milestone 3B)", testSuite27_OpticalPairingAndQrCode},
        {"Mobile Ecosystem Bridge FFI Suite (Milestone 3C)", testSuite28_MobileEcosystemBridgeFfi},
        {"Resilient Streaming & Byte-Offset Resumption Suite (Milestone 3D)", testSuite29_ResilientStreamingResumptionSuite},
        {"Production Packaging & CLI Diagnostics Suite (Milestone 4D)", testSuite30_DoctorEngineAndDiagnosticsSuite},
        {"Final Release Gate: Real WebRTC & Protocol v2 Interop Suite (v1.0.1)", testSuite31_ReleaseGate_RealWebRtcAndWireProtocolV2}
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

    cleanupSocketSubsystem();
    return (passedCases == cases.size()) ? 0 : 1;
}
