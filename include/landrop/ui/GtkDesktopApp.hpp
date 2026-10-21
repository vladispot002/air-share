#pragma once

#include <gtk/gtk.h>
#include <string>
#include <vector>
#include <memory>
#include "landrop/ui/IView.hpp"
#include "landrop/ui/DiscoveryPresenter.hpp"
#include "landrop/ui/TransferPresenter.hpp"
#include "landrop/ui/StoragePresenter.hpp"
#include "landrop/ui/SettingsPresenter.hpp"

namespace landrop::ui {

class GtkDesktopApp : public IDiscoveryView,
                      public ITransferView,
                      public IStorageView,
                      public ISettingsView {
public:
    GtkDesktopApp();
    ~GtkDesktopApp() override;

    int run(int argc, char* argv[]);

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

    void toggleTheme(bool isDark);

    DiscoveryPresenter& getDiscoveryPresenter() { return m_discoveryPresenter; }
    TransferPresenter& getTransferPresenter() { return m_transferPresenter; }
    StoragePresenter& getStoragePresenter() { return m_storagePresenter; }
    SettingsPresenter& getSettingsPresenter() { return m_settingsPresenter; }

private:
    DiscoveryPresenter m_discoveryPresenter;
    TransferPresenter m_transferPresenter;
    StoragePresenter m_storagePresenter;
    SettingsPresenter m_settingsPresenter;

    bool m_isDarkMode{true};
    bool m_usingWebKit{false};

    GtkWidget* m_mainWindow{nullptr};
    GtkWidget* m_headerBar{nullptr};
    GtkWidget* m_themeSwitch{nullptr};
    GtkWidget* m_statusLabel{nullptr};

    GtkWidget* m_stack{nullptr};
    GtkWidget* m_stackSwitcher{nullptr};

    GtkWidget* m_deviceListBox{nullptr};
    GtkWidget* m_scanStatusLabel{nullptr};
    GtkWidget* m_nfcEntry{nullptr};

    GtkWidget* m_transferTreeView{nullptr};
    GtkListStore* m_transferListStore{nullptr};
    GtkWidget* m_selectedTransferLabel{nullptr};

    GtkWidget* m_storagePathEntry{nullptr};
    GtkWidget* m_storageStatsLabel{nullptr};
    GtkWidget* m_fileTreeView{nullptr};
    GtkListStore* m_fileListStore{nullptr};
    GtkWidget* m_passwordEntry{nullptr};

    GtkWidget* m_portSpin{nullptr};
    GtkWidget* m_speedScale{nullptr};
    GtkWidget* m_autoAcceptSwitch{nullptr};
    GtkWidget* m_airdropSwitch{nullptr};
    GtkWidget* m_logTextView{nullptr};
    GtkTextBuffer* m_logBuffer{nullptr};

    GtkCssProvider* m_cssProvider{nullptr};

    void buildUi();
    GtkWidget* createDiscoveryScreen();
    GtkWidget* createTransfersScreen();
    GtkWidget* createStorageScreen();
    GtkWidget* createSettingsScreen();
    void applyAppleTheme();
};

}
