#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <filesystem>
#include <chrono>

#include "landrop/utils/Types.hpp"
#include "landrop/utils/CryptoFallback.hpp"
#include "landrop/core/ChunkBuffer.hpp"
#include "landrop/core/FileChunkHasher.hpp"
#include "landrop/core/NetworkSession.hpp"
#include "landrop/core/SharedFolderManager.hpp"
#include "landrop/core/MulticastBeacon.hpp"
#include "landrop/peers/NetworkPeer.hpp"
#include "landrop/peers/LocalPeer.hpp"
#include "landrop/peers/CloudPeer.hpp"
#include "landrop/tasks/TransferTask.hpp"
#include "landrop/tasks/P2PTransferTask.hpp"
#include "landrop/tasks/HttpWebTransferTask.hpp"

using namespace landrop;

static void printUsage(const char* progName) {
    std::cout << "Usage:\n";
    std::cout << "  " << progName << " send <file_path> <receiver_ip> [port]\n";
    std::cout << "  " << progName << " receive [port] [save_dir]\n";
    std::cout << "  " << progName << " demo\n";
}

static int runSender(const std::string& filePath, const std::string& receiverIp, uint16_t port) {
    std::filesystem::path p(filePath);
    if (!std::filesystem::exists(p) || !std::filesystem::is_regular_file(p)) {
        std::cerr << "Error: file does not exist: " << filePath << "\n";
        return 1;
    }

    uint64_t fileSize = std::filesystem::file_size(p);
    std::string fileName = p.filename().string();

    FileChunkHasher hasher;
    auto fullHashOpt = hasher.computeFullFileHash(filePath);
    std::string fileHash = fullHashOpt.value_or("");

    std::cout << "[Sender] File: " << fileName << " (" << fileSize << " bytes)\n";
    std::cout << "[Sender] SHA-256: " << fileHash << "\n";
    std::cout << "[Sender] Connecting to " << receiverIp << ":" << port << "...\n";

    NetworkSession session("sender-session", receiverIp, port);
    if (!session.connectTo(receiverIp, port)) {
        std::cerr << "Error: failed to connect to receiver\n";
        return 1;
    }
    std::cout << "[Sender] Connected successfully.\n";

    P2PTransferTask task("task-upload-01", fileName, fileSize, TransferDirection::Upload, "remote-node");
    task.initialize();

    uint32_t nameLen = static_cast<uint32_t>(fileName.size());
    std::vector<uint8_t> header;
    header.resize(sizeof(uint32_t) + nameLen + sizeof(uint64_t) + 64);

    uint8_t* ptr = header.data();
    std::memcpy(ptr, &nameLen, sizeof(uint32_t));
    ptr += sizeof(uint32_t);

    std::memcpy(ptr, fileName.data(), nameLen);
    ptr += nameLen;

    std::memcpy(ptr, &fileSize, sizeof(uint64_t));
    ptr += sizeof(uint64_t);

    std::string hashPadded = fileHash;
    hashPadded.resize(64, '0');
    std::memcpy(ptr, hashPadded.data(), 64);

    if (!session.sendData(header)) {
        std::cerr << "Error: failed to send metadata header\n";
        return 1;
    }

    std::ifstream inFile(filePath, std::ios::binary);
    std::vector<uint8_t> chunkBuf(64 * 1024);
    size_t chunkIndex = 0;
    uint64_t sentBytes = 0;

    auto startTime = std::chrono::steady_clock::now();

    while (sentBytes < fileSize && inFile) {
        inFile.read(reinterpret_cast<char*>(chunkBuf.data()), static_cast<std::streamsize>(chunkBuf.size()));
        size_t count = static_cast<size_t>(inFile.gcount());
        if (count == 0) {
            break;
        }

        uint32_t chunkSizeU32 = static_cast<uint32_t>(count);
        if (!session.sendAll(reinterpret_cast<const uint8_t*>(&chunkSizeU32), sizeof(uint32_t))) {
            std::cerr << "Error sending chunk size\n";
            return 1;
        }

        if (!session.sendAll(chunkBuf.data(), count)) {
            std::cerr << "Error sending chunk payload\n";
            return 1;
        }

        sentBytes += count;
        std::vector<uint8_t> chunkSlice(chunkBuf.begin(), chunkBuf.begin() + count);
        task.processChunk(chunkIndex++, chunkSlice);

        double progress = (static_cast<double>(sentBytes) / static_cast<double>(fileSize)) * 100.0;
        std::cout << "\r[Sender] Progress: " << sentBytes << "/" << fileSize << " bytes ("
                  << static_cast<int>(progress) << "%)" << std::flush;
    }
    std::cout << "\n";

    uint8_t ack = 0;
    if (session.receiveExact(&ack, 1) && ack == 1) {
        std::cout << "[Sender] Transfer verified and confirmed by receiver.\n";
    } else {
        std::cerr << "[Sender] Warning: did not receive positive confirmation.\n";
    }

    auto endTime = std::chrono::steady_clock::now();
    double duration = std::chrono::duration<double>(endTime - startTime).count();
    std::cout << "[Sender] Finished in " << duration << " seconds.\n";
    return 0;
}

static int runReceiver(uint16_t port, const std::string& saveDir) {
    std::filesystem::create_directories(saveDir);

    std::cout << "[Receiver] Listening on port " << port << "...\n";
    std::cout << "[Receiver] Save directory: " << saveDir << "\n";

    NetworkSession session("receiver-session", "0.0.0.0", port);
    if (!session.listenOn(port)) {
        std::cerr << "Error: failed to bind and listen on port " << port << "\n";
        return 1;
    }

    if (!session.acceptIncoming()) {
        std::cerr << "Error: failed to accept incoming connection\n";
        return 1;
    }

    std::cout << "[Receiver] Connection accepted from " << session.getRemoteIp() << ":" << session.getRemotePort() << "\n";

    uint32_t nameLen = 0;
    if (!session.receiveExact(reinterpret_cast<uint8_t*>(&nameLen), sizeof(uint32_t))) {
        std::cerr << "Error receiving name length\n";
        return 1;
    }

    if (nameLen > 1024) {
        std::cerr << "Error: invalid file name length\n";
        return 1;
    }

    std::string fileName(nameLen, '\0');
    if (!session.receiveExact(reinterpret_cast<uint8_t*>(fileName.data()), nameLen)) {
        std::cerr << "Error receiving file name\n";
        return 1;
    }

    uint64_t fileSize = 0;
    if (!session.receiveExact(reinterpret_cast<uint8_t*>(&fileSize), sizeof(uint64_t))) {
        std::cerr << "Error receiving file size\n";
        return 1;
    }

    std::string expectedHash(64, '\0');
    if (!session.receiveExact(reinterpret_cast<uint8_t*>(expectedHash.data()), 64)) {
        std::cerr << "Error receiving expected hash\n";
        return 1;
    }

    std::cout << "[Receiver] Incoming file: " << fileName << " (" << fileSize << " bytes)\n";
    std::cout << "[Receiver] Expected SHA-256: " << expectedHash << "\n";

    std::filesystem::path outPath = std::filesystem::path(saveDir) / fileName;
    std::ofstream outFile(outPath, std::ios::binary);
    if (!outFile.is_open()) {
        std::cerr << "Error creating output file: " << outPath.string() << "\n";
        return 1;
    }

    P2PTransferTask task("task-download-01", fileName, fileSize, TransferDirection::Download, "sender-node");
    task.initialize();

    uint64_t totalReceived = 0;
    size_t chunkIndex = 0;
    std::vector<uint8_t> chunkBuf;

    while (totalReceived < fileSize) {
        uint32_t chunkSize = 0;
        if (!session.receiveExact(reinterpret_cast<uint8_t*>(&chunkSize), sizeof(uint32_t))) {
            std::cerr << "Error receiving chunk size\n";
            return 1;
        }

        chunkBuf.resize(chunkSize);
        if (!session.receiveExact(chunkBuf.data(), chunkSize)) {
            std::cerr << "Error receiving chunk data\n";
            return 1;
        }

        outFile.write(reinterpret_cast<const char*>(chunkBuf.data()), static_cast<std::streamsize>(chunkSize));
        totalReceived += chunkSize;
        task.processChunk(chunkIndex++, chunkBuf);

        double progress = (static_cast<double>(totalReceived) / static_cast<double>(fileSize)) * 100.0;
        std::cout << "\r[Receiver] Progress: " << totalReceived << "/" << fileSize << " bytes ("
                  << static_cast<int>(progress) << "%)" << std::flush;
    }
    std::cout << "\n";
    outFile.close();

    FileChunkHasher hasher;
    auto actualHashOpt = hasher.computeFullFileHash(outPath.string());
    std::string actualHash = actualHashOpt.value_or("");

    std::cout << "[Receiver] Actual SHA-256:   " << actualHash << "\n";
    uint8_t ack = (actualHash == expectedHash) ? 1 : 0;
    session.sendAll(&ack, 1);

    if (ack == 1) {
        std::cout << "[Receiver] File verified successfully: " << outPath.string() << "\n";
        return 0;
    } else {
        std::cerr << "[Receiver] Hash mismatch! File may be corrupted.\n";
        return 2;
    }
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
    if (argc == 1) {
        return runDemo();
    }

    std::string mode = argv[1];
    if (mode == "demo") {
        return runDemo();
    }

    if (mode == "send") {
        if (argc < 4) {
            printUsage(argv[0]);
            return 1;
        }
        std::string filePath = argv[2];
        std::string ip = argv[3];
        uint16_t port = (argc >= 5) ? static_cast<uint16_t>(std::stoi(argv[4])) : 8848;
        return runSender(filePath, ip, port);
    }

    if (mode == "receive") {
        uint16_t port = (argc >= 3) ? static_cast<uint16_t>(std::stoi(argv[2])) : 8848;
        std::string saveDir = (argc >= 4) ? argv[3] : "./downloads";
        return runReceiver(port, saveDir);
    }

    printUsage(argv[0]);
    return 1;
}
