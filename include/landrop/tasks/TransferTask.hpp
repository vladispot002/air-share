#pragma once

#include <string>
#include <cstdint>
#include <chrono>
#include <vector>
#include <iostream>
#include "landrop/utils/Types.hpp"

namespace landrop {

class TransferTask {
public:
    TransferTask(
        std::string taskId,
        std::string fileName,
        uint64_t fileSize,
        TransferDirection direction,
        size_t chunkSize = 64 * 1024
    );

    virtual ~TransferTask() = default;

    virtual bool initialize() = 0;
    virtual void processChunk(size_t chunkIndex, const std::vector<uint8_t>& data) = 0;
    virtual std::string getTaskType() const = 0;

    virtual void pause();
    virtual void resume();
    virtual void cancel();

    double getProgressPercentage() const;
    double calculateEstimatedRemainingSeconds() const;
    void updateSpeed(uint64_t recentBytes, double intervalSeconds);

    friend std::ostream& operator<<(std::ostream& os, const TransferTask& task);

    const std::string& getTaskId() const { return m_taskId; }
    const std::string& getFileName() const { return m_fileName; }
    uint64_t getFileSize() const { return m_fileSize; }
    uint64_t getBytesTransferred() const { return m_bytesTransferred; }
    size_t getChunkSize() const { return m_chunkSize; }

    TransferState getState() const { return m_state; }
    void setState(TransferState state) { m_state = state; }

    TransferDirection getDirection() const { return m_direction; }
    double getCurrentSpeedBps() const { return m_currentSpeedBytesPerSec; }

    int64_t getElapsedSeconds() const;

protected:
    std::string m_taskId;
    std::string m_fileName;
    uint64_t m_fileSize;
    uint64_t m_bytesTransferred{0};
    size_t m_chunkSize;
    TransferState m_state{TransferState::Idle};
    TransferDirection m_direction;
    std::chrono::steady_clock::time_point m_startTime;
    double m_currentSpeedBytesPerSec{0.0};
};

}
