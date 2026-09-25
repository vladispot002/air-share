#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include "landrop/utils/Types.hpp"

namespace landrop {

class FileChunkHasher {
public:
    explicit FileChunkHasher(size_t defaultChunkSize = 64 * 1024);
    ~FileChunkHasher() = default;

    std::string computeChunkHash(const uint8_t* data, size_t size);
    std::string computeChunkHash(const std::vector<uint8_t>& data);
    std::string computeStringHash(const std::string& input);

    bool verifyChunk(const std::vector<uint8_t>& data, const std::string& expectedHash);
    
    std::vector<ChunkInfo> partitionAndHashFile(const std::string& filePath, size_t chunkSize = 0);
    std::optional<std::string> computeFullFileHash(const std::string& filePath);

    std::vector<size_t> findCorruptedChunks(
        const std::vector<ChunkInfo>& receivedChunks,
        const std::vector<std::string>& targetHashes
    ) const;

    bool validateIntegrity(
        const std::vector<ChunkInfo>& receivedChunks,
        const std::vector<std::string>& targetHashes
    ) const;

    size_t getDefaultChunkSize() const { return m_defaultChunkSize; }
    void setDefaultChunkSize(size_t size) { m_defaultChunkSize = size; }
    
    size_t getTotalChunksHashed() const { return m_totalChunksHashed; }
    uint64_t getTotalBytesHashed() const { return m_totalBytesHashed; }
    const std::string& getAlgorithmName() const { return m_algorithmName; }

    void resetStats();

private:
    std::string m_algorithmName{"SHA-256"};
    size_t m_defaultChunkSize;
    size_t m_totalChunksHashed{0};
    uint64_t m_totalBytesHashed{0};
    std::vector<std::string> m_cachedHashes;
    bool m_strictValidationEnabled{true};
};

}
