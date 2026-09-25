#include "landrop/tasks/P2PTransferTask.hpp"
#include <utility>
#include <algorithm>

namespace landrop {

P2PTransferTask::P2PTransferTask(
    std::string taskId,
    std::string fileName,
    uint64_t fileSize,
    TransferDirection direction,
    std::string targetPeerId,
    size_t chunkSize,
    bool enableResume
) : TransferTask(std::move(taskId), std::move(fileName), fileSize, direction, chunkSize),
    m_targetPeerId(std::move(targetPeerId)),
    m_resumeEnabled(enableResume) {
    if (m_fileSize > 0 && m_chunkSize > 0) {
        m_totalChunks = static_cast<size_t>((m_fileSize + m_chunkSize - 1) / m_chunkSize);
    }
    m_chunks.resize(m_totalChunks);
    for (size_t i = 0; i < m_totalChunks; ++i) {
        m_chunks[i].index = i;
        m_chunks[i].offset = i * m_chunkSize;
        m_chunks[i].size = std::min(static_cast<uint64_t>(m_chunkSize), m_fileSize - m_chunks[i].offset);
    }
}

bool P2PTransferTask::initialize() {
    m_state = TransferState::Connecting;
    m_completedChunks = 0;
    m_bytesTransferred = 0;
    return true;
}

void P2PTransferTask::processChunk(size_t chunkIndex, const std::vector<uint8_t>& data) {
    if (chunkIndex >= m_chunks.size()) {
        return;
    }

    if (!m_chunks[chunkIndex].isCompleted) {
        m_chunks[chunkIndex].isCompleted = true;
        m_chunks[chunkIndex].size = data.size();
        m_chunks[chunkIndex].sha256Hash = m_hasher.computeChunkHash(data);
        m_chunks[chunkIndex].isVerified = true;
        
        m_bytesTransferred += data.size();
        ++m_completedChunks;

        if (m_completedChunks >= m_totalChunks) {
            m_state = TransferState::Completed;
        } else {
            m_state = TransferState::Transferring;
        }
    }
}

std::string P2PTransferTask::getTaskType() const {
    return "P2PTransferTask";
}

void P2PTransferTask::setExpectedChunkHashes(const std::vector<std::string>& hashes) {
    size_t count = std::min(m_chunks.size(), hashes.size());
    for (size_t i = 0; i < count; ++i) {
        m_chunks[i].sha256Hash = hashes[i];
    }
}

bool P2PTransferTask::verifyChunkHash(size_t chunkIndex, const std::vector<uint8_t>& data) {
    if (chunkIndex >= m_chunks.size()) {
        return false;
    }
    return m_hasher.verifyChunk(data, m_chunks[chunkIndex].sha256Hash);
}

void P2PTransferTask::markChunkMissing(size_t chunkIndex) {
    if (chunkIndex < m_chunks.size() && m_chunks[chunkIndex].isCompleted) {
        m_chunks[chunkIndex].isCompleted = false;
        m_chunks[chunkIndex].isVerified = false;
        if (m_completedChunks > 0) {
            --m_completedChunks;
        }
        if (m_bytesTransferred >= m_chunks[chunkIndex].size) {
            m_bytesTransferred -= m_chunks[chunkIndex].size;
        }
    }
}

std::vector<size_t> P2PTransferTask::getMissingChunkIndices() const {
    std::vector<size_t> missing;
    for (size_t i = 0; i < m_chunks.size(); ++i) {
        if (!m_chunks[i].isCompleted) {
            missing.push_back(i);
        }
    }
    return missing;
}

bool P2PTransferTask::isChunkCompleted(size_t chunkIndex) const {
    if (chunkIndex < m_chunks.size()) {
        return m_chunks[chunkIndex].isCompleted;
    }
    return false;
}

}
