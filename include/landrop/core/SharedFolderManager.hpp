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

    void setMaxFilesLimit(size_t limit) { m_maxFilesLimit = limit; }
    size_t getMaxFilesLimit() const { return m_maxFilesLimit; }

    void clear();

private:
    std::string m_rootSharedPath;
    std::vector<FileMetadata> m_sharedFiles;
    uint64_t m_totalSharedBytes{0};
    bool m_isReadOnly{false};
    std::string m_accessPasswordHash;
    size_t m_maxFilesLimit{1000};

    void recalculateTotalBytes();
};

}
