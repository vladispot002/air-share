#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QListWidget>
#include <QStatusBar>
#include <QTimer>
#include <memory>
#include <map>

#include "landrop/ui/DiscoveryPresenter.hpp"
#include "landrop/ui/TransferPresenter.hpp"
#include "landrop/ui/StoragePresenter.hpp"
#include "landrop/ui/SettingsPresenter.hpp"
#include "landrop/core/TransferEngine.hpp"
#include "landrop/core/WebRtcSessionCoordinator.hpp"

namespace landrop::ui {

class QtDiscoveryWidget;
class QtTransferWidget;
class QtStorageWidget;
class QtSettingsWidget;

class QtDesktopApp : public QMainWindow {
    Q_OBJECT
public:
    explicit QtDesktopApp(QWidget* parent = nullptr);
    ~QtDesktopApp() override;

    void initialize();

    QListWidget* getSidebar() const { return m_sidebar; }
    QStackedWidget* getStackedWidget() const { return m_stackedWidget; }
    QtDiscoveryWidget* getDiscoveryWidget() const { return m_discoveryWidget; }
    QtTransferWidget* getTransferWidget() const { return m_transferWidget; }
    QtStorageWidget* getStorageWidget() const { return m_storageWidget; }
    QtSettingsWidget* getSettingsWidget() const { return m_settingsWidget; }
    DiscoveryPresenter* getDiscoveryPresenter() const { return m_discoveryPresenter.get(); }
    TransferPresenter* getTransferPresenter() const { return m_transferPresenter.get(); }
    StoragePresenter* getStoragePresenter() const { return m_storagePresenter.get(); }
    SettingsPresenter* getSettingsPresenter() const { return m_settingsPresenter.get(); }
    TransferEngine* getTransferEngine() const { return m_transferEngine.get(); }
    QTimer* getTransferTickTimer() const { return m_transferTickTimer; }
    std::string getDownloadsDirectory() const {
        std::lock_guard<std::mutex> lock(m_settingsMutex);
        return m_downloadsDirectory;
    }
    void setDownloadsDirectory(const std::string& dir) {
        std::lock_guard<std::mutex> lock(m_settingsMutex);
        m_downloadsDirectory = dir;
    }

    void startBackgroundTransferServer();
    void handleSendFile(const std::string& taskId, const std::string& targetPeerId, const std::string& filePath);
    void launchWorker(std::function<void()> task);

public slots:
    void applyTheme(const QString& themeName);
    void onHostRoomRequested(const QString& filePath = QString());
    void onJoinRoomRequested(const QString& roomCode);

private slots:
    void onNavigationItemChanged(int row);

private:
    std::shared_ptr<std::atomic<bool>> m_aliveToken{std::make_shared<std::atomic<bool>>(true)};
    mutable std::mutex m_workerThreadsMutex;
    std::vector<std::thread> m_workerThreads;

    mutable std::mutex m_coordinatorsMutex;
    std::map<std::string, std::shared_ptr<WebRtcSessionCoordinator>> m_globalCoordinators;
    QListWidget* m_sidebar{nullptr};
    QStackedWidget* m_stackedWidget{nullptr};

    QtDiscoveryWidget* m_discoveryWidget{nullptr};
    QtTransferWidget* m_transferWidget{nullptr};
    QtStorageWidget* m_storageWidget{nullptr};
    QtSettingsWidget* m_settingsWidget{nullptr};

    std::unique_ptr<DiscoveryPresenter> m_discoveryPresenter;
    std::unique_ptr<TransferPresenter> m_transferPresenter;
    std::unique_ptr<StoragePresenter> m_storagePresenter;
    std::unique_ptr<SettingsPresenter> m_settingsPresenter;
    std::unique_ptr<TransferEngine> m_transferEngine;

    mutable std::mutex m_settingsMutex;
    std::string m_downloadsDirectory;
    QTimer* m_transferTickTimer{nullptr};

    void setupUi();
    void setupPresenters();
};

}
