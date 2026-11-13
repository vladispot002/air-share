#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <filesystem>
#include <chrono>
#include <cstring>

#include "landrop/utils/Types.hpp"
#include "landrop/utils/SocketCompat.hpp"
#include "landrop/utils/CryptoFallback.hpp"
#include "landrop/core/ChunkBuffer.hpp"
#include "landrop/core/FileChunkHasher.hpp"
#include "landrop/core/NetworkSession.hpp"
#include "landrop/core/SharedFolderManager.hpp"
#include "landrop/core/MulticastBeacon.hpp"
#include "landrop/core/TransferEngine.hpp"
#include "landrop/core/WebRtcSessionCoordinator.hpp"
#include "landrop/core/DoctorEngine.hpp"
#include "landrop/peers/NetworkPeer.hpp"
#include "landrop/peers/LocalPeer.hpp"
#include "landrop/peers/CloudPeer.hpp"
#include "landrop/tasks/TransferTask.hpp"
#include "landrop/tasks/P2PTransferTask.hpp"
#include "landrop/tasks/HttpWebTransferTask.hpp"

#if defined(LANDROP_BUILD_QT)
#include <QApplication>
#include "landrop/ui/qt/QtDesktopApp.hpp"
#endif

#if defined(LANDROP_BUILD_GTK) || !defined(LANDROP_BUILD_QT)
#include "landrop/ui/DesktopGuiEngine.hpp"
#include "landrop/ui/GtkDesktopApp.hpp"
#endif

#include "landrop/mobile/NfcPairingSession.hpp"
#include "landrop/mobile/AirDropProtocolBridge.hpp"
#include "landrop/mobile/LandropMobileBridge.h"

using namespace landrop;

static void printUsage(const char* progName) {
    std::cout << "Usage:\n";
    std::cout << "  " << progName << " gui\n";
    std::cout << "  " << progName << " mobile-gui\n";
    std::cout << "  " << progName << " send <file_path> <receiver_ip> [port] [--tls [expected_fingerprint]]\n";
    std::cout << "  " << progName << " receive [port] [save_dir] [--tls]\n";
    std::cout << "  " << progName << " p2p-send <file_path> [--signaling <ws_url>]\n";
    std::cout << "  " << progName << " p2p-receive <room_code> [save_dir] [--signaling <ws_url>]\n";
    std::cout << "  " << progName << " doctor [--json] [--stun <host:port>] [--signaling <http_url>]\n";
    std::cout << "  " << progName << " demo\n";
    std::cout << "  " << progName << " mobile-test\n";
}

static int runSender(const std::string& filePath, const std::string& receiverIp, uint16_t port, bool useTls = false, const std::string& expectedFingerprint = "") {
    std::cout << "[Sender] Starting file transfer: " << filePath << " -> " << receiverIp << ":" << port << "\n";
    if (useTls) {
        std::cout << "[Sender] TLS enabled. Pinned fingerprint: "
                  << (expectedFingerprint.empty() ? "(none - self-signed will be rejected)" : expectedFingerprint) << "\n";
    }

    TransferEngine engine;
    TransferOptions opts;
    opts.useTls = useTls;
    opts.expectedFingerprint = expectedFingerprint;

    std::string error;
    bool ok = engine.sendFile("cli-tx-01", filePath, receiverIp, port, opts, [](const TransferProgressUpdate& upd) {
        if (upd.state == TransferState::Connecting) {
            std::cout << "[Sender] " << upd.message << "\n" << std::flush;
        } else if (upd.state == TransferState::Transferring) {
            std::cout << "\r[Sender] Progress: " << upd.bytesTransferred << "/" << upd.totalBytes
                      << " bytes (" << static_cast<int>(upd.progressPercentage) << "%)" << std::flush;
        }
    }, nullptr, &error);

    std::cout << "\n";
    if (!ok) {
        std::cerr << "Error: " << error << "\n";
        return 1;
    }
    std::cout << "[Sender] Transfer verified and confirmed by receiver.\n";
    return 0;
}

static int runReceiver(uint16_t port, const std::string& saveDir, bool useTls = false) {
    std::cout << "[Receiver] Listening on port " << port << "...\n" << std::flush;
    std::cout << "[Receiver] Save directory: " << saveDir << "\n" << std::flush;
    if (useTls) {
        std::cout << "[Receiver] TLS enabled.\n" << std::flush;
    }

    TransferEngine engine;
    TransferOptions opts;
    opts.useTls = useTls;
    opts.autoRenameDuplicates = true;

    std::string error;
    bool ok = engine.receiveFileOnce("cli-rx-01", port, saveDir, opts, [](const TransferProgressUpdate& upd) {
        if (upd.state == TransferState::Connecting) {
            std::cout << "[Receiver] " << upd.message << "\n" << std::flush;
        } else if (upd.state == TransferState::Transferring) {
            std::cout << "\r[Receiver] Progress: " << upd.bytesTransferred << "/" << upd.totalBytes
                      << " bytes (" << static_cast<int>(upd.progressPercentage) << "%)" << std::flush;
        }
    }, nullptr, &error);

    std::cout << "\n";
    if (!ok) {
        std::cerr << "Error: " << error << "\n";
        return 1;
    }
    std::cout << "[Receiver] File verified successfully.\n";
    return 0;
}

[[maybe_unused]] static std::string deriveHttpUrlFromWs(const std::string& wsUrl) {
    std::string httpUrl = wsUrl;
    if (httpUrl.rfind("wss://", 0) == 0) {
        httpUrl = "https://" + httpUrl.substr(6);
    } else if (httpUrl.rfind("ws://", 0) == 0) {
        httpUrl = "http://" + httpUrl.substr(5);
    }
    size_t pathPos = httpUrl.find('/', 8);
    if (pathPos != std::string::npos) {
        httpUrl = httpUrl.substr(0, pathPos);
    }
    return httpUrl;
}

[[maybe_unused]] static int runP2pSender(const std::string& filePath, const std::string& signalingWsUrl) {
#if !defined(LANDROP_HAVE_WEBRTC)
    (void)filePath;
    (void)signalingWsUrl;
    std::cerr << "Error: WebRTC P2P transport is not enabled in this build.\n"
              << "Rebuild LAN-Drop with -DLANDROP_ENABLE_WEBRTC=ON to enable global P2P file transfers.\n";
    return 1;
#else
    std::error_code ec;
    std::filesystem::path p(filePath);
    if (!std::filesystem::is_regular_file(p, ec)) {
        std::cerr << "Error: File not found or not a regular file: " << filePath << "\n";
        return 1;
    }
    uint64_t fileSize = std::filesystem::file_size(p, ec);
    if (ec) {
        std::cerr << "Error: Failed to determine file size: " << ec.message() << "\n";
        return 1;
    }

    std::cout << "[Sender] Initializing Global P2P WebRTC Transfer...\n";
    std::cout << "[Sender] Target file: " << filePath << " (" << fileSize << " bytes)\n";
    std::cout << "[Sender] Signaling service: " << signalingWsUrl << "\n";

    CoordinatorOptions opts;
    opts.signalingHttpUrl = deriveHttpUrlFromWs(signalingWsUrl);
    opts.autoFetchTurn = true;
    opts.dataChannelLabel = "lan-drop-data";

    auto coordinator = std::make_shared<WebRtcSessionCoordinator>("cli-p2p-tx", WebRtcConfig{}, opts);

    coordinator->onRoomReady = [](const std::string& code) {
        std::cout << "\n============================================================\n"
                  << "  LAN-Drop Global P2P Transfer (Anonymous Guest Mode)\n"
                  << "============================================================\n"
                  << "  Room Code: " << code << "\n"
                  << "  Share this 8-character code with the receiver.\n"
                  << "  Waiting for receiver to join...\n"
                  << "============================================================\n" << std::flush;
    };

    coordinator->onPeerJoined = [](const std::string& peerId) {
        std::cout << "[Sender] Peer joined (" << peerId << "). Negotiating WebRTC connection...\n" << std::flush;
    };

    coordinator->onError = [](const std::string& err) {
        std::cerr << "[Sender Signaling Notice] " << err << "\n" << std::flush;
    };

    if (!coordinator->startAsInitiator(signalingWsUrl)) {
        std::cerr << "Error: Failed to initiate signaling session with " << signalingWsUrl << "\n";
        return 1;
    }

    std::string waitErr;
    auto channel = coordinator->waitForConnection(std::chrono::seconds(120), nullptr, &waitErr);
    if (!channel || !channel->isConnected()) {
        std::cerr << "Error: WebRTC P2P connection failed: " << (waitErr.empty() ? "timeout waiting for peer" : waitErr) << "\n";
        coordinator->close();
        return 1;
    }

    std::cout << "[Sender] Direct WebRTC P2P connection established! Streaming file...\n" << std::flush;

    TransferEngine engine;
    TransferOptions txOpts;
    std::string sendErr;
    bool ok = engine.sendFileOverChannel(
        "cli-p2p-tx",
        filePath,
        channel,
        txOpts,
        [](const TransferProgressUpdate& upd) {
            if (upd.state == TransferState::Transferring) {
                std::cout << "\r[Sender] Progress: " << upd.bytesTransferred << "/" << upd.totalBytes
                          << " bytes (" << static_cast<int>(upd.progressPercentage) << "%) [WebRTC P2P]" << std::flush;
            }
        },
        nullptr,
        &sendErr
    );

    std::cout << "\n";
    coordinator->close();

    if (!ok) {
        std::cerr << "Error: Transfer failed: " << sendErr << "\n";
        return 1;
    }

    std::cout << "[Sender] File transfer verified and acknowledged by receiver.\n";
    return 0;
#endif
}

[[maybe_unused]] static int runP2pReceiver(const std::string& rawRoomCode, const std::string& saveDir, const std::string& signalingWsUrl) {
#if !defined(LANDROP_HAVE_WEBRTC)
    (void)rawRoomCode;
    (void)saveDir;
    (void)signalingWsUrl;
    std::cerr << "Error: WebRTC P2P transport is not enabled in this build.\n"
              << "Rebuild LAN-Drop with -DLANDROP_ENABLE_WEBRTC=ON to enable global P2P file transfers.\n";
    return 1;
#else
    std::string normErr;
    std::string roomCode = WebRtcSessionCoordinator::normalizeRoomCode(rawRoomCode, &normErr);
    if (roomCode.empty()) {
        std::cerr << "Error: Invalid room code '" << rawRoomCode << "': " << normErr << "\n";
        return 1;
    }

    std::error_code ec;
    std::filesystem::create_directories(saveDir, ec);

    std::cout << "[Receiver] Initializing Global P2P WebRTC Transfer...\n";
    std::cout << "[Receiver] Target room: " << roomCode << "\n";
    std::cout << "[Receiver] Destination directory: " << saveDir << "\n";
    std::cout << "[Receiver] Signaling service: " << signalingWsUrl << "\n";

    CoordinatorOptions opts;
    opts.signalingHttpUrl = deriveHttpUrlFromWs(signalingWsUrl);
    opts.autoFetchTurn = true;
    opts.dataChannelLabel = "lan-drop-data";

    auto coordinator = std::make_shared<WebRtcSessionCoordinator>("cli-p2p-rx", WebRtcConfig{}, opts);

    coordinator->onError = [](const std::string& err) {
        std::cerr << "[Receiver Signaling Notice] " << err << "\n" << std::flush;
    };

    std::cout << "[Receiver] Connecting to signaling service and joining room " << roomCode << "...\n" << std::flush;
    if (!coordinator->startAsJoiner(signalingWsUrl, roomCode)) {
        std::cerr << "Error: Failed to join room " << roomCode << " at " << signalingWsUrl << "\n";
        return 1;
    }

    std::string waitErr;
    auto channel = coordinator->waitForConnection(std::chrono::seconds(120), nullptr, &waitErr);
    if (!channel || !channel->isConnected()) {
        std::cerr << "Error: WebRTC P2P connection failed: " << (waitErr.empty() ? "timeout or peer left" : waitErr) << "\n";
        coordinator->close();
        return 1;
    }

    std::cout << "[Receiver] Direct WebRTC P2P connection established! Awaiting incoming file...\n" << std::flush;

    TransferEngine engine;
    TransferOptions rxOpts;
    rxOpts.autoRenameDuplicates = true;
    std::string recvErr;

    bool ok = engine.receiveFileOverChannel(
        "cli-p2p-rx",
        saveDir,
        channel,
        rxOpts,
        [](const TransferProgressUpdate& upd) {
            if (upd.state == TransferState::Transferring) {
                std::cout << "\r[Receiver] Progress: " << upd.bytesTransferred << "/" << upd.totalBytes
                          << " bytes (" << static_cast<int>(upd.progressPercentage) << "%) [WebRTC P2P]" << std::flush;
            }
        },
        nullptr,
        &recvErr
    );

    std::cout << "\n";
    coordinator->close();

    if (!ok) {
        std::cerr << "Error: Transfer failed: " << recvErr << "\n";
        return 1;
    }

    std::cout << "[Receiver] File verified successfully (SHA-256 integrity confirmed).\n";
    return 0;
#endif
}


static void logStartupMessage(const std::string& message) {
    std::string logDir;
#if defined(_WIN32)
    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData && localAppData[0] != '\0') {
        logDir = std::string(localAppData) + "\\lan-drop";
    } else {
        const char* userProfile = std::getenv("USERPROFILE");
        if (userProfile && userProfile[0] != '\0') {
            logDir = std::string(userProfile) + "\\AppData\\Local\\lan-drop";
        }
    }
#else
    const char* xdgState = std::getenv("XDG_STATE_HOME");
    if (xdgState && xdgState[0] != '\0') {
        logDir = std::string(xdgState) + "/lan-drop";
    } else {
        const char* home = std::getenv("HOME");
        if (home && home[0] != '\0') {
            logDir = std::string(home) + "/.local/share/lan-drop";
        }
    }
#endif
    if (logDir.empty()) {
        std::error_code ec;
        auto tempP = std::filesystem::temp_directory_path(ec);
        if (!ec) {
            logDir = tempP.string();
        }
    }

    if (!logDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(logDir, ec);
        std::filesystem::path logFile = std::filesystem::path(logDir) / "startup.log";
        std::ofstream out(logFile, std::ios::app);
        if (out.is_open()) {
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            char buf[64] = {0};
#if defined(_WIN32)
            ctime_s(buf, sizeof(buf), &now);
#else
            ctime_r(&now, buf);
#endif
            std::string timeStr = buf;
            if (!timeStr.empty() && timeStr.back() == '\n') timeStr.pop_back();
            out << "[" << timeStr << "] " << message << "\n";
        }
    }
}

static int runGui(int argc, char* argv[], bool isMobile = false) {
    logStartupMessage("Starting LAN-Drop GUI...");
    try {
#if defined(LANDROP_BUILD_QT)
        (void)isMobile;
        QApplication qtApp(argc, argv);
        ui::QtDesktopApp mainWindow;
        mainWindow.show();
        logStartupMessage("LAN-Drop GUI initialized and window shown successfully.");
        return qtApp.exec();
#elif defined(LANDROP_BUILD_GTK) || !defined(LANDROP_BUILD_QT)
        ui::GtkDesktopApp app(isMobile);
        return app.run(argc, argv);
#else
        (void)argc;
        (void)argv;
        (void)isMobile;
        std::cerr << "Error: No GUI backend compiled in.\n";
        logStartupMessage("Error: No GUI backend compiled in.");
        return 1;
#endif
    } catch (const std::exception& e) {
        std::string err = "Fatal unhandled exception in GUI: " + std::string(e.what());
        std::cerr << err << "\n";
        logStartupMessage(err);
        return 1;
    } catch (...) {
        std::string err = "Fatal unknown exception occurred in GUI";
        std::cerr << err << "\n";
        logStartupMessage(err);
        return 1;
    }
}

static int runMobileTest() {
    std::cout << "========================================\n";
    std::cout << "  LAN-Drop: Mobile Subsystem Unit Test  \n";
    std::cout << "========================================\n\n";

    std::cout << "[1] Testing NFC Fast Pairing Session:\n";
    mobile::NfcPairingSession nfcSender("phone-s24-uuid", "Galaxy S24 Ultra");
    std::string nfcTag = nfcSender.generateNdefPayload("192.168.1.180", 8848);
    std::cout << "  Generated NFC Tag: " << nfcTag << "\n";

    mobile::NfcPairingSession nfcReceiver("laptop-uuid", "Dell XPS 15");
    auto parsedOpt = nfcReceiver.parseNdefPayload(nfcTag);
    if (parsedOpt.has_value()) {
        std::cout << "  Parsed Device: " << parsedOpt->deviceName << " (" << parsedOpt->ipAddress << ":" << parsedOpt->port << ")\n";
        std::cout << "  Session PIN: " << parsedOpt->sessionPin << ", Key: " << parsedOpt->ephemeralPublicKey << "\n";
        std::cout << "  NFC Status: OK\n";
    } else {
        std::cerr << "  NFC Parse Error!\n";
        return 1;
    }

    std::cout << "\n[2] Testing AirDrop Protocol Bridge:\n";
    mobile::AirDropProtocolBridge airdrop("macbook-air-uuid", "MacBook Air M3");
    airdrop.startAdvertising(8770);
    std::cout << "  Bonjour Record: " << airdrop.generateBonjourRecord() << "\n";
    std::cout << "  Discover Response: " << airdrop.buildDiscoverResponse() << "\n";

    mobile::AirDropAsset asset{"sample_photo.heic", 4194304, "image/heic", "d5a8b79c..."};
    std::string askPayload = airdrop.buildAskRequest(asset);
    std::cout << "  Ask Payload: " << askPayload.substr(0, 100) << "...\n";

    mobile::AirDropAsset receivedAsset;
    if (airdrop.handleIncomingAsk(askPayload, receivedAsset)) {
        std::cout << "  Incoming Ask File: " << receivedAsset.fileName << " (" << receivedAsset.fileSize << " bytes)\n";
        airdrop.acceptIncomingTransfer();
        std::cout << "  AirDrop Status: ACCEPTED / TRANSFERRING\n";
    }

    std::cout << "\n[3] Testing C-ABI / FFI Bridge (for Flutter/Kotlin/Swift):\n";
    LandropHandle handle = landrop_mobile_create("Flutter-Client", "mob-uuid-flutter");
    landrop_mobile_start_discovery(handle, 8848);
    int count = landrop_mobile_get_peer_count(handle);
    std::cout << "  Discovered peer count via FFI: " << count << "\n";

    char name[64], ip[32];
    uint16_t port = 0;
    if (landrop_mobile_get_peer_info(handle, 0, name, sizeof(name), ip, sizeof(ip), &port) == 0) {
        std::cout << "  Peer 0 via FFI: " << name << " @ " << ip << ":" << port << "\n";
    }

    char nfcOut[256];
    landrop_mobile_nfc_generate_payload(handle, "192.168.1.99", 8848, nfcOut, sizeof(nfcOut));
    std::cout << "  NFC payload generated via FFI: " << nfcOut << "\n";

    landrop_mobile_destroy(handle);
    std::cout << "  FFI Cleaned up.\n\n";

    std::cout << "Mobile Subsystem Test: SUCCESS\n";
    return 0;
}

static int runDemo() {
    std::cout << "========================================\n";
    std::cout << "  LAN-Drop: Core Architecture OOP Demo  \n";
    std::cout << "========================================\n\n";

    std::cout << "[1] Hierarchy 1 (Network Nodes) Dynamic Polymorphism:\n";
    std::vector<std::shared_ptr<NetworkPeer>> peers;
    peers.push_back(std::make_shared<LocalPeer>("peer-lan-1", "MacBook-Air", "192.168.1.45", 8848, "macOS", "Apple M2"));
    peers.push_back(std::make_shared<LocalPeer>("peer-lan-2", "ThinkPad-Linux", "192.168.1.112", 8848, "Linux", "Lenovo X1"));
    peers.push_back(std::make_shared<CloudPeer>("peer-cloud-1", "DropCloud-EU", "https://relay.dropcloud.internal", 443, "DropCloud", "auth_token_secret"));

    for (const auto& peer : peers) {
        peer->initialize();
        std::cout << "  * " << *peer << "\n";
    }

    std::cout << "\n[2] Static Polymorphism (Templates: filterPeers<T>):\n";
    auto localNodes = NetworkPeer::filterPeers<LocalPeer>(peers);
    std::cout << "  Filtered Local Nodes count: " << localNodes.size() << "\n";
    for (const auto& local : localNodes) {
        std::cout << "    - " << local->getDisplayName() << " (OS: " << local->getOsName() << ")\n";
    }

    auto cloudNodes = NetworkPeer::filterPeers<CloudPeer>(peers);
    std::cout << "  Filtered Cloud Nodes count: " << cloudNodes.size() << "\n";
    for (const auto& cloud : cloudNodes) {
        std::cout << "    - " << cloud->getDisplayName() << " (Provider: " << cloud->getCloudProvider() << ")\n";
    }

    std::cout << "\n[3] Static Polymorphism (Templates: ChunkBuffer<T>):\n";
    ChunkBuffer<std::string> chunkBuffer(4, "DemoBuffer");
    chunkBuffer.push("CHUNK_BLOCK_001");
    chunkBuffer.push("CHUNK_BLOCK_002");
    chunkBuffer.push("CHUNK_BLOCK_003");
    std::cout << "  Buffer tag: " << chunkBuffer.getTag() << ", size: " << chunkBuffer.size() << "/" << chunkBuffer.capacity() << "\n";
    std::cout << "  Buffer[1] via operator[]: " << chunkBuffer[1] << "\n";

    std::cout << "\n[4] Hierarchy 2 (Transfer Tasks) Dynamic Polymorphism:\n";
    std::vector<std::shared_ptr<TransferTask>> tasks;
    tasks.push_back(std::make_shared<P2PTransferTask>("t-01", "firmware.bin", 1024 * 1024 * 4, TransferDirection::Upload, "peer-lan-1"));
    tasks.push_back(std::make_shared<HttpWebTransferTask>("t-02", "photo_gallery.zip", 1024 * 1024 * 15, TransferDirection::Download, 8080, "qr_token_771"));

    for (const auto& task : tasks) {
        task->initialize();
        std::vector<uint8_t> dummyChunk(64 * 1024, 0xAA);
        task->processChunk(0, dummyChunk);
        std::cout << "  * " << *task << " | Progress: " << task->getProgressPercentage() << "%\n";
    }

    std::cout << "\n[5] Core Helper Entities:\n";
    SharedFolderManager folderManager("./demo_shared");
    std::cout << "  SharedFolderManager root: " << folderManager.getRootPath() << ", files: " << folderManager.getSharedFileCount() << "\n";

    MulticastBeacon beacon("LocalTestNode", "uuid-test-99", 8848);
    beacon.start();
    std::string payload = beacon.serializePayload();
    std::cout << "  MulticastBeacon payload: " << payload << "\n";
    beacon.simulateReceiveBeacon(payload);
    std::cout << "  Beacons received: " << beacon.getBeaconsReceived() << "\n";

    std::cout << "\n[6] Loopback Network File Transfer Test (127.0.0.1:8999):\n";
    std::string testFileName = "demo_transfer_test.tmp";
    std::string testContent = "LAN-Drop high-performance binary socket transfer demonstration.";
    {
        std::ofstream out(testFileName);
        out << testContent;
    }

    uint16_t testPort = 8999;
    std::string testSaveDir = "./demo_received";

    std::thread receiverThread([testPort, testSaveDir]() {
        runReceiver(testPort, testSaveDir);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    int sendResult = runSender(testFileName, "127.0.0.1", testPort);
    if (receiverThread.joinable()) {
        receiverThread.join();
    }

    std::filesystem::remove(testFileName);
    std::filesystem::remove_all(testSaveDir);
    std::filesystem::remove_all("./demo_shared");

    std::cout << "\nDemo status: " << (sendResult == 0 ? "SUCCESS" : "FAILURE") << "\n";
    return sendResult;
}

int main(int argc, char* argv[]) {
    initSocketSubsystem();

    std::string argSummary = "LAN-Drop started with argc=" + std::to_string(argc);
    for (int i = 0; i < argc; ++i) {
        argSummary += " [" + std::string(argv[i]) + "]";
    }
    logStartupMessage(argSummary);

    int exitCode = 0;
    if (argc == 1) {
        exitCode = runGui(argc, argv, false);
    } else {
        std::string mode = argv[1];
        if (mode == "--help" || mode == "-h" || mode == "help") {
            printUsage(argv[0]);
            exitCode = 0;
        } else if (mode == "demo") {
            exitCode = runDemo();
        } else if (mode == "gui" || mode == "desktop-gui") {
            exitCode = runGui(argc, argv, false);
        } else if (mode == "mobile-gui" || mode == "--mobile" || mode == "-m") {
            exitCode = runGui(argc, argv, true);
        } else if (mode == "mobile-test") {
            exitCode = runMobileTest();
        } else if (mode == "send") {
            if (argc < 4) {
                printUsage(argv[0]);
                exitCode = 1;
            } else {
                std::string filePath = argv[2];
                std::string ip = argv[3];
                uint16_t port = 8848;
                bool useTls = false;
                std::string expectedFingerprint;
                for (int i = 4; i < argc; ++i) {
                    std::string arg = argv[i];
                    if (arg == "--tls") {
                        useTls = true;
                        if (i + 1 < argc && argv[i + 1][0] != '-') {
                            expectedFingerprint = argv[++i];
                        }
                    } else if (!arg.empty() && std::isdigit(static_cast<unsigned char>(arg[0]))) {
                        port = static_cast<uint16_t>(std::stoi(arg));
                    }
                }
                exitCode = runSender(filePath, ip, port, useTls, expectedFingerprint);
            }
        } else if (mode == "receive") {
            uint16_t port = 8848;
            std::string saveDir = "./downloads";
            bool useTls = false;
            for (int i = 2; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "--tls") {
                    useTls = true;
                } else if (!arg.empty() && std::isdigit(static_cast<unsigned char>(arg[0]))) {
                    port = static_cast<uint16_t>(std::stoi(arg));
                } else {
                    saveDir = arg;
                }
            }
            exitCode = runReceiver(port, saveDir, useTls);
        } else if (mode == "p2p-send") {
#if !defined(LANDROP_HAVE_WEBRTC)
            std::cerr << "Error: WebRTC P2P transport is not enabled in this build.\n"
                      << "Rebuild LAN-Drop with -DLANDROP_ENABLE_WEBRTC=ON to enable global P2P file transfers.\n";
            exitCode = 1;
#else
            if (argc < 3) {
                printUsage(argv[0]);
                exitCode = 1;
            } else {
                std::string filePath = argv[2];
                const char* envSignaling = std::getenv("LANDROP_SIGNALING_URL");
                std::string signalingWsUrl = (envSignaling && envSignaling[0] != '\0') ? envSignaling : "wss://landrop-server.duckdns.org/ws/signaling";
                for (int i = 3; i < argc; ++i) {
                    std::string arg = argv[i];
                    if (arg == "--signaling" && i + 1 < argc) {
                        signalingWsUrl = argv[++i];
                    }
                }
                exitCode = runP2pSender(filePath, signalingWsUrl);
            }
#endif
        } else if (mode == "p2p-receive") {
#if !defined(LANDROP_HAVE_WEBRTC)
            std::cerr << "Error: WebRTC P2P transport is not enabled in this build.\n"
                      << "Rebuild LAN-Drop with -DLANDROP_ENABLE_WEBRTC=ON to enable global P2P file transfers.\n";
            exitCode = 1;
#else
            if (argc < 3) {
                printUsage(argv[0]);
                exitCode = 1;
            } else {
                std::string roomCode = argv[2];
                std::string saveDir = "./downloads";
                const char* envSignaling = std::getenv("LANDROP_SIGNALING_URL");
                std::string signalingWsUrl = (envSignaling && envSignaling[0] != '\0') ? envSignaling : "wss://landrop-server.duckdns.org/ws/signaling";
                for (int i = 3; i < argc; ++i) {
                    std::string arg = argv[i];
                    if (arg == "--signaling" && i + 1 < argc) {
                        signalingWsUrl = argv[++i];
                    } else if (!arg.empty() && arg[0] != '-') {
                        saveDir = arg;
                    }
                }
                exitCode = runP2pReceiver(roomCode, saveDir, signalingWsUrl);
            }
#endif
        } else if (mode == "doctor") {
            DoctorOptions opts;
            bool jsonOutput = false;
            for (int i = 2; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "--json") {
                    jsonOutput = true;
                } else if (arg == "--stun" && i + 1 < argc) {
                    opts.stunServer = argv[++i];
                } else if (arg == "--signaling" && i + 1 < argc) {
                    opts.signalingUrl = argv[++i];
                } else if (arg == "--no-stun") {
                    opts.checkStun = false;
                } else if (arg == "--no-signaling") {
                    opts.checkSignaling = false;
                }
            }
            DoctorEngine doctor(opts);
            auto report = doctor.runAllDiagnostics();
            if (jsonOutput) {
                std::cout << report.toJson() << "\n";
            } else {
                std::cout << report.toFormattedText(true) << "\n";
            }
            exitCode = (report.overallStatus == DiagnosticStatus::Fail) ? 1 : 0;
        } else {
            // Unrecognized or GUI parameters (e.g. desktop launcher %U, Qt flags -platform, -style, etc.)
            // Launch GUI application and let Qt parse framework arguments
            exitCode = runGui(argc, argv, false);
        }
    }

    cleanupSocketSubsystem();
    return exitCode;
}
