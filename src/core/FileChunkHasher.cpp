#include "landrop/core/FileChunkHasher.hpp"
#include "landrop/utils/CryptoFallback.hpp"
#include <fstream>
#include <filesystem>

namespace landrop {

FileChunkHasher::FileChunkHasher(size_t defaultChunkSize)
    : m_defaultChunkSize(defaultChunkSize) {}

std::string FileChunkHasher::computeChunkHash(const uint8_t* data, size_t size) {
    m_totalBytesHashed += size;
    ++m_totalChunksHashed;
    return crypto::Sha256::hash(data, size);
}

std::string FileChunkHasher::computeChunkHash(const std::vector<uint8_t>& data) {
    return computeChunkHash(data.data(), data.size());
}

std::string FileChunkHasher::computeStringHash(const std::string& input) {
    return crypto::Sha256::hash(input);
}

bool FileChunkHasher::verifyChunk(const std::vector<uint8_t>& data, const std::string& expectedHash) {
    if (expectedHash.empty()) {
        return false;
    }
    std::string actual = computeChunkHash(data);
    return actual == expectedHash;
}

std::vector<ChunkInfo> FileChunkHasher::partitionAndHashFile(const std::string& filePath, size_t chunkSize) {
    if (chunkSize == 0) {
        chunkSize = m_defaultChunkSize;
    }

    std::vector<ChunkInfo> result;
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return result;
    }

    std::vector<uint8_t> buffer(chunkSize);
    uint64_t offset = 0;
    size_t index = 0;

    while (file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(chunkSize)) || file.gcount() > 0) {
        size_t bytesRead = static_cast<size_t>(file.gcount());
        ChunkInfo info;
        info.index = index++;
        info.offset = offset;
        info.size = bytesRead;
        info.sha256Hash = crypto::Sha256::hash(buffer.data(), bytesRead);
        info.isCompleted = true;
        info.isVerified = true;

        offset += bytesRead;
        m_totalBytesHashed += bytesRead;
        ++m_totalChunksHashed;
        result.push_back(std::move(info));

        if (bytesRead < chunkSize) {
            break;
        }
    }

    return result;
}

std::optional<std::string> FileChunkHasher::computeFullFileHash(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return std::nullopt;
    }

    crypto::Sha256 sha;
    std::vector<uint8_t> buffer(64 * 1024);
    while (file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size())) || file.gcount() > 0) {
        size_t bytes = static_cast<size_t>(file.gcount());
        sha.update(buffer.data(), bytes);
        m_totalBytesHashed += bytes;
        if (bytes < buffer.size()) {
            break;
        }
    }
    return sha.finalize();
}

std::vector<size_t> FileChunkHasher::findCorruptedChunks(
    const std::vector<ChunkInfo>& receivedChunks,
    const std::vector<std::string>& targetHashes
) const {
    std::vector<size_t> corrupted;
    size_t count = std::min(receivedChunks.size(), targetHashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (receivedChunks[i].sha256Hash != targetHashes[i]) {
            corrupted.push_back(i);
        }
    }
    for (size_t i = count; i < targetHashes.size(); ++i) {
        corrupted.push_back(i);
    }
    return corrupted;
}

bool FileChunkHasher::validateIntegrity(
    const std::vector<ChunkInfo>& receivedChunks,
    const std::vector<std::string>& targetHashes
) const {
    if (receivedChunks.size() != targetHashes.size()) {
        return false;
    }
    for (size_t i = 0; i < targetHashes.size(); ++i) {
        if (receivedChunks[i].sha256Hash != targetHashes[i]) {
            return false;
        }
    }
    return true;
}

void FileChunkHasher::resetStats() {
    m_totalChunksHashed = 0;
    m_totalBytesHashed = 0;
    m_cachedHashes.clear();
}

}
