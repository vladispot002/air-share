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
        } else {
            // Unrecognized or GUI parameters (e.g. desktop launcher %U, Qt flags -platform, -style, etc.)
            // Launch GUI application and let Qt parse framework arguments
            exitCode = runGui(argc, argv, false);
        }
    }

    cleanupSocketSubsystem();
    return exitCode;
}
