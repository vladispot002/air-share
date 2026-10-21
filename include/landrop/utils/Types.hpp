#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <chrono>
#include <iostream>

namespace landrop {

enum class PeerType {
    Local,
    Cloud
};

enum class TransferState {
    Idle,
    Connecting,
    Transferring,
    Paused,
    Completed,
    Failed,
    Cancelled
};

enum class TransferDirection {
    Upload,
    Download
};

enum class ConnectionState {
    Disconnected,
    Handshake,
    Connected,
    Terminated
};

struct ChunkInfo {
    size_t index{0};
    uint64_t offset{0};
    size_t size{0};
    std::string sha256Hash;
    bool isCompleted{false};
    bool isVerified{false};
};

struct FileMetadata {
    std::string fileName;
    uint64_t fileSize{0};
    std::string fileSha256;
    size_t chunkSize{64 * 1024};
    size_t totalChunks{0};
    std::string mimeType{"application/octet-stream"};
};

inline std::string transferStateToString(TransferState state) {
    switch (state) {
        case TransferState::Idle: return "Idle";
        case TransferState::Connecting: return "Connecting";
        case TransferState::Transferring: return "Transferring";
        case TransferState::Paused: return "Paused";
        case TransferState::Completed: return "Completed";
        case TransferState::Failed: return "Failed";
        case TransferState::Cancelled: return "Cancelled";
        default: return "Unknown";
    }
}

inline std::string connectionStateToString(ConnectionState state) {
    switch (state) {
        case ConnectionState::Disconnected: return "Disconnected";
        case ConnectionState::Handshake: return "Handshake";
        case ConnectionState::Connected: return "Connected";
        case ConnectionState::Terminated: return "Terminated";
        default: return "Unknown";
    }
}

inline bool isSafeFileName(const std::string& fileName) {
    if (fileName.empty() || fileName.size() > 255) {
        return false;
    }
    if (fileName.front() == '.') {
        return false;
    }
    for (char c : fileName) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 32 || uc == 127 || c == '/' || c == '\\') {
            return false;
        }
    }
    if (fileName.find("..") != std::string::npos) {
        return false;
    }
    return true;
}

inline bool isValidFileSize(uint64_t fileSize, uint64_t maxFileSize = 100ULL * 1024 * 1024 * 1024) {
    return fileSize > 0 && fileSize <= maxFileSize;
}

inline bool isValidChunkSize(
    uint32_t chunkSize,
    uint64_t fileSize,
    uint64_t totalReceived,
    uint32_t maxChunkSize = 4 * 1024 * 1024
) {
    if (chunkSize == 0 || chunkSize > maxChunkSize) {
        return false;
    }
    if (totalReceived > fileSize) {
        return false;
    }
    uint64_t remaining = fileSize - totalReceived;
    if (static_cast<uint64_t>(chunkSize) > remaining) {
        return false;
    }
    return true;
}

}

