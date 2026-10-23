#include "landrop/core/TransferEngine.hpp"
#include "landrop/core/TlsContext.hpp"
#include <iostream>
#include <fstream>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace landrop {

TransferEngine::TransferEngine() = default;

TransferEngine::~TransferEngine() {
    stopServer();
}

void TransferEngine::cancelTask(const std::string& taskId) {
    std::lock_guard<std::mutex> lock(m_cancelTokensMutex);
    auto it = m_cancelTokens.find(taskId);
    if (it != m_cancelTokens.end() && it->second) {
        it->second->store(true);
    }
}

void TransferEngine::registerCancelToken(const std::string& taskId, std::shared_ptr<std::atomic<bool>> token) {
    std::lock_guard<std::mutex> lock(m_cancelTokensMutex);
    m_cancelTokens[taskId] = std::move(token);
}

void TransferEngine::unregisterCancelToken(const std::string& taskId) {
    std::lock_guard<std::mutex> lock(m_cancelTokensMutex);
    m_cancelTokens.erase(taskId);
}

bool TransferEngine::sendFile(
    const std::string& taskId,
    const std::string& filePath,
    const std::string& remoteIp,
    uint16_t remotePort,
    const TransferOptions& options,
    TransferProgressCallback progressCb,
    std::atomic<bool>* cancelFlag,
    std::string* outError
) {
    auto internalCancel = std::make_shared<std::atomic<bool>>(false);
    registerCancelToken(taskId, internalCancel);
    struct CancelScopeCleaner {
        TransferEngine* engine;
        std::string tid;
        ~CancelScopeCleaner() { if (engine) engine->unregisterCancelToken(tid); }
    } cleaner{this, taskId};

    auto isCancelled = [&]() -> bool {
        return (cancelFlag && cancelFlag->load()) || internalCancel->load();
    };

    auto reportError = [&](const std::string& err) {
        if (outError) *outError = err;
        if (progressCb) {
            TransferProgressUpdate upd;
            upd.taskId = taskId;
            upd.state = TransferState::Failed;
            upd.message = err;
            progressCb(upd);
        }
        return false;
    };

    std::filesystem::path p(filePath);
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || !std::filesystem::is_regular_file(p, ec)) {
        return reportError("File does not exist or is not a regular file: " + filePath);
    }

    uint64_t fileSize = std::filesystem::file_size(p, ec);
    if (ec) {
        return reportError("Failed to determine file size: " + ec.message());
    }
    if (fileSize > options.maxFileSizeLimit) {
        return reportError("File size exceeds configured maximum limit: " + std::to_string(fileSize) + " > " + std::to_string(options.maxFileSizeLimit));
    }

    std::string fileName = p.filename().string();
    if (fileName.empty() || fileName.size() > 255 || !isSafeFileName(fileName)) {
        return reportError("Invalid or unsafe filename: " + fileName);
    }

    if (progressCb) {
        TransferProgressUpdate upd;
        upd.taskId = taskId;
        upd.fileName = fileName;
        upd.totalBytes = fileSize;
        upd.bytesTransferred = 0;
        upd.state = TransferState::Connecting;
        upd.message = "Computing SHA-256 hash...";
        progressCb(upd);
    }

    // Check cancellation before hashing
    if (isCancelled()) {
        if (progressCb) {
            TransferProgressUpdate upd;
            upd.taskId = taskId;
            upd.fileName = fileName;
            upd.state = TransferState::Cancelled;
            upd.message = "Transfer cancelled by user";
            progressCb(upd);
        }
        return false;
    }

    FileChunkHasher hasher;
    auto fullHashOpt = hasher.computeFullFileHash(filePath);
    if (!fullHashOpt.has_value()) {
        return reportError("Failed to compute SHA-256 checksum for: " + filePath);
    }
    std::string fileHash = fullHashOpt.value();

    if (progressCb) {
        TransferProgressUpdate upd;
        upd.taskId = taskId;
        upd.fileName = fileName;
        upd.totalBytes = fileSize;
        upd.state = TransferState::Connecting;
        upd.message = "Connecting to " + remoteIp + ":" + std::to_string(remotePort) + "...";
        progressCb(upd);
    }

    NetworkSession session("tx-" + taskId, remoteIp, remotePort);
    if (options.useTls) {
        TlsConfig tlsConfig;
        tlsConfig.role = TlsRole::Client;
        tlsConfig.verifyMode = TlsVerificationMode::VerifyPinnedFingerprint;
        tlsConfig.expectedFingerprint = options.expectedFingerprint;
        session.enableTls(tlsConfig);
    }

    if (!session.connectTo(remoteIp, remotePort)) {
        if (options.useTls && session.getTlsStatus() != TlsStatus::Connected && session.getTlsStatus() != TlsStatus::None) {
            return reportError(std::string("TLS connection failed: ") + TlsHelper::statusToString(session.getTlsStatus()));
        }
        return reportError("Connection to " + remoteIp + ":" + std::to_string(remotePort) + " failed");
    }

    // Check cancellation after connection
    if (isCancelled()) {
        session.closeSession();
        if (progressCb) {
            TransferProgressUpdate upd;
            upd.taskId = taskId;
            upd.fileName = fileName;
            upd.state = TransferState::Cancelled;
            upd.message = "Transfer cancelled by user";
            progressCb(upd);
        }
        return false;
    }

    // Prepare wire protocol metadata header
    uint32_t nameLen = static_cast<uint32_t>(fileName.size());
    std::vector<uint8_t> header(sizeof(uint32_t) + nameLen + sizeof(uint64_t) + 64);
    uint8_t* ptr = header.data();
    std::memcpy(ptr, &nameLen, sizeof(uint32_t)); ptr += sizeof(uint32_t);
    std::memcpy(ptr, fileName.data(), nameLen); ptr += nameLen;
    std::memcpy(ptr, &fileSize, sizeof(uint64_t)); ptr += sizeof(uint64_t);
    std::string hashPadded = fileHash;
    hashPadded.resize(64, '0');
    std::memcpy(ptr, hashPadded.data(), 64);

    if (!session.sendAll(header.data(), header.size())) {
        return reportError("Failed to send metadata header to receiver");
    }

    std::ifstream in(filePath, std::ios::binary);
    if (!in.is_open()) {
        return reportError("Failed to open local file for reading: " + filePath);
    }

    size_t chunkCapacity = options.chunkSize > 0 ? options.chunkSize : (64 * 1024);
    if (chunkCapacity > 4 * 1024 * 1024) chunkCapacity = 4 * 1024 * 1024;
    std::vector<uint8_t> chunkBuf(chunkCapacity);

    uint64_t sentBytes = 0;
    auto startTime = std::chrono::steady_clock::now();
    auto lastProgressReport = startTime;

    if (progressCb) {
        TransferProgressUpdate upd;
        upd.taskId = taskId;
        upd.fileName = fileName;
        upd.totalBytes = fileSize;
        upd.bytesTransferred = 0;
        upd.progressPercentage = 0.0;
        upd.state = TransferState::Transferring;
        upd.message = "Streaming file chunks...";
        progressCb(upd);
    }

    while (sentBytes < fileSize && in) {
        if (isCancelled()) {
            session.closeSession();
            if (progressCb) {
                TransferProgressUpdate upd;
                upd.taskId = taskId;
                upd.fileName = fileName;
                upd.state = TransferState::Cancelled;
                upd.message = "Transfer cancelled by user";
                progressCb(upd);
            }
            return false;
        }

        in.read(reinterpret_cast<char*>(chunkBuf.data()), static_cast<std::streamsize>(chunkBuf.size()));
        size_t count = static_cast<size_t>(in.gcount());
        if (count == 0) break;

        uint32_t countU32 = static_cast<uint32_t>(count);
        if (!session.sendAll(reinterpret_cast<const uint8_t*>(&countU32), sizeof(uint32_t))) {
            return reportError("Connection interrupted while sending chunk header");
        }
        if (!session.sendAll(chunkBuf.data(), count)) {
            return reportError("Connection interrupted while sending chunk payload");
        }

        sentBytes += count;
        auto now = std::chrono::steady_clock::now();
        double elapsedTotal = std::chrono::duration<double>(now - startTime).count();
        double speedBps = elapsedTotal > 0.05 ? (static_cast<double>(sentBytes) / elapsedTotal) : 0.0;

        // Speed throttling if speedLimitKbps > 0
        if (options.speedLimitKbps > 0) {
            double maxSpeedBps = static_cast<double>(options.speedLimitKbps) * 1024.0;
            if (speedBps > maxSpeedBps && maxSpeedBps > 0.0) {
                double expectedSeconds = static_cast<double>(sentBytes) / maxSpeedBps;
                double delay = expectedSeconds - elapsedTotal;
                if (delay > 0.001) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(delay * 1000.0)));
                }
            }
        }

        if (progressCb && (sentBytes == count || std::chrono::duration<double>(now - lastProgressReport).count() >= 0.1)) {
            lastProgressReport = now;
            TransferProgressUpdate upd;
            upd.taskId = taskId;
            upd.fileName = fileName;
            upd.totalBytes = fileSize;
            upd.bytesTransferred = sentBytes;
            upd.progressPercentage = (static_cast<double>(sentBytes) / static_cast<double>(fileSize)) * 100.0;
            upd.speedBytesPerSec = speedBps;
            upd.state = TransferState::Transferring;
            upd.message = "Streaming file chunks...";
            progressCb(upd);
        }
    }

    in.close();

    if (sentBytes < fileSize) {
        return reportError("File read ended prematurely before all bytes were sent");
    }

    if (progressCb) {
        TransferProgressUpdate upd;
        upd.taskId = taskId;
        upd.fileName = fileName;
        upd.totalBytes = fileSize;
        upd.bytesTransferred = sentBytes;
        upd.progressPercentage = 100.0;
        upd.state = TransferState::Transferring;
        upd.message = "Waiting for receiver SHA-256 verification ACK...";
        progressCb(upd);
    }

    // Wait for receiver ACK
    uint8_t ack = 0;
    if (!session.receiveExact(&ack, 1)) {
        return reportError("Connection dropped while waiting for receiver verification ACK");
    }

    if (ack != 1) {
        return reportError("Receiver rejected transfer: checksum mismatch or storage error");
    }

    if (progressCb) {
        TransferProgressUpdate upd;
        upd.taskId = taskId;
        upd.fileName = fileName;
        upd.totalBytes = fileSize;
        upd.bytesTransferred = sentBytes;
        upd.progressPercentage = 100.0;
        upd.state = TransferState::Completed;
        upd.message = "Transfer verified and completed successfully!";
        progressCb(upd);
    }

    return true;
}

bool TransferEngine::receiveFileOnce(
    const std::string& taskId,
    uint16_t listenPort,
    const std::string& saveDirectory,
    const TransferOptions& options,
    TransferProgressCallback progressCb,
    std::atomic<bool>* cancelFlag,
    std::string* outError
) {
    auto reportError = [&](const std::string& err) {
        if (outError) *outError = err;
        if (progressCb) {
            TransferProgressUpdate upd;
            upd.taskId = taskId;
            upd.state = TransferState::Failed;
            upd.message = err;
            progressCb(upd);
        }
        return false;
    };

    std::error_code ec;
    std::filesystem::create_directories(saveDirectory, ec);
    if (ec) {
        return reportError("Failed to create save directory: " + saveDirectory);
    }

    auto session = std::make_unique<NetworkSession>("rx-" + taskId, "0.0.0.0", listenPort);
    if (options.useTls) {
        TlsCertificate cert = TlsHelper::generateCertificate("LAN-Drop-Receiver");
        TlsConfig tlsConfig;
        tlsConfig.role = TlsRole::Server;
        tlsConfig.certPem = cert.certPem;
        tlsConfig.keyPem = cert.keyPem;
        session->enableTls(tlsConfig);
    }

    if (!session->listenOn(listenPort)) {
        return reportError("Failed to bind and listen on port " + std::to_string(listenPort));
    }

    if (progressCb) {
        TransferProgressUpdate upd;
        upd.taskId = taskId;
        upd.state = TransferState::Connecting;
        upd.message = "Listening for incoming transfer on port " + std::to_string(listenPort) + "...";
        progressCb(upd);
    }

    // Wait for connection with cancel support
    int listenFd = session->getListenFd();
    while (!cancelFlag || !cancelFlag->load()) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listenFd, &fds);
        timeval tv{0, 200000}; // 200 ms
        int ret = select(listenFd + 1, &fds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(listenFd, &fds)) {
            break;
        }
        if (cancelFlag && cancelFlag->load()) {
            session->closeSession();
            if (progressCb) {
                TransferProgressUpdate upd;
                upd.taskId = taskId;
                upd.state = TransferState::Cancelled;
                upd.message = "Receiver cancelled";
                progressCb(upd);
            }
            return false;
        }
    }

    if (!session->acceptIncoming()) {
        return reportError("Failed to accept incoming connection or TLS handshake failed");
    }

    bool success = handleIncomingConnection(
        std::move(session),
        saveDirectory,
        options,
        nullptr,
        progressCb,
        nullptr,
        cancelFlag
    );

    return success;
}

bool TransferEngine::handleIncomingConnection(
    std::unique_ptr<NetworkSession> session,
    const std::string& saveDir,
    const TransferOptions& options,
    ServerIncomingCallback onIncoming,
    TransferProgressCallback onProgress,
    ServerFinishedCallback onFinished,
    std::atomic<bool>* cancelFlag
) {
    std::string taskId = session->getSessionId();
    if (taskId.empty() || taskId == "rx-client" || taskId == "rx-session") {
        taskId = "RX-" + std::to_string(m_nextSessionId.fetch_add(1));
    }
    std::string senderIp = session->getRemoteIp();

    auto internalCancel = std::make_shared<std::atomic<bool>>(false);
    registerCancelToken(taskId, internalCancel);
    struct CancelScopeCleaner {
        TransferEngine* engine;
        std::string tid;
        ~CancelScopeCleaner() { if (engine) engine->unregisterCancelToken(tid); }
    } cleaner{this, taskId};

    auto isCancelled = [&]() -> bool {
        return (cancelFlag && cancelFlag->load()) || internalCancel->load() || (!m_serverRunning.load() && m_serverPort > 0);
    };

    auto finishWithFailure = [&](const std::string& fileName, const std::string& err) {
        uint8_t nack = 0;
        session->sendAll(&nack, 1);
        session->closeSession();
        if (onProgress) {
            TransferProgressUpdate upd;
            upd.taskId = taskId;
            upd.fileName = fileName;
            upd.state = TransferState::Failed;
            upd.message = err;
            onProgress(upd);
        }
        if (onFinished) {
            onFinished(taskId, fileName, false, "", err);
        }
        return false;
    };

    // 1. Read metadata header
    uint32_t nameLen = 0;
    if (!session->receiveExact(reinterpret_cast<uint8_t*>(&nameLen), sizeof(uint32_t))) {
        return finishWithFailure("", "Failed to receive filename length");
    }

    if (nameLen == 0 || nameLen > 255) {
        return finishWithFailure("", "Invalid filename length: " + std::to_string(nameLen));
    }

    std::string fileName(nameLen, '\0');
    if (!session->receiveExact(reinterpret_cast<uint8_t*>(fileName.data()), nameLen)) {
        return finishWithFailure("", "Failed to receive filename");
    }

    if (!isSafeFileName(fileName)) {
        return finishWithFailure(fileName, "Filename rejected due to safety policy: " + fileName);
    }

    std::filesystem::path cleanName = std::filesystem::path(fileName).filename();
    if (cleanName.empty() || cleanName.string() != fileName) {
        return finishWithFailure(fileName, "Filename contains path components: " + fileName);
    }

    uint64_t fileSize = 0;
    if (!session->receiveExact(reinterpret_cast<uint8_t*>(&fileSize), sizeof(uint64_t))) {
        return finishWithFailure(fileName, "Failed to receive file size");
    }

    if (!isValidFileSize(fileSize) || fileSize > options.maxFileSizeLimit) {
        return finishWithFailure(fileName, "Invalid or excessive file size: " + std::to_string(fileSize));
    }

    std::string expectedHash(64, '\0');
    if (!session->receiveExact(reinterpret_cast<uint8_t*>(expectedHash.data()), 64)) {
        return finishWithFailure(fileName, "Failed to receive expected SHA-256 hash");
    }

    std::filesystem::path canonicalBase = std::filesystem::weakly_canonical(std::filesystem::path(saveDir));
    std::filesystem::path destPath = canonicalBase / cleanName;
    std::filesystem::path canonicalDest = std::filesystem::weakly_canonical(destPath);

    auto rel = std::filesystem::relative(canonicalDest, canonicalBase);
    if (rel.empty() || rel.string().rfind("..", 0) == 0 || canonicalDest == canonicalBase) {
        return finishWithFailure(fileName, "Path traversal attempt detected: " + fileName);
    }

    // Check available disk space
    std::error_code ec;
    auto space = std::filesystem::space(canonicalBase, ec);
    if (!ec && space.available < fileSize) {
        return finishWithFailure(fileName, "Insufficient disk space: available=" + std::to_string(space.available) + ", required=" + std::to_string(fileSize));
    }

    // Check existing file collision
    if (std::filesystem::exists(destPath, ec)) {
        if (options.autoRenameDuplicates) {
            std::string stem = cleanName.stem().string();
            std::string ext = cleanName.extension().string();
            int counter = 1;
            while (counter < 10000) {
                std::filesystem::path candidate = canonicalBase / (stem + " (" + std::to_string(counter) + ")" + ext);
                if (!std::filesystem::exists(candidate, ec)) {
                    destPath = candidate;
                    cleanName = candidate.filename();
                    break;
                }
                counter++;
            }
        } else {
            return finishWithFailure(fileName, "Destination file already exists: " + destPath.string());
        }
    }

    // Check symlink collision
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(destPath, ec))) {
        return finishWithFailure(fileName, "Destination path is a symlink: " + destPath.string());
    }

    // If incoming callback provided, check if application accepts
    if (onIncoming) {
        bool accepted = onIncoming(taskId, fileName, fileSize, senderIp);
        if (!accepted) {
            return finishWithFailure(fileName, "Transfer rejected by local user");
        }
    }

    std::filesystem::path partPath = canonicalBase / (cleanName.string() + ".part");
    std::filesystem::remove(partPath, ec);

    std::ofstream outFile(partPath, std::ios::binary);
    if (!outFile.is_open()) {
        return finishWithFailure(fileName, "Failed to open temporary file: " + partPath.string());
    }

    uint64_t totalReceived = 0;
    auto startTime = std::chrono::steady_clock::now();
    auto lastProgressReport = startTime;
    constexpr uint32_t kMaxChunkSize = 4 * 1024 * 1024;
    std::vector<uint8_t> chunkBuf;

    if (onProgress) {
        TransferProgressUpdate upd;
        upd.taskId = taskId;
        upd.fileName = fileName;
        upd.totalBytes = fileSize;
        upd.bytesTransferred = 0;
        upd.state = TransferState::Transferring;
        upd.message = "Receiving chunks...";
        onProgress(upd);
    }

    while (totalReceived < fileSize) {
        if (isCancelled()) {
            outFile.close();
            std::filesystem::remove(partPath, ec);
            return finishWithFailure(fileName, "Transfer cancelled by receiver");
        }

        uint32_t chunkSize = 0;
        if (!session->receiveExact(reinterpret_cast<uint8_t*>(&chunkSize), sizeof(uint32_t))) {
            outFile.close();
            std::filesystem::remove(partPath, ec);
            return finishWithFailure(fileName, "Sender interrupted connection while reading chunk size");
        }

        if (!isValidChunkSize(chunkSize, fileSize, totalReceived, kMaxChunkSize)) {
            outFile.close();
            std::filesystem::remove(partPath, ec);
            return finishWithFailure(fileName, "Invalid chunk size: " + std::to_string(chunkSize));
        }

        try {
            chunkBuf.resize(chunkSize);
        } catch (...) {
            outFile.close();
            std::filesystem::remove(partPath, ec);
            return finishWithFailure(fileName, "Memory allocation failed for chunk");
        }

        if (!session->receiveExact(chunkBuf.data(), chunkSize)) {
            outFile.close();
            std::filesystem::remove(partPath, ec);
            return finishWithFailure(fileName, "Sender disconnected abruptly while sending chunk payload");
        }

        outFile.write(reinterpret_cast<const char*>(chunkBuf.data()), static_cast<std::streamsize>(chunkSize));
        totalReceived += chunkSize;

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - startTime).count();
        double speedBps = elapsed > 0.05 ? (static_cast<double>(totalReceived) / elapsed) : 0.0;

        if (onProgress && std::chrono::duration<double>(now - lastProgressReport).count() >= 0.1) {
            lastProgressReport = now;
            TransferProgressUpdate upd;
            upd.taskId = taskId;
            upd.fileName = fileName;
            upd.totalBytes = fileSize;
            upd.bytesTransferred = totalReceived;
            upd.progressPercentage = (static_cast<double>(totalReceived) / static_cast<double>(fileSize)) * 100.0;
            upd.speedBytesPerSec = speedBps;
            upd.state = TransferState::Transferring;
            upd.message = "Receiving chunks...";
            onProgress(upd);
        }
    }

    outFile.close();

    // Verify SHA-256 integrity
    FileChunkHasher hasher;
    auto computedHash = hasher.computeFullFileHash(partPath.string());
    if (!computedHash.has_value() || computedHash.value() != expectedHash) {
        std::filesystem::remove(partPath, ec);
        return finishWithFailure(fileName, "Integrity verification failed: SHA-256 mismatch");
    }

    // Atomic rename .part -> destPath
    std::filesystem::rename(partPath, destPath, ec);
    if (ec) {
        std::filesystem::remove(partPath, ec);
        return finishWithFailure(fileName, "Failed to finalize received file: " + ec.message());
    }

    // Send positive ACK
    uint8_t ack = 1;
    session->sendAll(&ack, 1);
    session->closeSession();

    if (onProgress) {
        TransferProgressUpdate upd;
        upd.taskId = taskId;
        upd.fileName = fileName;
        upd.totalBytes = fileSize;
        upd.bytesTransferred = fileSize;
        upd.progressPercentage = 100.0;
        upd.state = TransferState::Completed;
        upd.message = "File received and verified successfully";
        onProgress(upd);
    }

    if (onFinished) {
        onFinished(taskId, fileName, true, destPath.string(), "");
    }

    return true;
}

bool TransferEngine::startServer(
    uint16_t listenPort,
    const std::string& saveDirectory,
    const TransferOptions& options,
    ServerIncomingCallback onIncoming,
    TransferProgressCallback onProgress,
    ServerFinishedCallback onFinished
) {
    stopServer();

    m_serverPort = listenPort;
    {
        std::lock_guard<std::mutex> lock(m_saveDirMutex);
        m_saveDirectory = saveDirectory;
    }
    m_serverOptions = options;

    std::error_code ec;
    std::filesystem::create_directories(saveDirectory, ec);

    m_listenSession = std::make_unique<NetworkSession>("server-listen", "0.0.0.0", listenPort);
    if (options.useTls) {
        TlsCertificate cert = TlsHelper::generateCertificate("LAN-Drop-Receiver");
        TlsConfig tlsConfig;
        tlsConfig.role = TlsRole::Server;
        tlsConfig.certPem = cert.certPem;
        tlsConfig.keyPem = cert.keyPem;
        m_listenSession->enableTls(tlsConfig);
    }

    if (!m_listenSession->listenOn(listenPort)) {
        return false;
    }

    m_serverRunning.store(true);
    m_serverThread = std::make_unique<std::thread>(&TransferEngine::serverLoop, this, onIncoming, onProgress, onFinished);
    return true;
}

void TransferEngine::stopServer() {
    m_serverRunning.store(false);
    if (m_listenSession) {
        m_listenSession->closeSession();
    }
    if (m_serverThread && m_serverThread->joinable()) {
        m_serverThread->join();
    }
    m_serverThread.reset();
    m_listenSession.reset();
}

void TransferEngine::serverLoop(
    ServerIncomingCallback onIncoming,
    TransferProgressCallback onProgress,
    ServerFinishedCallback onFinished
) {
    while (m_serverRunning.load()) {
        if (!m_listenSession) break;
        int listenFd = m_listenSession->getListenFd();
        if (listenFd < 0) break;

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listenFd, &fds);
        timeval tv{0, 250000}; // 250 ms timeout for responsiveness

        int ret = select(listenFd + 1, &fds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(listenFd, &fds)) {
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            int clientFd = accept(listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
            if (clientFd >= 0) {
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
                auto clientSession = std::make_unique<NetworkSession>("rx-client", ipStr, ntohs(clientAddr.sin_port));
                clientSession->attachSocket(clientFd, ConnectionState::Connected);

                if (m_serverOptions.useTls) {
                    TlsCertificate cert = TlsHelper::generateCertificate("LAN-Drop-Receiver");
                    TlsConfig tlsConfig;
                    tlsConfig.role = TlsRole::Server;
                    tlsConfig.certPem = cert.certPem;
                    tlsConfig.keyPem = cert.keyPem;
                    clientSession->enableTls(tlsConfig);
                    if (clientSession->performTlsHandshake() != TlsStatus::Connected) {
                        clientSession->closeSession();
                        continue;
                    }
                }

                // Process transfer on this server loop
                std::string currentSaveDir = getSaveDirectory();
                handleIncomingConnection(
                    std::move(clientSession),
                    currentSaveDir,
                    m_serverOptions,
                    onIncoming,
                    onProgress,
                    onFinished,
                    nullptr
                );
            }
        }
    }
}

} // namespace landrop
