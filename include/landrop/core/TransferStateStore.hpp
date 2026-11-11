#pragma once

#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <cstdint>
#include "landrop/tasks/TransferTask.hpp"

namespace landrop {

struct PersistedTransferRecord {
    std::string taskId;
    std::string fileName;
    std::string relativePath;
    std::string localFilePath;
    uint64_t fileSize{0};
    uint64_t bytesTransferred{0};
    TransferDirection direction{TransferDirection::Upload};
    std::string targetPeerId;
    TransferState state{TransferState::Idle};
    std::string fileHash;
    std::string partialHash;
    bool isBatch{false};
    std::string batchId;
    uint32_t totalFiles{1};
    int64_t lastUpdatedEpoch{0};
};

class TransferStateStore {
public:
    explicit TransferStateStore(std::string journalPath = "");
    ~TransferStateStore() = default;

    TransferStateStore(const TransferStateStore&) = delete;
    TransferStateStore& operator=(const TransferStateStore&) = delete;

    void setJournalPath(std::string path);
    const std::string& getJournalPath() const { return m_journalPath; }

    bool saveRecord(const PersistedTransferRecord& record);
    bool updateProgress(const std::string& taskId, uint64_t bytesTransferred, TransferState state, const std::string& partialHash = "");
    bool removeRecord(const std::string& taskId);
    std::optional<PersistedTransferRecord> getRecord(const std::string& taskId) const;
    std::vector<PersistedTransferRecord> loadAllRecords() const;
    void clearFinishedRecords();
    size_t recordCount() const;

    static std::string getDefaultJournalPath();

private:
    mutable std::mutex m_mutex;
    std::string m_journalPath;

    bool persistToDiskLocked(const std::vector<PersistedTransferRecord>& records) const;
    std::vector<PersistedTransferRecord> readFromDiskLocked() const;
};

} // namespace landrop
