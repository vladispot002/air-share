#include "landrop/core/SharedFolderManager.hpp"
#include "landrop/core/FileChunkHasher.hpp"
#include <filesystem>
#include <algorithm>

namespace landrop {

SharedFolderManager::SharedFolderManager(std::string rootPath)
    : m_rootSharedPath(std::move(rootPath)) {
    std::filesystem::create_directories(m_rootSharedPath);
}

bool SharedFolderManager::setRootPath(const std::string& newPath) {
    if (std::filesystem::exists(newPath) || std::filesystem::create_directories(newPath)) {
        m_rootSharedPath = newPath;
        scanDirectory();
        return true;
    }
    return false;
}

size_t SharedFolderManager::scanDirectory() {
    m_sharedFiles.clear();
    m_totalSharedBytes = 0;

    if (!std::filesystem::exists(m_rootSharedPath)) {
        return 0;
    }

    FileChunkHasher hasher;
    for (const auto& entry : std::filesystem::directory_iterator(m_rootSharedPath)) {
        if (entry.is_regular_file() && m_sharedFiles.size() < m_maxFilesLimit) {
            FileMetadata meta;
            meta.fileName = entry.path().filename().string();
            meta.fileSize = entry.file_size();
            meta.chunkSize = 64 * 1024;
            meta.totalChunks = (meta.fileSize + meta.chunkSize - 1) / meta.chunkSize;
            
            auto fullHash = hasher.computeFullFileHash(entry.path().string());
            meta.fileSha256 = fullHash.value_or("");

            m_totalSharedBytes += meta.fileSize;
            m_sharedFiles.push_back(std::move(meta));
        }
    }
    return m_sharedFiles.size();
}

bool SharedFolderManager::addSharedFile(const std::string& filePath) {
    std::filesystem::path path(filePath);
    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
        return false;
    }

    if (m_sharedFiles.size() >= m_maxFilesLimit) {
        return false;
    }

    FileChunkHasher hasher;
    FileMetadata meta;
    meta.fileName = path.filename().string();
    meta.fileSize = std::filesystem::file_size(path);
    meta.chunkSize = 64 * 1024;
    meta.totalChunks = (meta.fileSize + meta.chunkSize - 1) / meta.chunkSize;
    auto fullHash = hasher.computeFullFileHash(path.string());
    meta.fileSha256 = fullHash.value_or("");

    m_totalSharedBytes += meta.fileSize;
    m_sharedFiles.push_back(std::move(meta));
    return true;
}

bool SharedFolderManager::removeSharedFile(const std::string& fileName) {
    auto it = std::remove_if(m_sharedFiles.begin(), m_sharedFiles.end(),
        [&fileName](const FileMetadata& meta) {
            return meta.fileName == fileName;
        });

    if (it != m_sharedFiles.end()) {
        m_sharedFiles.erase(it, m_sharedFiles.end());
        recalculateTotalBytes();
        return true;
    }
    return false;
}

std::optional<FileMetadata> SharedFolderManager::findFile(const std::string& fileName) const {
    for (const auto& meta : m_sharedFiles) {
        if (meta.fileName == fileName) {
            return meta;
        }
    }
    return std::nullopt;
}

void SharedFolderManager::setAccessPassword(const std::string& password) {
    if (password.empty()) {
        m_accessPasswordHash.clear();
    } else {
        FileChunkHasher hasher;
        m_accessPasswordHash = hasher.computeStringHash(password);
    }
}

bool SharedFolderManager::verifyAccessPassword(const std::string& password) const {
    if (m_accessPasswordHash.empty()) {
        return true;
    }
    FileChunkHasher hasher;
    return hasher.computeStringHash(password) == m_accessPasswordHash;
}

void SharedFolderManager::clear() {
    m_sharedFiles.clear();
    m_totalSharedBytes = 0;
}

void SharedFolderManager::recalculateTotalBytes() {
    m_totalSharedBytes = 0;
    for (const auto& f : m_sharedFiles) {
        m_totalSharedBytes += f.fileSize;
    }
}

}
