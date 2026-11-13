#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <optional>
#include <filesystem>
#include <unordered_map>
#include "landrop/utils/Types.hpp"
#include "landrop/core/NetworkSession.hpp"
#include "landrop/core/FileChunkHasher.hpp"
#include "landrop/core/ITransportChannel.hpp"

namespace landrop {

struct TransferProgressUpdate {
    std::string taskId;
    std::string fileName;
    uint64_t bytesTransferred{0};
    uint64_t totalBytes{0};
    double progressPercentage{0.0};
    double speedBytesPerSec{0.0};
    TransferState state{TransferState::Idle};
    std::string message;
    uint32_t currentFileIndex{1};
    uint32_t totalFiles{1};
};

using TransferProgressCallback = std::function<void(const TransferProgressUpdate&)>;

struct BatchFileItem {
    std::string localFilePath; // Local path on sender's filesystem
    std::string relativePath;  // Safe relative destination path (e.g., "docs/report.pdf")
    uint64_t fileSize{0};
    std::string sha256Hex;     // Precomputed hash (computed automatically if empty)
};

struct TransferOptions {
    bool useTls{false};
    std::string expectedFingerprint;
    uint32_t chunkSize{64 * 1024}; // 64 KB default chunk size
    uint64_t maxFileSizeLimit{10ULL * 1024 * 1024 * 1024}; // 10 GB limit
    uint32_t speedLimitKbps{0}; // 0 = unlimited
    bool autoRenameDuplicates{false}; // When true, rename file (1).ext on duplicate instead of failing
    bool allowLegacyV1Fallback{true}; // Allows transparent interop with legacy LAN-Drop v1.x clients
    bool requireProtocolV2{false};     // When true, strictly aborts if incoming connection does not negotiate Protocol v2
    bool closeChannelOnCompletion{true}; // When false, channel remains open after transfer completion/failure
    uint32_t headerTimeoutMs{10000};     // Timeout for initial header (0 = indefinite wait until cancel/disconnect)
    bool enableResume{true};             // Whether to negotiate byte-offset resumption (Milestone 3D)
    uint32_t resumeTimeoutMs{150};       // Negotiation timeout window waiting for ResumeRequest (ms)
};

class TransferEngine {
public:
    TransferEngine();
    ~TransferEngine();

    // Prevent copying
    TransferEngine(const TransferEngine&) = delete;
    TransferEngine& operator=(const TransferEngine&) = delete;

    // Scan a local directory recursively into a list of safe BatchFileItem
    static std::vector<BatchFileItem> scanDirectory(const std::string& directoryPath, std::string* outError = nullptr);

    // Send a single file to a remote peer (client mode)
    bool sendFile(
        const std::string& taskId,
        const std::string& filePath,
        const std::string& remoteIp,
        uint16_t remotePort,
        const TransferOptions& options,
        TransferProgressCallback progressCb,
        std::atomic<bool>* cancelFlag = nullptr,
        std::string* outError = nullptr
    );

    // Send a single file over an arbitrary transport channel (WebRTC, TCP, etc.)
    bool sendFileOverChannel(
        const std::string& taskId,
        const std::string& filePath,
        std::shared_ptr<ITransportChannel> channel,
        const TransferOptions& options,
        TransferProgressCallback progressCb,
        std::atomic<bool>* cancelFlag = nullptr,
        std::string* outError = nullptr
    );

    // Send multiple files or a directory batch to a remote peer (client mode)
    bool sendBatch(
        const std::string& taskId,
        const std::vector<BatchFileItem>& items,
        const std::string& batchName,
        const std::string& remoteIp,
        uint16_t remotePort,
        const TransferOptions& options,
        TransferProgressCallback progressCb,
        std::atomic<bool>* cancelFlag = nullptr,
        std::string* outError = nullptr
    );

    // Send multiple files or a directory batch over an arbitrary transport channel
    bool sendBatchOverChannel(
        const std::string& taskId,
        const std::vector<BatchFileItem>& items,
        const std::string& batchName,
        std::shared_ptr<ITransportChannel> channel,
        const TransferOptions& options,
        TransferProgressCallback progressCb,
        std::atomic<bool>* cancelFlag = nullptr,
        std::string* outError = nullptr
    );

    // Receive a single file (CLI receive mode)
    bool receiveFileOnce(
        const std::string& taskId,
        uint16_t listenPort,
        const std::string& saveDirectory,
        const TransferOptions& options,
        TransferProgressCallback progressCb,
        std::atomic<bool>* cancelFlag = nullptr,
        std::string* outError = nullptr
    );

    // Background server mode callbacks (for Desktop GUI)
    using ServerIncomingCallback = std::function<bool(
        const std::string& taskId,
        const std::string& fileName,
        uint64_t fileSize,
        const std::string& senderIp
    )>;

    using ServerFinishedCallback = std::function<void(
        const std::string& taskId,
        const std::string& fileName,
        bool success,
        const std::string& savedPath,
        const std::string& error
    )>;

    // Receive a single file over an arbitrary transport channel (WebRTC, TCP, etc.)
    bool receiveFileOverChannel(
        const std::string& taskId,
        const std::string& saveDirectory,
        std::shared_ptr<ITransportChannel> channel,
        const TransferOptions& options,
        TransferProgressCallback progressCb,
        std::atomic<bool>* cancelFlag = nullptr,
        std::string* outError = nullptr,
        ServerIncomingCallback onIncoming = nullptr,
        ServerFinishedCallback onFinished = nullptr
    );

    // Start background receiver listener
    bool startServer(
        uint16_t listenPort,
        const std::string& saveDirectory,
        const TransferOptions& options,
        ServerIncomingCallback onIncoming,
        TransferProgressCallback onProgress,
        ServerFinishedCallback onFinished
    );

    // Stop background receiver listener
    void stopServer();

    // Cancel active transfer by taskId
    void cancelTask(const std::string& taskId);

    void registerCancelToken(const std::string& taskId, std::shared_ptr<std::atomic<bool>> token);
    void unregisterCancelToken(const std::string& taskId);

    bool isServerRunning() const { return m_serverRunning.load(); }
    uint16_t getServerPort() const { return m_serverPort; }
    std::string getSaveDirectory() const {
        std::lock_guard<std::mutex> lock(m_saveDirMutex);
        return m_saveDirectory;
    }
    void setSaveDirectory(const std::string& saveDir) {
        std::lock_guard<std::mutex> lock(m_saveDirMutex);
        m_saveDirectory = saveDir;
    }

private:
    mutable std::mutex m_saveDirMutex;
    std::mutex m_cancelTokensMutex;
    std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>> m_cancelTokens;
    std::atomic<uint64_t> m_nextSessionId{100};
    std::atomic<bool> m_serverRunning{false};
    uint16_t m_serverPort{0};
    std::string m_saveDirectory;
    TransferOptions m_serverOptions;
    std::unique_ptr<std::thread> m_serverThread;
    std::unique_ptr<NetworkSession> m_listenSession;

    void serverLoop(
        ServerIncomingCallback onIncoming,
        TransferProgressCallback onProgress,
        ServerFinishedCallback onFinished
    );

    bool handleIncomingConnection(
        std::unique_ptr<NetworkSession> session,
        const std::string& saveDir,
        const TransferOptions& options,
        ServerIncomingCallback onIncoming,
        TransferProgressCallback onProgress,
        ServerFinishedCallback onFinished,
        std::atomic<bool>* cancelFlag
    );
};

} // namespace landrop
