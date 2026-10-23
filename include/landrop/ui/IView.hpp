#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "landrop/utils/Types.hpp"

namespace landrop::ui {

struct DeviceViewModel {
    std::string id;
    std::string name;
    std::string endpoint;
    std::string type;
    bool isOnline{false};
    int signalRssi{-50};
};

struct TransferViewModel {
    std::string taskId;
    std::string fileName;
    uint64_t fileSize{0};
    uint64_t bytesTransferred{0};
    double progress{0.0};
    double speedBytesPerSec{0.0};
    std::string stateStr;
    std::string direction;
};

class IDiscoveryView {
public:
    virtual ~IDiscoveryView() = default;
    virtual void updateDeviceList(const std::vector<DeviceViewModel>& devices) = 0;
    virtual void setScanningStatus(bool isScanning) = 0;
    virtual void showNfcPairResult(bool success, const std::string& message) = 0;
    virtual void showNotification(const std::string& text) = 0;
};

class ITransferView {
public:
    virtual ~ITransferView() = default;
    virtual void updateTransferTable(const std::vector<TransferViewModel>& transfers) = 0;
    virtual void updateSelectedTransferDetails(const TransferViewModel& item) = 0;
    virtual void showTransferError(const std::string& taskId, const std::string& error) = 0;
};

class IStorageView {
public:
    virtual ~IStorageView() = default;
    virtual void setSharedPath(const std::string& path) = 0;
    virtual void updateFileList(const std::vector<FileMetadata>& files) = 0;
    virtual void setStorageStats(uint64_t totalBytes, size_t fileCount) = 0;
    virtual void setPasswordProtectionStatus(bool isProtected) = 0;
};

class ISettingsView {
public:
    virtual ~ISettingsView() = default;
    virtual void loadSettings(uint16_t port, uint32_t speedLimitKbps, bool autoAccept, bool airdropEnabled) = 0;
    virtual void appendDiagnosticLog(const std::string& logLine) = 0;
    virtual void showSaveSuccess() = 0;
    virtual void setDownloadsPath(const std::string& /*path*/) {}
};

}
