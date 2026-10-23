#pragma once

#include <string>
#include <vector>
#include <optional>
#include "landrop/utils/Types.hpp"

namespace landrop {

class SharedFolderManager {
public:
    explicit SharedFolderManager(std::string rootPath = "./shared_drop");
    ~SharedFolderManager() = default;

    bool setRootPath(const std::string& newPath);
    const std::string& getRootPath() const { return m_rootSharedPath; }

    size_t scanDirectory();
    bool addSharedFile(const std::string& filePath);
    bool removeSharedFile(const std::string& fileName);
    
    std::optional<FileMetadata> findFile(const std::string& fileName) const;
    const std::vector<FileMetadata>& getSharedFiles() const { return m_sharedFiles; }

    uint64_t getTotalSharedBytes() const { return m_totalSharedBytes; }
    size_t getSharedFileCount() const { return m_sharedFiles.size(); }

    bool isReadOnly() const { return m_isReadOnly; }
    void setReadOnly(bool readOnly) { m_isReadOnly = readOnly; }

    void setAccessPassword(const std::string& password);
    bool verifyAccessPassword(const std::string& password) const;
    bool hasPasswordProtection() const { return !m_accessPasswordHash.empty(); }
    const std::string& getPasswordHash() const { return m_accessPasswordHash; }
    void setPasswordHash(std::string hash) { m_accessPasswordHash = std::move(hash); }
    uint32_t getFailedAttempts() const { return m_failedAttempts; }

    void setMaxFilesLimit(size_t limit) { m_maxFilesLimit = limit; }
    size_t getMaxFilesLimit() const { return m_maxFilesLimit; }

    void setMaxFileSizeLimit(uint64_t limit) { m_maxFileSizeLimit = limit; }
    uint64_t getMaxFileSizeLimit() const { return m_maxFileSizeLimit; }

    std::optional<std::string> getSafeFilePath(const std::string& fileName) const;

    void clear();

private:
    std::string m_rootSharedPath;
    std::vector<FileMetadata> m_sharedFiles;
    uint64_t m_totalSharedBytes{0};
    bool m_isReadOnly{false};
    std::string m_accessPasswordHash;
    mutable uint32_t m_failedAttempts{0};
    size_t m_maxFilesLimit{1000};
    uint64_t m_maxFileSizeLimit{10ULL * 1024 * 1024 * 1024}; // 10 GB default

    void recalculateTotalBytes();
};

}
