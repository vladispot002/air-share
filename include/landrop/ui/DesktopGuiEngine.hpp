#pragma once

#include <string>
#include <vector>
#include <memory>
#include "landrop/ui/IView.hpp"
#include "landrop/ui/DiscoveryPresenter.hpp"
#include "landrop/ui/TransferPresenter.hpp"
#include "landrop/ui/StoragePresenter.hpp"
#include "landrop/ui/SettingsPresenter.hpp"

namespace landrop::ui {

enum class GuiScreen {
    Discovery,
    Transfers,
    Storage,
    Settings
};

class DesktopGuiEngine : public IDiscoveryView,
                         public ITransferView,
                         public IStorageView,
                         public ISettingsView {
public:
    DesktopGuiEngine();
    ~DesktopGuiEngine() override;

    bool initialize(int width = 960, int height = 640, const std::string& title = "LAN-Drop Desktop Manager");
    void run();
    void stop();

    void updateDeviceList(const std::vector<DeviceViewModel>& devices) override;
    void setScanningStatus(bool isScanning) override;
    void showNfcPairResult(bool success, const std::string& message) override;
    void showNotification(const std::string& text) override;

    void updateTransferTable(const std::vector<TransferViewModel>& transfers) override;
    void updateSelectedTransferDetails(const TransferViewModel& item) override;
    void showTransferError(const std::string& taskId, const std::string& error) override;

    void setSharedPath(const std::string& path) override;
    void updateFileList(const std::vector<FileMetadata>& files) override;
    void setStorageStats(uint64_t totalBytes, size_t fileCount) override;
    void setPasswordProtectionStatus(bool isProtected) override;

    void loadSettings(uint16_t port, uint32_t speedLimitKbps, bool autoAccept, bool airdropEnabled) override;
    void appendDiagnosticLog(const std::string& logLine) override;
    void showSaveSuccess() override;

    void switchScreen(GuiScreen screen);
    GuiScreen getCurrentScreen() const { return m_currentScreen; }

    void triggerAction(int actionCode);
    void tick();

private:
    int m_width{960};
    int m_height{640};
    std::string m_title;
    bool m_isRunning{false};
    GuiScreen m_currentScreen{GuiScreen::Discovery};

    DiscoveryPresenter m_discoveryPresenter;
    TransferPresenter m_transferPresenter;
    StoragePresenter m_storagePresenter;
    SettingsPresenter m_settingsPresenter;

    std::vector<DeviceViewModel> m_deviceList;
    bool m_isScanning{false};
    std::string m_lastNotification;

    std::vector<TransferViewModel> m_transferList;
    TransferViewModel m_selectedTransfer;

    std::string m_sharedPath;
    std::vector<FileMetadata> m_fileList;
    uint64_t m_sharedTotalBytes{0};
    size_t m_sharedFileCount{0};
    bool m_isPasswordProtected{false};

    uint16_t m_settingsPort{8848};
    uint32_t m_settingsSpeedLimit{0};
    bool m_settingsAutoAccept{true};
    bool m_settingsAirDropEnabled{false};
    std::vector<std::string> m_diagnosticLogs;

    void* m_displayPtr{nullptr};
    unsigned long m_windowId{0};
    void* m_gcPtr{nullptr};

    bool initX11();
    void handleX11Events();
    void renderScreen();
    void closeX11();
};

}
