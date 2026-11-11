#include "landrop/core/TransferEngine.hpp"
#include "landrop/core/TlsContext.hpp"
#include "landrop/core/TcpSocketChannel.hpp"
#include "landrop/core/WireProtocolV2.hpp"
#include "landrop/utils/CryptoFallback.hpp"
#include <iostream>
#include <fstream>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <cctype>
#include "landrop/utils/SocketCompat.hpp"

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

    if (progressCb) {
        TransferProgressUpdate upd;
        upd.taskId = taskId;
        upd.state = TransferState::Connecting;
        upd.message = "Connecting to " + remoteIp + ":" + std::to_string(remotePort) + "...";
        progressCb(upd);
    }

    auto session = std::make_unique<NetworkSession>("tx-" + taskId, remoteIp, remotePort);
    if (options.useTls) {
        TlsConfig tlsConfig;
        tlsConfig.role = TlsRole::Client;
        tlsConfig.verifyMode = TlsVerificationMode::VerifyPinnedFingerprint;
        tlsConfig.expectedFingerprint = options.expectedFingerprint;
        session->enableTls(tlsConfig);
    }

    if (!session->connectTo(remoteIp, remotePort)) {
        if (options.useTls && session->getTlsStatus() != TlsStatus::Connected && session->getTlsStatus() != TlsStatus::None) {
            return reportError(std::string("TLS connection failed: ") + TlsHelper::statusToString(session->getTlsStatus()));
        }
        return reportError("Connection to " + remoteIp + ":" + std::to_string(remotePort) + " failed");
    }

    auto channel = std::make_shared<TcpSocketChannel>(std::move(session));
    return sendFileOverChannel(taskId, filePath, channel, options, progressCb, cancelFlag, outError);
}

std::vector<BatchFileItem> TransferEngine::scanDirectory(const std::string& directoryPath, std::string* outError) {
    std::vector<BatchFileItem> items;
    std::error_code ec;
    std::filesystem::path root(directoryPath);

    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        if (outError) *outError = "Path does not exist or is not a directory: " + directoryPath;
        return items;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            if (outError) *outError = "Directory scan error: " + ec.message();
            return {};
        }

        if (entry.is_symlink(ec)) {
            continue;
        }

        if (entry.is_regular_file(ec)) {
            auto rel = std::filesystem::relative(entry.path(), root, ec).generic_string();
            if (ec || !isSafeRelativePath(rel)) {
                if (outError) *outError = "Encountered unsafe relative path in directory: " + rel;
                return {};
            }

            uint64_t sz = entry.file_size(ec);
            if (ec) {
                if (outError) *outError = "Failed to determine file size: " + entry.path().string();
                return {};
            }

            BatchFileItem item;
            item.localFilePath = entry.path().string();
            item.relativePath = rel;
            item.fileSize = sz;
            items.push_back(std::move(item));
        }
    }

    if (items.empty()) {
        if (outError) *outError = "Directory contains no regular files: " + directoryPath;
    }
    return items;
}

bool TransferEngine::sendBatch(
    const std::string& taskId,
    const std::vector<BatchFileItem>& items,
    const std::string& batchName,
    const std::string& remoteIp,
    uint16_t remotePort,
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
            upd.totalFiles = static_cast<uint32_t>(items.size());
            upd.currentFileIndex = 1;
            progressCb(upd);
        }
        return false;
    };

    if (progressCb) {
        TransferProgressUpdate upd;
        upd.taskId = taskId;
        upd.state = TransferState::Connecting;
        upd.message = "Connecting to " + remoteIp + ":" + std::to_string(remotePort) + "...";
        upd.totalFiles = static_cast<uint32_t>(items.size());
        upd.currentFileIndex = 1;
        progressCb(upd);
    }

    auto session = std::make_unique<NetworkSession>("tx-" + taskId, remoteIp, remotePort);
    if (options.useTls) {
        TlsConfig tlsConfig;
        tlsConfig.role = TlsRole::Client;
        tlsConfig.verifyMode = TlsVerificationMode::VerifyPinnedFingerprint;
        tlsConfig.expectedFingerprint = options.expectedFingerprint;
        session->enableTls(tlsConfig);
    }

    if (!session->connectTo(remoteIp, remotePort)) {
        if (options.useTls && session->getTlsStatus() != TlsStatus::Connected && session->getTlsStatus() != TlsStatus::None) {
            return reportError(std::string("TLS connection failed: ") + TlsHelper::statusToString(session->getTlsStatus()));
        }
        return reportError("Connection to " + remoteIp + ":" + std::to_string(remotePort) + " failed");
    }

    auto channel = std::make_shared<TcpSocketChannel>(std::move(session));
    return sendBatchOverChannel(taskId, items, batchName, channel, options, progressCb, cancelFlag, outError);
}

bool TransferEngine::sendFileOverChannel(
    const std::string& taskId,
    const std::string& filePath,
    std::shared_ptr<ITransportChannel> channel,
    const TransferOptions& options,
    TransferProgressCallback progressCb,
    std::atomic<bool>* cancelFlag,
    std::string* outError
) {
    std::shared_ptr<std::atomic<bool>> effectiveCancel;
    if (cancelFlag) {
        effectiveCancel = std::shared_ptr<std::atomic<bool>>(cancelFlag, [](std::atomic<bool>*){});
    } else {
        effectiveCancel = std::make_shared<std::atomic<bool>>(false);
    }
    registerCancelToken(taskId, effectiveCancel);
    struct CancelScopeCleaner {
        TransferEngine* engine;
        std::string tid;
        ~CancelScopeCleaner() { if (engine) engine->unregisterCancelToken(tid); }
    } cleaner{this, taskId};

    auto isCancelled = [&]() -> bool {
        return effectiveCancel->load();
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

    if (!channel || !channel->isConnected()) {
        return reportError("Transport channel is null or not connected");
    }

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

    // Prepare wire protocol metadata header (WireProtocol v2)
    protocol::v2::FileHeaderMessage fhdr;
    fhdr.fileSize = fileSize;
    size_t chunkCapacity = options.chunkSize > 0 ? options.chunkSize : (64 * 1024);
    if (chunkCapacity > 4 * 1024 * 1024) chunkCapacity = 4 * 1024 * 1024;
    fhdr.chunkSize = static_cast<uint32_t>(chunkCapacity);
    fhdr.flags = options.enableResume ? protocol::v2::FileHeaderFlags::RESUME_SUPPORTED : protocol::v2::FileHeaderFlags::NONE;
    fhdr.fileName = fileName;
    fhdr.sha256Hex = fileHash;
    auto headerFrame = protocol::v2::buildFrame(protocol::v2::MessageType::FileHeader, fhdr.serialize());

    if (!channel->sendExact(headerFrame.data(), headerFrame.size())) {
        return reportError("Failed to send metadata header to receiver");
    }

    std::ifstream in(filePath, std::ios::binary);
    if (!in.is_open()) {
        return reportError("Failed to open local file for reading: " + filePath);
    }

    std::vector<uint8_t> chunkBuf(chunkCapacity);
    uint32_t chunkIndex = 0;
    uint64_t sentBytes = 0;

    // Resilient resumption negotiation: check if receiver sent a ResumeRequestMessage
    if (options.enableResume && channel->waitForReadable(options.resumeTimeoutMs, effectiveCancel.get())) {
        uint8_t maybeResumeByte = 0;
        if (channel->receiveExact(&maybeResumeByte, 1, 1000, effectiveCancel.get())) {
        if (maybeResumeByte == 'L') {
            uint8_t resumeHdrRest[15];
            if (channel->receiveExact(resumeHdrRest, 15, 5000, effectiveCancel.get())) {
                uint8_t fullResumeHdr[16];
                fullResumeHdr[0] = 'L';
                std::memcpy(fullResumeHdr + 1, resumeHdrRest, 15);
                protocol::v2::FrameHeader resumeFhdr;
                std::string resumeFhdrErr;
                if (protocol::v2::FrameHeader::deserialize(fullResumeHdr, resumeFhdr, &resumeFhdrErr)) {
                    if (resumeFhdr.msgType == protocol::v2::MessageType::ResumeRequest && resumeFhdr.payloadLength == 136) {
                        std::vector<uint8_t> resumePayload(136);
                        if (channel->receiveExact(resumePayload.data(), 136, 5000, effectiveCancel.get())) {
                            protocol::v2::ResumeRequestMessage req;
                            std::string reqErr;
                            if (protocol::v2::ResumeRequestMessage::deserialize(resumePayload.data(), resumePayload.size(), req, &reqErr)) {
                                bool resumeValid = false;
                                if (req.fileHash == fileHash &&
                                    req.bytesReceivedSoFar > 0 &&
                                    req.bytesReceivedSoFar < fileSize &&
                                    (req.bytesReceivedSoFar % chunkCapacity == 0)) {
                                    auto computedPartial = hasher.computePartialFileHash(filePath, req.bytesReceivedSoFar);
                                    if (computedPartial.has_value() && computedPartial.value() == req.partialSha256) {
                                        resumeValid = true;
                                    }
                                }

                                protocol::v2::TransferAckMessage ack;
                                if (resumeValid) {
                                    ack.status = protocol::v2::AckStatus::ResumeAccepted;
                                    ack.receivedBytes = req.bytesReceivedSoFar;
                                    ack.message = "Resume accepted at byte offset " + std::to_string(req.bytesReceivedSoFar);
                                    auto ackFrame = protocol::v2::buildFrame(protocol::v2::MessageType::TransferAck, ack.serialize());
                                    channel->sendExact(ackFrame.data(), ackFrame.size());

                                    in.seekg(static_cast<std::streamoff>(req.bytesReceivedSoFar), std::ios::beg);
                                    sentBytes = req.bytesReceivedSoFar;
                                    chunkIndex = static_cast<uint32_t>(sentBytes / chunkCapacity);
                                } else {
                                    ack.status = protocol::v2::AckStatus::ResumeRejected;
                                    ack.receivedBytes = 0;
                                    ack.message = "Resume rejected: offset or hash mismatch";
                                    auto ackFrame = protocol::v2::buildFrame(protocol::v2::MessageType::TransferAck, ack.serialize());
                                    channel->sendExact(ackFrame.data(), ackFrame.size());

                                    in.seekg(0, std::ios::beg);
                                    sentBytes = 0;
                                    chunkIndex = 0;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

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
            protocol::v2::TransferCancelMessage cmsg;
            cmsg.errorCode = 2;
            cmsg.reason = "Transfer cancelled by user";
            auto cancelFrame = protocol::v2::buildFrame(protocol::v2::MessageType::TransferCancel, cmsg.serialize());
            channel->sendExact(cancelFrame.data(), cancelFrame.size());
            channel->close();
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

        protocol::v2::ChunkDataMessage cmsg;
        cmsg.offset = sentBytes;
        cmsg.chunkIndex = chunkIndex++;
        cmsg.chunkCrc32 = protocol::v2::computeCrc32(chunkBuf.data(), count);
        cmsg.data.assign(chunkBuf.data(), chunkBuf.data() + count);

        auto chunkFrame = protocol::v2::buildFrame(protocol::v2::MessageType::ChunkData, cmsg.serialize());
        if (!channel->sendExact(chunkFrame.data(), chunkFrame.size())) {
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
        upd.message = "Waiting for receiver verification ACK...";
        progressCb(upd);
    }

    // Wait for receiver ACK (supports both WireProtocolV2 TransferAckMessage and legacy 1-byte ACK)
    uint8_t firstByte = 0;
    if (!channel->receiveExact(&firstByte, 1, 30000, effectiveCancel.get())) {
        return reportError("Connection dropped while waiting for receiver verification ACK");
    }

    if (firstByte == 'L') {
        uint8_t restHdr[15];
        if (!channel->receiveExact(restHdr, 15, 10000, effectiveCancel.get())) {
            return reportError("Truncated FrameHeader from receiver ACK");
        }
        uint8_t fullHdr[16];
        fullHdr[0] = 'L';
        std::memcpy(fullHdr + 1, restHdr, 15);
        protocol::v2::FrameHeader ackFhdr;
        std::string hdrErr;
        if (!protocol::v2::FrameHeader::deserialize(fullHdr, ackFhdr, &hdrErr)) {
            return reportError("Failed to parse ACK FrameHeader: " + hdrErr);
        }
        std::vector<uint8_t> payload(ackFhdr.payloadLength);
        if (ackFhdr.payloadLength > 0) {
            if (!channel->receiveExact(payload.data(), ackFhdr.payloadLength, 10000, effectiveCancel.get())) {
                return reportError("Truncated ACK payload");
            }
        }
        if (ackFhdr.msgType == protocol::v2::MessageType::TransferAck) {
            protocol::v2::TransferAckMessage ackMsg;
            std::string ackErr;
            if (!protocol::v2::TransferAckMessage::deserialize(payload.data(), payload.size(), ackMsg, &ackErr)) {
                return reportError("Failed to parse TransferAckMessage: " + ackErr);
            }
            if (ackMsg.status != protocol::v2::AckStatus::Success) {
                return reportError("Receiver rejected transfer: status=" + std::to_string(static_cast<int>(ackMsg.status)) + " message=" + ackMsg.message);
            }
        } else if (ackFhdr.msgType == protocol::v2::MessageType::TransferCancel) {
            return reportError("Receiver cancelled transfer");
        } else {
            return reportError("Unexpected frame type awaiting ACK: " + std::to_string(static_cast<int>(ackFhdr.msgType)));
        }
    } else if (firstByte == 1) {
        // Legacy v1 positive ACK
    } else {
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

bool TransferEngine::sendBatchOverChannel(
    const std::string& taskId,
    const std::vector<BatchFileItem>& items,
    const std::string& batchName,
    std::shared_ptr<ITransportChannel> channel,
    const TransferOptions& options,
    TransferProgressCallback progressCb,
    std::atomic<bool>* cancelFlag,
    std::string* outError
) {
    std::shared_ptr<std::atomic<bool>> effectiveCancel;
    if (cancelFlag) {
        effectiveCancel = std::shared_ptr<std::atomic<bool>>(cancelFlag, [](std::atomic<bool>*){});
    } else {
        effectiveCancel = std::make_shared<std::atomic<bool>>(false);
    }
    registerCancelToken(taskId, effectiveCancel);
    struct CancelScopeCleaner {
        TransferEngine* engine;
        std::string tid;
        ~CancelScopeCleaner() { if (engine) engine->unregisterCancelToken(tid); }
    } cleaner{this, taskId};

    auto isCancelled = [&]() -> bool {
        return effectiveCancel->load();
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

    if (!channel || !channel->isConnected()) {
        return reportError("Transport channel is null or not connected");
    }

    if (items.empty()) {
        return reportError("Batch file list is empty");
    }

    std::string effectiveBatchName = batchName.empty() ? ("batch-" + taskId) : batchName;

    // Validate all items exist and compute hashes
    std::vector<BatchFileItem> preparedItems = items;
    uint64_t totalBatchBytes = 0;
    FileChunkHasher hasher;

    for (size_t i = 0; i < preparedItems.size(); ++i) {
        auto& item = preparedItems[i];
        if (!isSafeRelativePath(item.relativePath)) {
            return reportError("Unsafe relative path in batch: " + item.relativePath);
        }
        std::error_code ec;
        std::filesystem::path p(item.localFilePath);
        if (!std::filesystem::exists(p, ec) || !std::filesystem::is_regular_file(p, ec)) {
            return reportError("File does not exist or is not regular: " + item.localFilePath);
        }
        uint64_t sz = std::filesystem::file_size(p, ec);
        if (ec) {
            return reportError("Cannot determine file size: " + item.localFilePath);
        }
        item.fileSize = sz;
        totalBatchBytes += sz;

        if (item.sha256Hex.empty()) {
            if (isCancelled()) {
                return reportError("Batch transfer cancelled before hashing");
            }
            if (progressCb) {
                TransferProgressUpdate upd;
                upd.taskId = taskId;
                upd.fileName = item.relativePath;
                upd.totalBytes = totalBatchBytes;
                upd.bytesTransferred = 0;
                upd.currentFileIndex = static_cast<uint32_t>(i + 1);
                upd.totalFiles = static_cast<uint32_t>(preparedItems.size());
                upd.state = TransferState::Connecting;
                upd.message = "Hashing file " + std::to_string(i + 1) + " of " + std::to_string(preparedItems.size()) + ": " + item.relativePath;
                progressCb(upd);
            }
            auto hOpt = hasher.computeFullFileHash(item.localFilePath);
            if (!hOpt.has_value()) {
                return reportError("Failed to hash file: " + item.localFilePath);
            }
            item.sha256Hex = hOpt.value();
        }
    }

    if (totalBatchBytes > options.maxFileSizeLimit) {
        return reportError("Total batch size exceeds maximum limit: " + std::to_string(totalBatchBytes) + " > " + std::to_string(options.maxFileSizeLimit));
    }

    // Prepare and send BatchManifest
    protocol::v2::BatchManifestMessage manifest;
    manifest.totalFiles = static_cast<uint32_t>(preparedItems.size());
    manifest.totalBytes = totalBatchBytes;
    manifest.batchId = effectiveBatchName;
    for (size_t i = 0; i < preparedItems.size(); ++i) {
        protocol::v2::BatchFileEntry entry;
        entry.fileIndex = static_cast<uint32_t>(i);
        entry.fileSize = preparedItems[i].fileSize;
        entry.relativePath = preparedItems[i].relativePath;
        entry.sha256Hex = preparedItems[i].sha256Hex;
        manifest.files.push_back(std::move(entry));
    }

    auto manifestFrame = protocol::v2::buildFrame(protocol::v2::MessageType::BatchManifest, manifest.serialize());
    if (!channel->sendExact(manifestFrame.data(), manifestFrame.size())) {
        return reportError("Failed to send BatchManifest frame to receiver");
    }

    // Wait for receiver manifest acceptance ACK
    uint8_t ackHdrBytes[16];
    if (!channel->receiveExact(ackHdrBytes, 16, 30000, effectiveCancel.get())) {
        return reportError("Connection timed out waiting for batch manifest acceptance ACK");
    }
    protocol::v2::FrameHeader ackFhdr;
    std::string ackFhdrErr;
    if (!protocol::v2::FrameHeader::deserialize(ackHdrBytes, ackFhdr, &ackFhdrErr)) {
        return reportError("Failed to deserialize batch manifest ACK FrameHeader: " + ackFhdrErr);
    }
    if (ackFhdr.msgType == protocol::v2::MessageType::TransferCancel) {
        return reportError("Batch transfer rejected or cancelled by receiver");
    }
    if (ackFhdr.msgType != protocol::v2::MessageType::TransferAck) {
        return reportError("Expected TransferAck for BatchManifest, got message type: " + std::to_string(static_cast<int>(ackFhdr.msgType)));
    }
    std::vector<uint8_t> ackPayload(ackFhdr.payloadLength);
    if (ackFhdr.payloadLength > 0) {
        if (!channel->receiveExact(ackPayload.data(), ackFhdr.payloadLength, 10000, effectiveCancel.get())) {
            return reportError("Truncated batch manifest ACK payload");
        }
    }
    protocol::v2::TransferAckMessage manifestAck;
    std::string ackErr;
    if (!protocol::v2::TransferAckMessage::deserialize(ackPayload.data(), ackPayload.size(), manifestAck, &ackErr)) {
        return reportError("Failed to parse batch manifest TransferAckMessage: " + ackErr);
    }
    if (manifestAck.status != protocol::v2::AckStatus::Success) {
        return reportError("Receiver rejected batch: " + manifestAck.message);
    }

    // Stream each file in the batch
    size_t chunkCapacity = options.chunkSize > 0 ? options.chunkSize : (64 * 1024);
    if (chunkCapacity > 4 * 1024 * 1024) chunkCapacity = 4 * 1024 * 1024;
    std::vector<uint8_t> chunkBuf(chunkCapacity);

    uint64_t sentBatchBytes = 0;
    auto batchStartTime = std::chrono::steady_clock::now();
    auto lastProgressReport = batchStartTime;

    for (size_t i = 0; i < preparedItems.size(); ++i) {
        const auto& item = preparedItems[i];
        if (isCancelled()) {
            protocol::v2::TransferCancelMessage cmsg;
            cmsg.errorCode = 2;
            cmsg.reason = "Batch transfer cancelled by user";
            auto cancelFrame = protocol::v2::buildFrame(protocol::v2::MessageType::TransferCancel, cmsg.serialize());
            channel->sendExact(cancelFrame.data(), cancelFrame.size());
            channel->close();
            return reportError("Batch transfer cancelled by user");
        }

        // 1. Send FileHeader for this file
        protocol::v2::FileHeaderMessage fhdr;
        fhdr.fileSize = item.fileSize;
        fhdr.chunkSize = static_cast<uint32_t>(chunkCapacity);
        fhdr.flags = 0;
        fhdr.fileName = item.relativePath;
        fhdr.sha256Hex = item.sha256Hex;
        auto headerFrame = protocol::v2::buildFrame(protocol::v2::MessageType::FileHeader, fhdr.serialize());
        if (!channel->sendExact(headerFrame.data(), headerFrame.size())) {
            return reportError("Failed to send FileHeader for: " + item.relativePath);
        }

        std::ifstream in(item.localFilePath, std::ios::binary);
        if (!in.is_open()) {
            return reportError("Failed to open local file: " + item.localFilePath);
        }

        uint64_t sentFileBytes = 0;
        uint32_t chunkIndex = 0;

        while (sentFileBytes < item.fileSize && in) {
            if (isCancelled()) {
                protocol::v2::TransferCancelMessage cmsg;
                cmsg.errorCode = 2;
                cmsg.reason = "Batch transfer cancelled by user";
                auto cancelFrame = protocol::v2::buildFrame(protocol::v2::MessageType::TransferCancel, cmsg.serialize());
                channel->sendExact(cancelFrame.data(), cancelFrame.size());
                channel->close();
                return reportError("Batch transfer cancelled by user");
            }

            in.read(reinterpret_cast<char*>(chunkBuf.data()), static_cast<std::streamsize>(chunkBuf.size()));
            size_t count = static_cast<size_t>(in.gcount());
            if (count == 0) break;

            protocol::v2::ChunkDataMessage cmsg;
            cmsg.offset = sentFileBytes;
            cmsg.chunkIndex = chunkIndex++;
            cmsg.chunkCrc32 = protocol::v2::computeCrc32(chunkBuf.data(), count);
            cmsg.data.assign(chunkBuf.data(), chunkBuf.data() + count);

            auto chunkFrame = protocol::v2::buildFrame(protocol::v2::MessageType::ChunkData, cmsg.serialize());
            if (!channel->sendExact(chunkFrame.data(), chunkFrame.size())) {
                return reportError("Connection interrupted while sending chunk for: " + item.relativePath);
            }

            sentFileBytes += count;
            sentBatchBytes += count;

            auto now = std::chrono::steady_clock::now();
            double elapsedTotal = std::chrono::duration<double>(now - batchStartTime).count();
            double speedBps = elapsedTotal > 0.05 ? (static_cast<double>(sentBatchBytes) / elapsedTotal) : 0.0;

            if (options.speedLimitKbps > 0) {
                double maxSpeedBps = static_cast<double>(options.speedLimitKbps) * 1024.0;
                if (speedBps > maxSpeedBps && maxSpeedBps > 0.0) {
                    double expectedSeconds = static_cast<double>(sentBatchBytes) / maxSpeedBps;
                    double delay = expectedSeconds - elapsedTotal;
                    if (delay > 0.001) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(delay * 1000.0)));
                    }
                }
            }

            if (progressCb && std::chrono::duration<double>(now - lastProgressReport).count() >= 0.1) {
                lastProgressReport = now;
                TransferProgressUpdate upd;
                upd.taskId = taskId;
                upd.fileName = item.relativePath;
                upd.totalBytes = totalBatchBytes;
                upd.bytesTransferred = sentBatchBytes;
                upd.progressPercentage = totalBatchBytes > 0 ? ((static_cast<double>(sentBatchBytes) / static_cast<double>(totalBatchBytes)) * 100.0) : 100.0;
                upd.speedBytesPerSec = speedBps;
                upd.state = TransferState::Transferring;
                upd.message = "Streaming file " + std::to_string(i + 1) + " of " + std::to_string(preparedItems.size()) + ": " + item.relativePath;
                upd.currentFileIndex = static_cast<uint32_t>(i + 1);
                upd.totalFiles = static_cast<uint32_t>(preparedItems.size());
                progressCb(upd);
            }
        }
        in.close();

        if (sentFileBytes < item.fileSize) {
            return reportError("File read ended prematurely for: " + item.relativePath);
        }

        // Wait for receiver ACK for this file
        uint8_t fileAckHdrBytes[16];
        if (!channel->receiveExact(fileAckHdrBytes, 16, 30000, effectiveCancel.get())) {
            return reportError("Connection dropped waiting for file verification ACK: " + item.relativePath);
        }
        protocol::v2::FrameHeader fileAckFhdr;
        std::string fileAckFhdrErr;
        if (!protocol::v2::FrameHeader::deserialize(fileAckHdrBytes, fileAckFhdr, &fileAckFhdrErr)) {
            return reportError("Failed to deserialize file ACK FrameHeader: " + fileAckFhdrErr);
        }
        std::vector<uint8_t> fileAckPayload(fileAckFhdr.payloadLength);
        if (fileAckFhdr.payloadLength > 0) {
            if (!channel->receiveExact(fileAckPayload.data(), fileAckFhdr.payloadLength, 10000, effectiveCancel.get())) {
                return reportError("Truncated file ACK payload for: " + item.relativePath);
            }
        }
        protocol::v2::TransferAckMessage fileAckMsg;
        std::string fileAckErr;
        if (!protocol::v2::TransferAckMessage::deserialize(fileAckPayload.data(), fileAckPayload.size(), fileAckMsg, &fileAckErr)) {
            return reportError("Failed to parse file TransferAckMessage: " + fileAckErr);
        }
        if (fileAckMsg.status != protocol::v2::AckStatus::Success) {
            return reportError("Receiver rejected file " + item.relativePath + ": " + fileAckMsg.message);
        }
    }

    if (options.closeChannelOnCompletion) {
        channel->close();
    }

    if (progressCb) {
        TransferProgressUpdate upd;
        upd.taskId = taskId;
        upd.fileName = effectiveBatchName;
        upd.totalBytes = totalBatchBytes;
        upd.bytesTransferred = sentBatchBytes;
        upd.progressPercentage = 100.0;
        upd.state = TransferState::Completed;
        upd.message = "Batch transfer verified and completed successfully!";
        upd.currentFileIndex = static_cast<uint32_t>(preparedItems.size());
        upd.totalFiles = static_cast<uint32_t>(preparedItems.size());
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
    socket_handle_t listenFd = session->getListenFd();
    while (!cancelFlag || !cancelFlag->load()) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listenFd, &fds);
        timeval tv{0, 200000}; // 200 ms
        int ret = select(static_cast<int>(listenFd + 1), &fds, nullptr, nullptr, &tv);
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
    if (!session) return false;
    std::string taskId = session->getSessionId();
    if (taskId.empty() || taskId == "rx-client" || taskId == "rx-session") {
        taskId = "RX-" + std::to_string(m_nextSessionId.fetch_add(1));
    }
    auto channel = std::make_shared<TcpSocketChannel>(std::move(session));
    return receiveFileOverChannel(
        taskId,
        saveDir,
        channel,
        options,
        onProgress,
        cancelFlag,
        nullptr,
        onIncoming,
        onFinished
    );
}

bool TransferEngine::receiveFileOverChannel(
    const std::string& taskId,
    const std::string& saveDirectory,
    std::shared_ptr<ITransportChannel> channel,
    const TransferOptions& options,
    TransferProgressCallback progressCb,
    std::atomic<bool>* cancelFlag,
    std::string* outError,
    ServerIncomingCallback onIncoming,
    ServerFinishedCallback onFinished
) {
    std::shared_ptr<std::atomic<bool>> effectiveCancel;
    if (cancelFlag) {
        effectiveCancel = std::shared_ptr<std::atomic<bool>>(cancelFlag, [](std::atomic<bool>*){});
    } else {
        effectiveCancel = std::make_shared<std::atomic<bool>>(false);
    }
    registerCancelToken(taskId, effectiveCancel);
    struct CancelScopeCleaner {
        TransferEngine* engine;
        std::string tid;
        ~CancelScopeCleaner() { if (engine) engine->unregisterCancelToken(tid); }
    } cleaner{this, taskId};

    auto isCancelled = [&]() -> bool {
        return effectiveCancel->load() || (!m_serverRunning.load() && m_serverPort > 0);
    };

    bool isV2 = false;

    auto finishWithFailure = [&](const std::string& fileName, const std::string& err) {
        if (channel && channel->isConnected()) {
            if (isV2) {
                protocol::v2::TransferCancelMessage cmsg;
                cmsg.errorCode = 1;
                cmsg.reason = err;
                auto cancelFrame = protocol::v2::buildFrame(protocol::v2::MessageType::TransferCancel, cmsg.serialize());
                channel->sendExact(cancelFrame.data(), cancelFrame.size());
            } else {
                uint8_t nack = 0;
                channel->sendExact(&nack, 1);
            }
            if (options.closeChannelOnCompletion) {
                channel->close();
            }
        }
        if (outError) *outError = err;
        if (progressCb) {
            TransferProgressUpdate upd;
            upd.taskId = taskId;
            upd.fileName = fileName;
            upd.state = TransferState::Failed;
            upd.message = err;
            progressCb(upd);
        }
        if (onFinished) {
            onFinished(taskId, fileName, false, "", err);
        }
        return false;
    };

    if (!channel || !channel->isConnected()) {
        return finishWithFailure("", "Transport channel is null or not connected");
    }

    std::string senderAddr = channel->getRemoteAddress();

    // 1. Read metadata header prefix to determine protocol version (WireProtocol v2 vs legacy v1)
    uint8_t first4[4];
    int headerTimeout = (options.headerTimeoutMs > 0) ? static_cast<int>(options.headerTimeoutMs) : -1;
    if (!channel->receiveExact(first4, 4, headerTimeout, effectiveCancel.get())) {
        return finishWithFailure("", "Failed to receive metadata header prefix");
    }

    isV2 = protocol::v2::isProtocolV2(first4);

    std::string fileName;
    uint64_t fileSize = 0;
    std::string expectedHash;
    protocol::v2::FileHeaderMessage fileHeaderMsg;

    if (isV2) {
        uint8_t header16[16];
        std::memcpy(header16, first4, 4);
        if (!channel->receiveExact(header16 + 4, 12, 10000, effectiveCancel.get())) {
            return finishWithFailure("", "Failed to receive WireProtocol v2 frame header");
        }
        protocol::v2::FrameHeader fhdr;
        std::string hdrErr;
        if (!protocol::v2::FrameHeader::deserialize(header16, fhdr, &hdrErr)) {
            return finishWithFailure("", "Invalid WireProtocol v2 frame header: " + hdrErr);
        }
        if (fhdr.msgType == protocol::v2::MessageType::BatchManifest) {
            std::vector<uint8_t> payload(fhdr.payloadLength);
            if (fhdr.payloadLength > 0) {
                if (!channel->receiveExact(payload.data(), fhdr.payloadLength, 10000, effectiveCancel.get())) {
                    return finishWithFailure("", "Failed to receive BatchManifest payload");
                }
            }
            if (fhdr.crc32OrSeq != 0) {
                uint32_t actualCrc = protocol::v2::computeCrc32(payload.data(), payload.size());
                if (actualCrc != fhdr.crc32OrSeq) {
                    return finishWithFailure("", "BatchManifest CRC-32 checksum mismatch");
                }
            }
            protocol::v2::BatchManifestMessage manifest;
            std::string manifestErr;
            if (!protocol::v2::BatchManifestMessage::deserialize(payload.data(), payload.size(), manifest, &manifestErr)) {
                return finishWithFailure("", "Failed to parse BatchManifestMessage: " + manifestErr);
            }
            if (manifest.totalFiles == 0 || manifest.files.size() != manifest.totalFiles) {
                return finishWithFailure("", "BatchManifest contains invalid file count: " + std::to_string(manifest.totalFiles));
            }

            // Path safety check for all files in manifest
            for (const auto& entry : manifest.files) {
                if (!isSafeRelativePath(entry.relativePath)) {
                    protocol::v2::TransferAckMessage rejAck;
                    rejAck.status = protocol::v2::AckStatus::PathUnsafe;
                    rejAck.message = "Batch rejected due to unsafe relative path: " + entry.relativePath;
                    auto rejFrame = protocol::v2::buildFrame(protocol::v2::MessageType::TransferAck, rejAck.serialize());
                    channel->sendExact(rejFrame.data(), rejFrame.size());
                    return finishWithFailure(manifest.batchId, rejAck.message);
                }
            }

            std::filesystem::path canonicalBase = std::filesystem::weakly_canonical(std::filesystem::path(saveDirectory));

            // Check available disk space
            std::error_code ec;
            auto space = std::filesystem::space(canonicalBase, ec);
            if (!ec && space.available < manifest.totalBytes) {
                return finishWithFailure(manifest.batchId, "Insufficient disk space for batch: available=" + std::to_string(space.available) + ", required=" + std::to_string(manifest.totalBytes));
            }

            // User confirmation if callback provided
            if (onIncoming) {
                std::string displayName = manifest.batchId.empty() ? ("Batch (" + std::to_string(manifest.totalFiles) + " files)") : manifest.batchId;
                bool accepted = onIncoming(taskId, displayName, manifest.totalBytes, senderAddr);
                if (!accepted) {
                    protocol::v2::TransferAckMessage rejAck;
                    rejAck.status = protocol::v2::AckStatus::Rejected;
                    rejAck.message = "Batch transfer rejected by local user";
                    auto rejFrame = protocol::v2::buildFrame(protocol::v2::MessageType::TransferAck, rejAck.serialize());
                    channel->sendExact(rejFrame.data(), rejFrame.size());
                    return finishWithFailure(manifest.batchId, "Batch transfer rejected by local user");
                }
            }

            // Send acceptance ACK for manifest
            protocol::v2::TransferAckMessage manifestAck;
            manifestAck.status = protocol::v2::AckStatus::Success;
            manifestAck.receivedBytes = 0;
            manifestAck.message = "Batch manifest accepted";
            auto ackFrame = protocol::v2::buildFrame(protocol::v2::MessageType::TransferAck, manifestAck.serialize());
            if (!channel->sendExact(ackFrame.data(), ackFrame.size())) {
                return finishWithFailure(manifest.batchId, "Failed to send batch manifest acceptance ACK");
            }

            // Receive each file in the batch
            uint64_t totalBatchReceived = 0;
            auto batchStartTime = std::chrono::steady_clock::now();
            auto lastProgressReport = batchStartTime;

            for (uint32_t fileIdx = 0; fileIdx < manifest.totalFiles; ++fileIdx) {
                if (isCancelled()) {
                    return finishWithFailure(manifest.batchId, "Transfer cancelled by receiver");
                }

                // 1. Receive FileHeader for file fileIdx
                uint8_t fileHdrBytes[16];
                if (!channel->receiveExact(fileHdrBytes, 16, 15000, effectiveCancel.get())) {
                    return finishWithFailure(manifest.batchId, "Interrupted while reading FileHeader for file index " + std::to_string(fileIdx));
                }
                protocol::v2::FrameHeader fileFhdr;
                std::string fileFhdrErr;
                if (!protocol::v2::FrameHeader::deserialize(fileHdrBytes, fileFhdr, &fileFhdrErr)) {
                    return finishWithFailure(manifest.batchId, "Invalid FileHeader frame for file index " + std::to_string(fileIdx) + ": " + fileFhdrErr);
                }
                if (fileFhdr.msgType == protocol::v2::MessageType::TransferCancel) {
                    return finishWithFailure(manifest.batchId, "Transfer cancelled by sender");
                }
                if (fileFhdr.msgType != protocol::v2::MessageType::FileHeader) {
                    return finishWithFailure(manifest.batchId, "Expected FileHeader for file index " + std::to_string(fileIdx) + ", got " + std::to_string(static_cast<int>(fileFhdr.msgType)));
                }

                std::vector<uint8_t> filePayload(fileFhdr.payloadLength);
                if (fileFhdr.payloadLength > 0) {
                    if (!channel->receiveExact(filePayload.data(), fileFhdr.payloadLength, 15000, effectiveCancel.get())) {
                        return finishWithFailure(manifest.batchId, "Failed to receive FileHeader payload for file index " + std::to_string(fileIdx));
                    }
                }
                if (fileFhdr.crc32OrSeq != 0) {
                    uint32_t actualCrc = protocol::v2::computeCrc32(filePayload.data(), filePayload.size());
                    if (actualCrc != fileFhdr.crc32OrSeq) {
                        return finishWithFailure(manifest.batchId, "FileHeader CRC mismatch for file index " + std::to_string(fileIdx));
                    }
                }
                protocol::v2::FileHeaderMessage currentFhdr;
                std::string currentFhdrErr;
                if (!protocol::v2::FileHeaderMessage::deserialize(filePayload.data(), filePayload.size(), currentFhdr, &currentFhdrErr)) {
                    return finishWithFailure(manifest.batchId, "Failed to parse FileHeaderMessage for file index " + std::to_string(fileIdx) + ": " + currentFhdrErr);
                }

                if (currentFhdr.fileName != manifest.files[fileIdx].relativePath) {
                    return finishWithFailure(manifest.batchId, "Manifest mismatch: expected " + manifest.files[fileIdx].relativePath + ", got " + currentFhdr.fileName);
                }

                std::filesystem::path destPath = canonicalBase / std::filesystem::path(currentFhdr.fileName);
                std::filesystem::path canonicalDest = std::filesystem::weakly_canonical(destPath);
                auto rel = std::filesystem::relative(canonicalDest, canonicalBase);
                if (rel.empty() || rel.string().rfind("..", 0) == 0 || canonicalDest == canonicalBase) {
                    return finishWithFailure(currentFhdr.fileName, "Path traversal attempt detected in batch file: " + currentFhdr.fileName);
                }

                // Handle duplicate file names if needed
                if (std::filesystem::exists(destPath, ec)) {
                    if (options.autoRenameDuplicates) {
                        std::string stem = destPath.stem().string();
                        std::string ext = destPath.extension().string();
                        int counter = 1;
                        while (counter < 10000) {
                            std::filesystem::path candidate = destPath.parent_path() / (stem + " (" + std::to_string(counter) + ")" + ext);
                            if (!std::filesystem::exists(candidate, ec)) {
                                destPath = candidate;
                                break;
                            }
                            counter++;
                        }
                    }
                }

                // Ensure parent directory exists
                std::filesystem::create_directories(destPath.parent_path(), ec);
                if (ec) {
                    return finishWithFailure(currentFhdr.fileName, "Failed to create directory structure: " + destPath.parent_path().string());
                }

                // Temporary .part file in destination directory
                std::filesystem::path partPath = destPath.parent_path() / ("." + destPath.filename().string() + "." + taskId + ".part");
                std::filesystem::remove(partPath, ec);

                std::ofstream outFile(partPath, std::ios::binary);
                if (!outFile.is_open()) {
                    return finishWithFailure(currentFhdr.fileName, "Failed to open temporary file: " + partPath.string());
                }

                landrop::crypto::Sha256 fileHasher;
                uint64_t fileReceived = 0;
                uint32_t expectedChunkIndex = 0;
                uint64_t expectedOffset = 0;

                while (fileReceived < currentFhdr.fileSize) {
                    if (isCancelled()) {
                        outFile.close();
                        std::filesystem::remove(partPath, ec);
                        return finishWithFailure(manifest.batchId, "Transfer cancelled by receiver");
                    }

                    uint8_t chunkHdrBytes[16];
                    if (!channel->receiveExact(chunkHdrBytes, 16, 15000, effectiveCancel.get())) {
                        outFile.close();
                        std::filesystem::remove(partPath, ec);
                        return finishWithFailure(currentFhdr.fileName, "Interrupted while reading ChunkData frame header");
                    }
                    protocol::v2::FrameHeader chunkFhdr;
                    std::string chunkHdrErr;
                    if (!protocol::v2::FrameHeader::deserialize(chunkHdrBytes, chunkFhdr, &chunkHdrErr)) {
                        outFile.close();
                        std::filesystem::remove(partPath, ec);
                        return finishWithFailure(currentFhdr.fileName, "Failed to deserialize ChunkData frame header: " + chunkHdrErr);
                    }
                    if (chunkFhdr.msgType == protocol::v2::MessageType::TransferCancel) {
                        outFile.close();
                        std::filesystem::remove(partPath, ec);
                        return finishWithFailure(manifest.batchId, "Transfer cancelled by sender");
                    }
                    if (chunkFhdr.msgType != protocol::v2::MessageType::ChunkData) {
                        outFile.close();
                        std::filesystem::remove(partPath, ec);
                        return finishWithFailure(currentFhdr.fileName, "Expected ChunkData frame, got " + std::to_string(static_cast<int>(chunkFhdr.msgType)));
                    }

                    std::vector<uint8_t> chunkPayload(chunkFhdr.payloadLength);
                    if (!channel->receiveExact(chunkPayload.data(), chunkFhdr.payloadLength, 30000, effectiveCancel.get())) {
                        outFile.close();
                        std::filesystem::remove(partPath, ec);
                        return finishWithFailure(currentFhdr.fileName, "Interrupted while receiving ChunkData payload");
                    }
                    if (chunkFhdr.crc32OrSeq != 0) {
                        uint32_t actualCrc = protocol::v2::computeCrc32(chunkPayload.data(), chunkPayload.size());
                        if (actualCrc != chunkFhdr.crc32OrSeq) {
                            outFile.close();
                            std::filesystem::remove(partPath, ec);
                            return finishWithFailure(currentFhdr.fileName, "Chunk frame CRC-32 checksum mismatch");
                        }
                    }

                    protocol::v2::ChunkDataMessage cmsg;
                    std::string cmsgErr;
                    if (!protocol::v2::ChunkDataMessage::deserialize(chunkPayload.data(), chunkPayload.size(), cmsg, &cmsgErr)) {
                        outFile.close();
                        std::filesystem::remove(partPath, ec);
                        return finishWithFailure(currentFhdr.fileName, "Failed to parse ChunkDataMessage: " + cmsgErr);
                    }

                    if (cmsg.chunkIndex != expectedChunkIndex || cmsg.offset != expectedOffset) {
                        outFile.close();
                        std::filesystem::remove(partPath, ec);
                        return finishWithFailure(currentFhdr.fileName, "Chunk sequence mismatch");
                    }

                    outFile.write(reinterpret_cast<const char*>(cmsg.data.data()), static_cast<std::streamsize>(cmsg.data.size()));
                    if (!outFile.good()) {
                        outFile.close();
                        std::filesystem::remove(partPath, ec);
                        return finishWithFailure(currentFhdr.fileName, "Disk write error");
                    }

                    fileHasher.update(cmsg.data.data(), cmsg.data.size());
                    fileReceived += cmsg.data.size();
                    totalBatchReceived += cmsg.data.size();
                    expectedChunkIndex++;
                    expectedOffset += cmsg.data.size();

                    auto now = std::chrono::steady_clock::now();
                    double elapsed = std::chrono::duration<double>(now - batchStartTime).count();
                    double speedBps = elapsed > 0.05 ? (static_cast<double>(totalBatchReceived) / elapsed) : 0.0;

                    if (progressCb && std::chrono::duration<double>(now - lastProgressReport).count() >= 0.1) {
                        lastProgressReport = now;
                        TransferProgressUpdate upd;
                        upd.taskId = taskId;
                        upd.fileName = currentFhdr.fileName;
                        upd.totalBytes = manifest.totalBytes;
                        upd.bytesTransferred = totalBatchReceived;
                        upd.progressPercentage = manifest.totalBytes > 0 ? ((static_cast<double>(totalBatchReceived) / static_cast<double>(manifest.totalBytes)) * 100.0) : 100.0;
                        upd.speedBytesPerSec = speedBps;
                        upd.state = TransferState::Transferring;
                        upd.message = "Receiving file " + std::to_string(fileIdx + 1) + " of " + std::to_string(manifest.totalFiles) + ": " + currentFhdr.fileName;
                        upd.currentFileIndex = fileIdx + 1;
                        upd.totalFiles = manifest.totalFiles;
                        progressCb(upd);
                    }
                }

                outFile.close();

                // Check file SHA-256
                std::string computedHash = fileHasher.finalize();
                bool hashMatch = (computedHash.size() == currentFhdr.sha256Hex.size()) &&
                    std::equal(computedHash.begin(), computedHash.end(), currentFhdr.sha256Hex.begin(),
                               [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
                if (!hashMatch) {
                    std::filesystem::remove(partPath, ec);
                    return finishWithFailure(currentFhdr.fileName, "Integrity verification failed for " + currentFhdr.fileName);
                }

                std::filesystem::rename(partPath, destPath, ec);
                if (ec) {
                    std::filesystem::remove(partPath, ec);
                    return finishWithFailure(currentFhdr.fileName, "Failed to finalize file: " + ec.message());
                }

                // Send ACK for this file
                protocol::v2::TransferAckMessage fileAck;
                fileAck.status = protocol::v2::AckStatus::Success;
                fileAck.receivedBytes = currentFhdr.fileSize;
                fileAck.message = "File verified";
                auto ackFileFrame = protocol::v2::buildFrame(protocol::v2::MessageType::TransferAck, fileAck.serialize());
                channel->sendExact(ackFileFrame.data(), ackFileFrame.size());
            }

            if (options.closeChannelOnCompletion) {
                channel->close();
            }

            if (progressCb) {
                TransferProgressUpdate upd;
                upd.taskId = taskId;
                upd.fileName = manifest.batchId;
                upd.totalBytes = manifest.totalBytes;
                upd.bytesTransferred = totalBatchReceived;
                upd.progressPercentage = 100.0;
                upd.state = TransferState::Completed;
                upd.message = "Batch received and verified successfully (" + std::to_string(manifest.totalFiles) + " files)";
                upd.currentFileIndex = manifest.totalFiles;
                upd.totalFiles = manifest.totalFiles;
                progressCb(upd);
            }

            if (onFinished) {
                onFinished(taskId, manifest.batchId, true, saveDirectory, "");
            }

            return true;
        }

        if (fhdr.msgType != protocol::v2::MessageType::FileHeader) {
            return finishWithFailure("", "Expected FileHeader or BatchManifest frame, got message type: " + std::to_string(static_cast<int>(fhdr.msgType)));
        }
        std::vector<uint8_t> payload(fhdr.payloadLength);
        if (fhdr.payloadLength > 0) {
            if (!channel->receiveExact(payload.data(), fhdr.payloadLength, 10000, effectiveCancel.get())) {
                return finishWithFailure("", "Failed to receive FileHeader payload");
            }
        }
        if (fhdr.crc32OrSeq != 0) {
            uint32_t actualCrc = protocol::v2::computeCrc32(payload.data(), payload.size());
            if (actualCrc != fhdr.crc32OrSeq) {
                return finishWithFailure("", "FileHeader CRC-32 checksum mismatch");
            }
        }
        std::string msgErr;
        if (!protocol::v2::FileHeaderMessage::deserialize(payload.data(), payload.size(), fileHeaderMsg, &msgErr)) {
            return finishWithFailure("", "Failed to parse FileHeaderMessage: " + msgErr);
        }
        fileName = fileHeaderMsg.fileName;
        fileSize = fileHeaderMsg.fileSize;
        expectedHash = fileHeaderMsg.sha256Hex;
    } else {
        if (options.requireProtocolV2) {
            return finishWithFailure("", "WireProtocol v2 required but client sent legacy header");
        }
        if (!options.allowLegacyV1Fallback) {
            return finishWithFailure("", "Legacy v1 fallback is disabled");
        }
        uint32_t nameLen = 0;
        std::memcpy(&nameLen, first4, sizeof(uint32_t));
        if (nameLen == 0 || nameLen > 255) {
            return finishWithFailure("", "Invalid filename length: " + std::to_string(nameLen));
        }
        fileName.resize(nameLen);
        if (!channel->receiveExact(reinterpret_cast<uint8_t*>(fileName.data()), nameLen, 10000, effectiveCancel.get())) {
            return finishWithFailure("", "Failed to receive filename");
        }
        if (!channel->receiveExact(reinterpret_cast<uint8_t*>(&fileSize), sizeof(uint64_t), 10000, effectiveCancel.get())) {
            return finishWithFailure(fileName, "Failed to receive file size");
        }
        expectedHash.resize(64);
        if (!channel->receiveExact(reinterpret_cast<uint8_t*>(expectedHash.data()), 64, 10000, effectiveCancel.get())) {
            return finishWithFailure(fileName, "Failed to receive expected SHA-256 hash");
        }
    }

    if (!isSafeFileName(fileName)) {
        return finishWithFailure(fileName, "Filename rejected due to safety policy: " + fileName);
    }

    std::filesystem::path cleanName = std::filesystem::path(fileName).filename();
    if (cleanName.empty() || cleanName.string() != fileName) {
        return finishWithFailure(fileName, "Filename contains path components: " + fileName);
    }

    if (!isValidFileSize(fileSize) || fileSize > options.maxFileSizeLimit) {
        return finishWithFailure(fileName, "Invalid or excessive file size: " + std::to_string(fileSize));
    }

    std::filesystem::path canonicalBase = std::filesystem::weakly_canonical(std::filesystem::path(saveDirectory));
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
        bool accepted = onIncoming(taskId, fileName, fileSize, senderAddr);
        if (!accepted) {
            return finishWithFailure(fileName, "Transfer rejected by local user");
        }
    }

    // Resilient resumption staging: keep consistent .part file path
    std::filesystem::path partPath = canonicalBase / ("." + cleanName.string() + ".part");
    bool canResume = isV2 && ((fileHeaderMsg.flags & protocol::v2::FileHeaderFlags::RESUME_SUPPORTED) != 0);

    landrop::crypto::Sha256 inFlightHasher;
    uint64_t totalReceived = 0;
    uint32_t expectedChunkIndex = 0;
    uint64_t expectedOffset = 0;

    bool didResume = false;
    if (canResume && std::filesystem::exists(partPath, ec)) {
        uint64_t existingBytes = std::filesystem::file_size(partPath, ec);
        uint32_t chunkSize = fileHeaderMsg.chunkSize > 0 ? fileHeaderMsg.chunkSize : (64 * 1024);
        uint64_t alignedOffset = (existingBytes / chunkSize) * chunkSize;
        if (alignedOffset > 0 && alignedOffset < fileSize) {
            if (existingBytes > alignedOffset) {
                std::filesystem::resize_file(partPath, alignedOffset, ec);
            }
            FileChunkHasher hasher;
            auto partialHashOpt = hasher.computePartialFileHash(partPath.string(), alignedOffset);
            if (partialHashOpt.has_value()) {
                protocol::v2::ResumeRequestMessage req;
                req.fileHash = fileHeaderMsg.sha256Hex;
                req.bytesReceivedSoFar = alignedOffset;
                req.partialSha256 = partialHashOpt.value();
                auto reqFrame = protocol::v2::buildFrame(protocol::v2::MessageType::ResumeRequest, req.serialize());
                if (channel->sendExact(reqFrame.data(), reqFrame.size())) {
                    uint8_t ackHdrBytes[16];
                    if (channel->receiveExact(ackHdrBytes, 16, 5000, effectiveCancel.get())) {
                        protocol::v2::FrameHeader ackFhdr;
                        std::string ackFhdrErr;
                        if (protocol::v2::FrameHeader::deserialize(ackHdrBytes, ackFhdr, &ackFhdrErr)) {
                            if (ackFhdr.msgType == protocol::v2::MessageType::TransferAck) {
                                std::vector<uint8_t> ackPayload(ackFhdr.payloadLength);
                                if (ackFhdr.payloadLength == 0 || channel->receiveExact(ackPayload.data(), ackFhdr.payloadLength, 5000, effectiveCancel.get())) {
                                    protocol::v2::TransferAckMessage ackMsg;
                                    std::string ackErr;
                                    if (protocol::v2::TransferAckMessage::deserialize(ackPayload.data(), ackPayload.size(), ackMsg, &ackErr)) {
                                        if (ackMsg.status == protocol::v2::AckStatus::ResumeAccepted && ackMsg.receivedBytes == alignedOffset) {
                                            didResume = true;
                                            totalReceived = alignedOffset;
                                            expectedOffset = alignedOffset;
                                            expectedChunkIndex = static_cast<uint32_t>(alignedOffset / chunkSize);
                                            // Seed inFlightHasher with the verified partial content
                                            std::ifstream preIn(partPath, std::ios::binary);
                                            if (preIn.is_open()) {
                                                std::vector<uint8_t> preBuf(64 * 1024);
                                                uint64_t preRemain = alignedOffset;
                                                while (preRemain > 0 && preIn) {
                                                    size_t toRead = static_cast<size_t>(std::min<uint64_t>(preRemain, preBuf.size()));
                                                    preIn.read(reinterpret_cast<char*>(preBuf.data()), static_cast<std::streamsize>(toRead));
                                                    size_t readCount = static_cast<size_t>(preIn.gcount());
                                                    if (readCount == 0) break;
                                                    inFlightHasher.update(preBuf.data(), readCount);
                                                    preRemain -= readCount;
                                                }
                                                preIn.close();
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    std::ofstream outFile;
    if (didResume) {
        outFile.open(partPath, std::ios::binary | std::ios::in | std::ios::out | std::ios::ate);
    } else {
        std::filesystem::remove(partPath, ec);
        outFile.open(partPath, std::ios::binary | std::ios::trunc);
    }

    if (!outFile.is_open()) {
        return finishWithFailure(fileName, "Failed to open temporary file: " + partPath.string());
    }

    auto startTime = std::chrono::steady_clock::now();
    auto lastProgressReport = startTime;
    constexpr uint32_t kMaxChunkSize = 4 * 1024 * 1024;
    std::vector<uint8_t> chunkBuf;

    if (progressCb) {
        TransferProgressUpdate upd;
        upd.taskId = taskId;
        upd.fileName = fileName;
        upd.totalBytes = fileSize;
        upd.bytesTransferred = totalReceived;
        upd.progressPercentage = fileSize > 0 ? ((static_cast<double>(totalReceived) / static_cast<double>(fileSize)) * 100.0) : 0.0;
        upd.state = TransferState::Transferring;
        upd.message = didResume ? ("Resuming from byte " + std::to_string(totalReceived) + "...") : "Receiving chunks...";
        progressCb(upd);
    }

    while (totalReceived < fileSize) {
        if (isCancelled()) {
            outFile.close();
            if (!canResume) {
                std::filesystem::remove(partPath, ec);
            }
            return finishWithFailure(fileName, "Transfer cancelled by receiver");
        }

        if (isV2) {
            uint8_t chunkHdrBytes[16];
            if (!channel->receiveExact(chunkHdrBytes, 16, 15000, effectiveCancel.get())) {
                outFile.close();
                if (!canResume) {
                    std::filesystem::remove(partPath, ec);
                }
                return finishWithFailure(fileName, "Interrupted while reading ChunkData frame header");
            }
            protocol::v2::FrameHeader chunkFhdr;
            std::string chunkHdrErr;
            if (!protocol::v2::FrameHeader::deserialize(chunkHdrBytes, chunkFhdr, &chunkHdrErr)) {
                outFile.close();
                std::filesystem::remove(partPath, ec);
                return finishWithFailure(fileName, "Failed to deserialize ChunkData frame header: " + chunkHdrErr);
            }
            if (chunkFhdr.msgType == protocol::v2::MessageType::TransferCancel) {
                outFile.close();
                std::filesystem::remove(partPath, ec);
                return finishWithFailure(fileName, "Transfer cancelled by sender");
            }
            if (chunkFhdr.msgType != protocol::v2::MessageType::ChunkData) {
                outFile.close();
                std::filesystem::remove(partPath, ec);
                return finishWithFailure(fileName, "Expected ChunkData frame, got " + std::to_string(static_cast<int>(chunkFhdr.msgType)));
            }
            std::vector<uint8_t> chunkPayload(chunkFhdr.payloadLength);
            if (!channel->receiveExact(chunkPayload.data(), chunkFhdr.payloadLength, 30000, effectiveCancel.get())) {
                outFile.close();
                std::filesystem::remove(partPath, ec);
                return finishWithFailure(fileName, "Interrupted while receiving ChunkData payload");
            }
            if (chunkFhdr.crc32OrSeq != 0) {
                uint32_t actualCrc = protocol::v2::computeCrc32(chunkPayload.data(), chunkPayload.size());
                if (actualCrc != chunkFhdr.crc32OrSeq) {
                    outFile.close();
                    std::filesystem::remove(partPath, ec);
                    return finishWithFailure(fileName, "Chunk frame CRC-32 checksum mismatch");
                }
            }
            protocol::v2::ChunkDataMessage cmsg;
            std::string cmsgErr;
            if (!protocol::v2::ChunkDataMessage::deserialize(chunkPayload.data(), chunkPayload.size(), cmsg, &cmsgErr)) {
                outFile.close();
                std::filesystem::remove(partPath, ec);
                return finishWithFailure(fileName, "Failed to parse ChunkDataMessage: " + cmsgErr);
            }
            if (cmsg.chunkIndex != expectedChunkIndex || cmsg.offset != expectedOffset) {
                outFile.close();
                std::filesystem::remove(partPath, ec);
                return finishWithFailure(fileName, "Chunk sequence mismatch: expected index " + std::to_string(expectedChunkIndex) +
                                                   " at offset " + std::to_string(expectedOffset) +
                                                   ", got index " + std::to_string(cmsg.chunkIndex) +
                                                   " at offset " + std::to_string(cmsg.offset));
            }
            // DEBT-07: Explicit write error check
            outFile.write(reinterpret_cast<const char*>(cmsg.data.data()), static_cast<std::streamsize>(cmsg.data.size()));
            if (!outFile.good()) {
                outFile.close();
                std::filesystem::remove(partPath, ec);
                return finishWithFailure(fileName, "Disk write error while writing chunk to " + partPath.string());
            }
            inFlightHasher.update(cmsg.data.data(), cmsg.data.size());
            totalReceived += cmsg.data.size();
            expectedChunkIndex++;
            expectedOffset += cmsg.data.size();
        } else {
            uint32_t chunkSize = 0;
            if (!channel->receiveExact(reinterpret_cast<uint8_t*>(&chunkSize), sizeof(uint32_t), 15000, effectiveCancel.get())) {
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

            if (!channel->receiveExact(chunkBuf.data(), chunkSize, 30000, effectiveCancel.get())) {
                outFile.close();
                std::filesystem::remove(partPath, ec);
                return finishWithFailure(fileName, "Sender disconnected abruptly while sending chunk payload");
            }

            // DEBT-07: Explicit write error check
            outFile.write(reinterpret_cast<const char*>(chunkBuf.data()), static_cast<std::streamsize>(chunkSize));
            if (!outFile.good()) {
                outFile.close();
                std::filesystem::remove(partPath, ec);
                return finishWithFailure(fileName, "Disk write error while writing chunk to " + partPath.string());
            }
            inFlightHasher.update(chunkBuf.data(), chunkSize);
            totalReceived += chunkSize;
        }

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - startTime).count();
        double speedBps = elapsed > 0.05 ? (static_cast<double>(totalReceived) / elapsed) : 0.0;

        if (progressCb && std::chrono::duration<double>(now - lastProgressReport).count() >= 0.1) {
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
            progressCb(upd);
        }
    }

    outFile.close();

    // Verify SHA-256 integrity using in-flight computed hash
    std::string computedHash = inFlightHasher.finalize();
    bool hashMatch = (computedHash.size() == expectedHash.size()) &&
        std::equal(computedHash.begin(), computedHash.end(), expectedHash.begin(),
                   [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });

    if (!hashMatch) {
        std::filesystem::remove(partPath, ec);
        return finishWithFailure(fileName, "Integrity verification failed: SHA-256 mismatch (expected=" + expectedHash + ", actual=" + computedHash + ")");
    }

    // Atomic rename .part -> destPath (with cross-device fallback)
    std::filesystem::rename(partPath, destPath, ec);
    if (ec) {
        ec.clear();
        std::filesystem::copy_file(partPath, destPath, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(partPath);
        if (ec) {
            return finishWithFailure(fileName, "Failed to finalize received file: " + ec.message());
        }
    }

    // Send positive ACK (WireProtocol v2 TransferAckMessage or legacy 1-byte ACK)
    if (isV2) {
        protocol::v2::TransferAckMessage ackMsg;
        ackMsg.status = protocol::v2::AckStatus::Success;
        ackMsg.receivedBytes = fileSize;
        ackMsg.message = "File verified and saved successfully";
        auto ackFrame = protocol::v2::buildFrame(protocol::v2::MessageType::TransferAck, ackMsg.serialize());
        channel->sendExact(ackFrame.data(), ackFrame.size());
    } else {
        uint8_t ack = 1;
        channel->sendExact(&ack, 1);
    }

    if (options.closeChannelOnCompletion) {
        channel->close();
    }

    if (progressCb) {
        TransferProgressUpdate upd;
        upd.taskId = taskId;
        upd.fileName = fileName;
        upd.totalBytes = fileSize;
        upd.bytesTransferred = fileSize;
        upd.progressPercentage = 100.0;
        upd.state = TransferState::Completed;
        upd.message = "File received and verified successfully";
        progressCb(upd);
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
        socket_handle_t listenFd = m_listenSession->getListenFd();
        if (!isValidSocket(listenFd)) break;

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listenFd, &fds);
        timeval tv{0, 250000}; // 250 ms timeout for responsiveness

        int ret = select(static_cast<int>(listenFd + 1), &fds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(listenFd, &fds)) {
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            socket_handle_t clientFd = accept(listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
            if (isValidSocket(clientFd)) {
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
