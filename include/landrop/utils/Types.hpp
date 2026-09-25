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

}
