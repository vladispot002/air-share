#include "landrop/core/TransferStateStore.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace landrop {

using json = nlohmann::json;

static std::string directionToString(TransferDirection dir) {
    return dir == TransferDirection::Download ? "Download" : "Upload";
}

static TransferDirection stringToDirection(const std::string& str) {
    return str == "Download" ? TransferDirection::Download : TransferDirection::Upload;
}

static std::string stateToString(TransferState st) {
    switch (st) {
        case TransferState::Connecting:   return "Connecting";
        case TransferState::Transferring: return "Transferring";
        case TransferState::Paused:       return "Paused";
        case TransferState::Completed:    return "Completed";
        case TransferState::Failed:       return "Failed";
        case TransferState::Cancelled:    return "Cancelled";
        case TransferState::Idle:
        default:                          return "Idle";
    }
}

static TransferState stringToState(const std::string& str) {
    if (str == "Connecting") return TransferState::Connecting;
    if (str == "Transferring") return TransferState::Transferring;
    if (str == "Paused") return TransferState::Paused;
    if (str == "Completed") return TransferState::Completed;
    if (str == "Failed") return TransferState::Failed;
    if (str == "Cancelled") return TransferState::Cancelled;
    return TransferState::Idle;
}

static int64_t currentEpochSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string TransferStateStore::getDefaultJournalPath() {
#ifdef _WIN32
    char appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
        std::filesystem::path p(appDataPath);
        p /= "LAN-Drop";
        p /= "transfer_journal.json";
        return p.string();
    }
    return "transfer_journal.json";
#else
    const char* xdgData = std::getenv("XDG_DATA_HOME");
    if (xdgData && *xdgData) {
        std::filesystem::path p(xdgData);
        p /= "landrop";
        p /= "transfer_journal.json";
        return p.string();
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        std::filesystem::path p(home);
        p /= ".local";
        p /= "share";
        p /= "landrop";
        p /= "transfer_journal.json";
        return p.string();
    }
    return "transfer_journal.json";
#endif
}

TransferStateStore::TransferStateStore(std::string journalPath)
    : m_journalPath(std::move(journalPath)) {
    if (m_journalPath.empty()) {
        m_journalPath = getDefaultJournalPath();
    }
}

void TransferStateStore::setJournalPath(std::string path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_journalPath = std::move(path);
}

bool TransferStateStore::persistToDiskLocked(const std::vector<PersistedTransferRecord>& records) const {
    if (m_journalPath.empty()) return false;

    std::error_code ec;
    std::filesystem::path target(m_journalPath);
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
    }

    std::filesystem::path tmpPath = target.string() + ".tmp";

    json jArray = json::array();
    for (const auto& r : records) {
        json item;
        item["taskId"] = r.taskId;
        item["fileName"] = r.fileName;
        item["relativePath"] = r.relativePath;
        item["localFilePath"] = r.localFilePath;
        item["fileSize"] = r.fileSize;
        item["bytesTransferred"] = r.bytesTransferred;
        item["direction"] = directionToString(r.direction);
        item["targetPeerId"] = r.targetPeerId;
        item["state"] = stateToString(r.state);
        item["fileHash"] = r.fileHash;
        item["partialHash"] = r.partialHash;
        item["isBatch"] = r.isBatch;
        item["batchId"] = r.batchId;
        item["totalFiles"] = r.totalFiles;
        item["lastUpdatedEpoch"] = r.lastUpdatedEpoch > 0 ? r.lastUpdatedEpoch : currentEpochSeconds();
        jArray.push_back(item);
    }

    std::string serialized = jArray.dump(2);

    std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;

    out.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    out.flush();
    if (!out.good()) {
        out.close();
        std::filesystem::remove(tmpPath, ec);
        return false;
    }
    out.close();

    std::filesystem::rename(tmpPath, target, ec);
    if (ec) {
        // Fallback for filesystem edge cases: copy and remove tmp
        std::filesystem::copy_file(tmpPath, target, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(tmpPath, ec);
    }
    return !ec;
}

std::vector<PersistedTransferRecord> TransferStateStore::readFromDiskLocked() const {
    std::vector<PersistedTransferRecord> result;
    if (m_journalPath.empty()) return result;

    std::error_code ec;
    if (!std::filesystem::exists(m_journalPath, ec)) {
        return result;
    }

    std::ifstream in(m_journalPath, std::ios::binary);
    if (!in.is_open()) return result;

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    if (content.empty()) return result;

    try {
        json jArray = json::parse(content);
        if (!jArray.is_array()) return result;

        for (const auto& item : jArray) {
            PersistedTransferRecord r;
            r.taskId = item.value("taskId", "");
            if (r.taskId.empty()) continue;

            r.fileName = item.value("fileName", "");
            r.relativePath = item.value("relativePath", "");
            r.localFilePath = item.value("localFilePath", "");
            r.fileSize = item.value("fileSize", static_cast<uint64_t>(0));
            r.bytesTransferred = item.value("bytesTransferred", static_cast<uint64_t>(0));
            r.direction = stringToDirection(item.value("direction", "Upload"));
            r.targetPeerId = item.value("targetPeerId", "");
            r.state = stringToState(item.value("state", "Idle"));
            r.fileHash = item.value("fileHash", "");
            r.partialHash = item.value("partialHash", "");
            r.isBatch = item.value("isBatch", false);
            r.batchId = item.value("batchId", "");
            r.totalFiles = item.value("totalFiles", static_cast<uint32_t>(1));
            r.lastUpdatedEpoch = item.value("lastUpdatedEpoch", static_cast<int64_t>(0));
            result.push_back(std::move(r));
        }
    } catch (...) {
        // Corrupted file ignored safely
    }
    return result;
}

bool TransferStateStore::saveRecord(const PersistedTransferRecord& record) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto records = readFromDiskLocked();

    bool found = false;
    for (auto& r : records) {
        if (r.taskId == record.taskId) {
            r = record;
            if (r.lastUpdatedEpoch <= 0) {
                r.lastUpdatedEpoch = currentEpochSeconds();
            }
            found = true;
            break;
        }
    }
    if (!found) {
        auto rec = record;
        if (rec.lastUpdatedEpoch <= 0) {
            rec.lastUpdatedEpoch = currentEpochSeconds();
        }
        records.push_back(std::move(rec));
    }

    return persistToDiskLocked(records);
}

bool TransferStateStore::updateProgress(
    const std::string& taskId,
    uint64_t bytesTransferred,
    TransferState state,
    const std::string& partialHash
) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto records = readFromDiskLocked();

    bool found = false;
    for (auto& r : records) {
        if (r.taskId == taskId) {
            r.bytesTransferred = bytesTransferred;
            r.state = state;
            if (!partialHash.empty()) {
                r.partialHash = partialHash;
            }
            r.lastUpdatedEpoch = currentEpochSeconds();
            found = true;
            break;
        }
    }
    if (!found) return false;

    return persistToDiskLocked(records);
}

bool TransferStateStore::removeRecord(const std::string& taskId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto records = readFromDiskLocked();

    auto it = std::remove_if(records.begin(), records.end(), [&](const PersistedTransferRecord& r) {
        return r.taskId == taskId;
    });

    if (it == records.end()) return false;
    records.erase(it, records.end());

    return persistToDiskLocked(records);
}

std::optional<PersistedTransferRecord> TransferStateStore::getRecord(const std::string& taskId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto records = readFromDiskLocked();

    for (const auto& r : records) {
        if (r.taskId == taskId) {
            return r;
        }
    }
    return std::nullopt;
}

std::vector<PersistedTransferRecord> TransferStateStore::loadAllRecords() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return readFromDiskLocked();
}

void TransferStateStore::clearFinishedRecords() {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto records = readFromDiskLocked();

    auto it = std::remove_if(records.begin(), records.end(), [](const PersistedTransferRecord& r) {
        return r.state == TransferState::Completed ||
               r.state == TransferState::Cancelled ||
               r.state == TransferState::Failed;
    });

    if (it != records.end()) {
        records.erase(it, records.end());
        persistToDiskLocked(records);
    }
}

size_t TransferStateStore::recordCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return readFromDiskLocked().size();
}

} // namespace landrop
