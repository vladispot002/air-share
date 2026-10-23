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


#include "landrop/utils/Types.hpp"
#include "landrop/core/ChunkBuffer.hpp"
#include "landrop/core/FileChunkHasher.hpp"
#include "landrop/core/SharedFolderManager.hpp"
#include "landrop/core/MulticastBeacon.hpp"
#include "landrop/core/NetworkSession.hpp"
#include "landrop/core/TlsContext.hpp"
#include "landrop/core/HttpClient.hpp"
#include "landrop/core/TransferEngine.hpp"


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
    }
    std::filesystem::remove_all(outsideDir);

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

    std::string testConfigDir = "/tmp/landrop_test_cfg_dir/nested_profile";
    std::string testConfigFile = testConfigDir + "/test_settings.json";
    std::error_code ec;
    std::filesystem::remove_all("/tmp/landrop_test_cfg_dir", ec);

    // 6. First launch test on non-existent file
    ui::SettingsPresenter firstLaunchPresenter(nullptr);
    firstLaunchPresenter.setConfigPath(testConfigFile);
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
    reloadedPresenter.setConfigPath(testConfigFile);
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
    corruptPresenter.setConfigPath(testConfigFile);
    bool corruptLoad = corruptPresenter.loadSettings();
    TEST_ASSERT(!corruptLoad, "Malformed JSON safely rejected");
    TEST_ASSERT(corruptPresenter.getPort() == 8848, "Presenter maintains safe defaults upon corrupt JSON");

    // 10. Unwritable path error handling
    ui::SettingsPresenter unwritablePresenter(nullptr);
    unwritablePresenter.setConfigPath("/dev/null/impossible/settings.json");
    bool unwriteOk = unwritablePresenter.saveSettings();
    TEST_ASSERT(!unwriteOk, "Unwritable path safely reports failure without throwing");

    std::filesystem::remove_all("/tmp/landrop_test_cfg_dir", ec);

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
        {"TransferEngine Core Pipeline & Hardened Security", testTransferEngine}
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
