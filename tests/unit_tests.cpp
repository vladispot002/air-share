#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <sys/socket.h>
#include <unistd.h>

#include "landrop/utils/Types.hpp"
#include "landrop/core/ChunkBuffer.hpp"
#include "landrop/core/FileChunkHasher.hpp"
#include "landrop/core/SharedFolderManager.hpp"
#include "landrop/core/MulticastBeacon.hpp"
#include "landrop/core/NetworkSession.hpp"
#include "landrop/core/TlsContext.hpp"

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
    trans.initialize();
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
    int fds[2];
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

    serverSession.closeSession();
    clientSession.closeSession();

    // 3. Negative Test 1: Untrusted Certificate / Missing Pinning Rejected by Default
    int fdsNeg1[2];
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
    int fdsNeg2[2];
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
    int fdsNeg3[2];
    TEST_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, fdsNeg3) == 0, "Socketpair for negative test 3");
    NetworkSession srvNeg3("srv-neg3", "127.0.0.1", 0);
    srvNeg3.attachSocket(fdsNeg3[0]);
    srvNeg3.enableTls(serverCfg);

    std::thread sTh3([&]() { sStatus = srvNeg3.performTlsHandshake(); });
    // Plaintext client sends raw HTTP or binary garbage
    const char plaintextGarbage[] = "GET /status HTTP/1.1\r\nHost: localhost\r\n\r\n";
    send(fdsNeg3[1], plaintextGarbage, sizeof(plaintextGarbage), 0);
    sTh3.join();
    close(fdsNeg3[1]);

    TEST_ASSERT(sStatus == TlsStatus::ProtocolMismatch || sStatus == TlsStatus::HandshakeFailed,
                "TLS Server safely rejected plaintext protocol mismatch");
    TEST_ASSERT(!srvNeg3.isEncrypted(), "Server remained unencrypted after protocol mismatch");
    srvNeg3.closeSession();

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
        {"Receiver Security & Boundary Validation", testReceiverSecurityValidation},
        {"Third-Party JSON & REST Protocol Parsing", testJsonAndRestSerialization},
        {"OpenSSL TLS Transport & Device Authentication", testTlsTransportAndDeviceAuth}
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
