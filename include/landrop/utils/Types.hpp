#pragma once

#include <string>
#include <string_view>
#include <cctype>
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
    // Leading/trailing dots and spaces are dangerous across Win32 / POSIX
    if (fileName.front() == '.' || fileName.back() == '.' ||
        fileName.front() == ' ' || fileName.back() == ' ') {
        return false;
    }
    // Path traversal sequence
    if (fileName.find("..") != std::string::npos) {
        return false;
    }

    // Invalid and dangerous characters
    // Slashes: forward slash and backslash
    // Windows ADS stream delimiter: :
    // Win32 forbidden characters: < > " | ? *
    // Control characters: 0x00-0x1F, 0x7F
    for (char c : fileName) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 32 || uc == 127 ||
            c == '/' || c == '\\' || c == ':' ||
            c == '<' || c == '>' || c == '"' ||
            c == '|' || c == '?' || c == '*') {
            return false;
        }
    }

    // Unicode bidirectional override / embedding spoofing characters:
    // U+202A to U+202E: UTF-8 "\xE2\x80\xAA" through "\xE2\x80\xAE" (LRE, RLE, PDF, LRO, RTLO)
    // U+2066 to U+2069: UTF-8 "\xE2\x81\xA6" through "\xE2\x81\xA9" (LRI, RLI, FSI, PDI)
    // Zero-width characters: U+200B (\xE2\x80\x8B), U+200E (\xE2\x80\x8E), U+200F (\xE2\x80\x8F)
    const std::string_view fsv(fileName);
    const std::string_view bidiOverrides[] = {
        "\xE2\x80\xAA", "\xE2\x80\xAB", "\xE2\x80\xAC", "\xE2\x80\xAD", "\xE2\x80\xAE",
        "\xE2\x81\xA6", "\xE2\x81\xA7", "\xE2\x81\xA8", "\xE2\x81\xA9",
        "\xE2\x80\x8B", "\xE2\x80\x8E", "\xE2\x80\x8F"
    };
    for (const auto& seq : bidiOverrides) {
        if (fsv.find(seq) != std::string_view::npos) {
            return false;
        }
    }

    // Windows reserved device names:
    // CON, PRN, AUX, NUL, COM1-9, LPT1-9 (case-insensitive, with or without extensions)
    size_t firstDot = fileName.find('.');
    std::string stem = (firstDot == std::string::npos) ? fileName : fileName.substr(0, firstDot);
    std::string upperStem;
    upperStem.reserve(stem.size());
    for (char ch : stem) {
        upperStem.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }

    if (upperStem == "CON" || upperStem == "PRN" || upperStem == "AUX" || upperStem == "NUL") {
        return false;
    }
    if (upperStem.size() == 4 &&
        (upperStem.rfind("COM", 0) == 0 || upperStem.rfind("LPT", 0) == 0)) {
        char digit = upperStem[3];
        if (digit >= '1' && digit <= '9') {
            return false;
        }
    }

    return true;
}

inline bool isSafeRelativePath(const std::string& relPath) {
    if (relPath.empty() || relPath.size() > 1024) {
        return false;
    }
    // Cannot start with slash, backslash, dot, or space
    if (relPath.front() == '/' || relPath.front() == '\\' || relPath.front() == '.' || relPath.front() == ' ') {
        return false;
    }
    // Cannot end with slash, backslash, dot, or space
    if (relPath.back() == '/' || relPath.back() == '\\' || relPath.back() == '.' || relPath.back() == ' ') {
        return false;
    }
    // Cannot contain directory traversal sequence
    if (relPath.find("..") != std::string::npos) {
        return false;
    }
    // Cannot contain Windows drive or stream colons
    if (relPath.find(':') != std::string::npos) {
        return false;
    }

    // Split by '/' or '\\' and ensure every segment is a safe filename
    std::string seg;
    for (char c : relPath) {
        if (c == '/' || c == '\\') {
            if (seg.empty() || !isSafeFileName(seg)) {
                return false;
            }
            seg.clear();
        } else {
            seg.push_back(c);
        }
    }
    if (seg.empty() || !isSafeFileName(seg)) {
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

