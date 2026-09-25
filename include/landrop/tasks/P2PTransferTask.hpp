#pragma once

#include "TransferTask.hpp"
#include "landrop/core/FileChunkHasher.hpp"

namespace landrop {

class P2PTransferTask : public TransferTask {
public:
    P2PTransferTask(
        std::string taskId,
        std::string fileName,
        uint64_t fileSize,
        TransferDirection direction,
        std::string targetPeerId,
        size_t chunkSize = 64 * 1024,
        bool enableResume = true
    );

    ~P2PTransferTask() override = default;

    bool initialize() override;
    void processChunk(size_t chunkIndex, const std::vector<uint8_t>& data) override;
    std::string getTaskType() const override;

    void setExpectedChunkHashes(const std::vector<std::string>& hashes);
    bool verifyChunkHash(size_t chunkIndex, const std::vector<uint8_t>& data);
    
    void markChunkMissing(size_t chunkIndex);
    std::vector<size_t> getMissingChunkIndices() const;
    bool isChunkCompleted(size_t chunkIndex) const;

    size_t getTotalChunks() const { return m_totalChunks; }
    size_t getCompletedChunks() const { return m_completedChunks; }
    const std::string& getTargetPeerId() const { return m_targetPeerId; }
    
    bool isResumeEnabled() const { return m_resumeEnabled; }
    void setResumeEnabled(bool enabled) { m_resumeEnabled = enabled; }

    uint32_t getRetryCount() const { return m_retryCount; }
    void incrementRetryCount() { ++m_retryCount; }

private:
    std::string m_targetPeerId;
    size_t m_totalChunks{0};
    size_t m_completedChunks{0};
    std::vector<ChunkInfo> m_chunks;
    bool m_resumeEnabled{true};
    uint32_t m_retryCount{0};
    FileChunkHasher m_hasher;
};

}
