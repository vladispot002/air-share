#include "landrop/tasks/TransferTask.hpp"
#include <utility>

namespace landrop {

TransferTask::TransferTask(
    std::string taskId,
    std::string fileName,
    uint64_t fileSize,
    TransferDirection direction,
    size_t chunkSize
) : m_taskId(std::move(taskId)),
    m_fileName(std::move(fileName)),
    m_fileSize(fileSize),
    m_chunkSize(chunkSize),
    m_direction(direction),
    m_startTime(std::chrono::steady_clock::now()) {}

void TransferTask::pause() {
    if (m_state == TransferState::Transferring) {
        m_state = TransferState::Paused;
    }
}

void TransferTask::resume() {
    if (m_state == TransferState::Paused) {
        m_state = TransferState::Transferring;
    }
}

void TransferTask::cancel() {
    m_state = TransferState::Cancelled;
}

double TransferTask::getProgressPercentage() const {
    if (m_fileSize == 0) {
        return 100.0;
    }
    return (static_cast<double>(m_bytesTransferred) / static_cast<double>(m_fileSize)) * 100.0;
}

double TransferTask::calculateEstimatedRemainingSeconds() const {
    if (m_currentSpeedBytesPerSec <= 0.0 || m_bytesTransferred >= m_fileSize) {
        return 0.0;
    }
    uint64_t remainingBytes = m_fileSize - m_bytesTransferred;
    return static_cast<double>(remainingBytes) / m_currentSpeedBytesPerSec;
}

void TransferTask::updateSpeed(uint64_t recentBytes, double intervalSeconds) {
    if (intervalSeconds > 0.0) {
        m_currentSpeedBytesPerSec = static_cast<double>(recentBytes) / intervalSeconds;
    }
}

int64_t TransferTask::getElapsedSeconds() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now - m_startTime).count();
}

std::ostream& operator<<(std::ostream& os, const TransferTask& task) {
    os << "[" << task.getTaskType() << "] " << task.m_fileName << " ("
       << task.m_bytesTransferred << "/" << task.m_fileSize << " bytes, "
       << transferStateToString(task.m_state) << ")";
    return os;
}

}
