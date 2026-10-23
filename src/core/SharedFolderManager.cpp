#include "landrop/core/SharedFolderManager.hpp"
#include "landrop/core/FileChunkHasher.hpp"
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

namespace landrop {

static std::string bytesToHex(const uint8_t* data, size_t len) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        ss << std::setw(2) << static_cast<int>(data[i]);
    }
    return ss.str();
}

static std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    if (hex.size() % 2 != 0) return bytes;
    for (size_t i = 0; i < hex.size(); i += 2) {
        try {
            uint8_t byte = static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16));
            bytes.push_back(byte);
        } catch (...) {
            return {};
        }
    }
    return bytes;
}

SharedFolderManager::SharedFolderManager(std::string rootPath)
    : m_rootSharedPath(std::move(rootPath)) {
    std::error_code ec;
    std::filesystem::create_directories(m_rootSharedPath, ec);
}

bool SharedFolderManager::setRootPath(const std::string& newPath) {
    std::error_code ec;
    if (std::filesystem::exists(newPath, ec) || std::filesystem::create_directories(newPath, ec)) {
        m_rootSharedPath = newPath;
        scanDirectory();
        return true;
    }
    return false;
}

size_t SharedFolderManager::scanDirectory() {
    m_sharedFiles.clear();
    m_totalSharedBytes = 0;

    try {
        std::error_code rootEc;
        if (!std::filesystem::exists(m_rootSharedPath, rootEc) || rootEc) {
            return 0;
        }

        std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(m_rootSharedPath, rootEc);
        if (rootEc) {
            rootEc.clear();
            canonicalRoot = std::filesystem::absolute(m_rootSharedPath, rootEc);
        }

        FileChunkHasher hasher;
        std::error_code iterEc;
        auto iter = std::filesystem::directory_iterator(
            m_rootSharedPath,
            std::filesystem::directory_options::skip_permission_denied,
            iterEc
        );

        if (iterEc) {
            return 0;
        }

        while (!iterEc && iter != std::filesystem::directory_iterator()) {
            std::error_code entryEc;
            const auto& entry = *iter;

            // Security check: Ignore symlinks pointing outside the shared directory or dangling
            bool isSym = entry.is_symlink(entryEc);
            if (entryEc) {
                entryEc.clear();
                iter.increment(iterEc);
                continue;
            }

            if (isSym) {
                auto symTarget = std::filesystem::weakly_canonical(entry.path(), entryEc);
                if (entryEc) {
                    entryEc.clear();
                    iter.increment(iterEc);
                    continue;
                }
                auto rel = std::filesystem::relative(symTarget, canonicalRoot, entryEc);
                if (entryEc || rel.empty() || rel.string().rfind("..", 0) == 0 || symTarget == canonicalRoot) {
                    iter.increment(iterEc);
                    continue;
                }
            }

            bool isReg = entry.is_regular_file(entryEc);
            if (entryEc || !isReg) {
                iter.increment(iterEc);
                continue;
            }

            if (m_sharedFiles.size() < m_maxFilesLimit) {
                uint64_t fsize = entry.file_size(entryEc);
                if (entryEc || fsize > m_maxFileSizeLimit) {
                    iter.increment(iterEc);
                    continue;
                }

                FileMetadata meta;
                meta.fileName = entry.path().filename().string();
                meta.fileSize = fsize;
                meta.chunkSize = 64 * 1024;
                meta.totalChunks = (meta.fileSize + meta.chunkSize - 1) / meta.chunkSize;
                
                auto fullHash = hasher.computeFullFileHash(entry.path().string());
                meta.fileSha256 = fullHash.value_or("");

                m_totalSharedBytes += meta.fileSize;
                m_sharedFiles.push_back(std::move(meta));
            }

            iter.increment(iterEc);
        }
    } catch (...) {
        return m_sharedFiles.size();
    }
    return m_sharedFiles.size();
}

bool SharedFolderManager::addSharedFile(const std::string& filePath) {
    std::error_code ec;
    std::filesystem::path path(filePath);
    if (!std::filesystem::exists(path, ec) || !std::filesystem::is_regular_file(path, ec)) {
        return false;
    }

    if (m_sharedFiles.size() >= m_maxFilesLimit) {
        return false;
    }

    // Security check: If path is a symlink, verify it doesn't escape allowed root
    std::error_code symEc;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path, symEc))) {
        std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(m_rootSharedPath, symEc);
        std::filesystem::path canonicalTarget = std::filesystem::weakly_canonical(path, symEc);
        auto rel = std::filesystem::relative(canonicalTarget, canonicalRoot, symEc);
        if (symEc || rel.empty() || rel.string().rfind("..", 0) == 0) {
            return false;
        }
    }

    uint64_t fsize = std::filesystem::file_size(path, ec);
    if (ec || fsize > m_maxFileSizeLimit) {
        return false;
    }

    FileChunkHasher hasher;
    FileMetadata meta;
    meta.fileName = path.filename().string();
    meta.fileSize = fsize;
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
    if (fileName.empty()) {
        return std::nullopt;
    }
    // Prevent path traversal
    std::filesystem::path p(fileName);
    if (p.filename().string() != fileName || fileName == "." || fileName == "..") {
        return std::nullopt;
    }
    for (const auto& meta : m_sharedFiles) {
        if (meta.fileName == fileName) {
            return meta;
        }
    }
    return std::nullopt;
}

std::optional<std::string> SharedFolderManager::getSafeFilePath(const std::string& fileName) const {
    auto fileOpt = findFile(fileName);
    if (!fileOpt) {
        return std::nullopt;
    }
    std::error_code ec;
    std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(m_rootSharedPath, ec);
    std::filesystem::path fullPath = canonicalRoot / fileOpt->fileName;
    std::filesystem::path canonicalFull = std::filesystem::weakly_canonical(fullPath, ec);
    auto rel = std::filesystem::relative(canonicalFull, canonicalRoot, ec);
    if (ec || rel.empty() || rel.string().rfind("..", 0) == 0 || canonicalFull == canonicalRoot) {
        return std::nullopt;
    }
    return canonicalFull.string();
}

void SharedFolderManager::setAccessPassword(const std::string& password) {
    if (password.empty()) {
        m_accessPasswordHash.clear();
        m_failedAttempts = 0;
        return;
    }

    constexpr int kIterations = 100000;
    std::vector<uint8_t> salt(16);
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
        for (size_t i = 0; i < salt.size(); ++i) {
            salt[i] = static_cast<uint8_t>((std::chrono::system_clock::now().time_since_epoch().count() + i * 37) & 0xFF);
        }
    }

    std::vector<uint8_t> derivedKey(32);
    if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                          salt.data(), static_cast<int>(salt.size()),
                          kIterations, EVP_sha256(),
                          static_cast<int>(derivedKey.size()), derivedKey.data()) == 1) {
        m_accessPasswordHash = "$pbkdf2-sha256$i=" + std::to_string(kIterations) + "$" +
                               bytesToHex(salt.data(), salt.size()) + "$" +
                               bytesToHex(derivedKey.data(), derivedKey.size());
        m_failedAttempts = 0;
    } else {
        FileChunkHasher hasher;
        m_accessPasswordHash = hasher.computeStringHash(password);
        m_failedAttempts = 0;
    }
}

bool SharedFolderManager::verifyAccessPassword(const std::string& password) const {
    if (m_accessPasswordHash.empty()) {
        return true;
    }
    if (password.empty()) {
        m_failedAttempts++;
        return false;
    }

    // Rate-limiting delay on repeated failures (progressively up to 500ms)
    if (m_failedAttempts > 0) {
        int delayMs = std::min<int>(static_cast<int>(m_failedAttempts) * 100, 500);
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }

    // Check PBKDF2 format: "$pbkdf2-sha256$i=<iter>$<salt>$<key>"
    if (m_accessPasswordHash.rfind("$pbkdf2-sha256$", 0) == 0) {
        size_t first = m_accessPasswordHash.find('$', 15);
        size_t second = (first != std::string::npos) ? m_accessPasswordHash.find('$', first + 1) : std::string::npos;

        if (first != std::string::npos && second != std::string::npos) {
            std::string iterPart = m_accessPasswordHash.substr(15, first - 15);
            int iter = 100000;
            if (iterPart.rfind("i=", 0) == 0) {
                try { iter = std::stoi(iterPart.substr(2)); } catch (...) {}
            }

            std::string saltHex = m_accessPasswordHash.substr(first + 1, second - first - 1);
            std::string keyHex = m_accessPasswordHash.substr(second + 1);

            auto salt = hexToBytes(saltHex);
            auto expectedKey = hexToBytes(keyHex);

            if (!salt.empty() && expectedKey.size() == 32) {
                std::vector<uint8_t> computedKey(32);
                if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                                      salt.data(), static_cast<int>(salt.size()),
                                      iter, EVP_sha256(),
                                      static_cast<int>(computedKey.size()), computedKey.data()) == 1) {
                    if (CRYPTO_memcmp(computedKey.data(), expectedKey.data(), 32) == 0) {
                        m_failedAttempts = 0;
                        return true;
                    }
                }
            }
        }
        m_failedAttempts++;
        return false;
    }

    // Legacy 64-char plain SHA-256 hash compatibility and auto-migration
    if (m_accessPasswordHash.size() == 64) {
        FileChunkHasher hasher;
        if (hasher.computeStringHash(password) == m_accessPasswordHash) {
            m_failedAttempts = 0;
            const_cast<SharedFolderManager*>(this)->setAccessPassword(password);
            return true;
        }
    }

    m_failedAttempts++;
    return false;
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
