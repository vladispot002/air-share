#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QListWidget>
#include <QStatusBar>
#include <memory>

#include "landrop/ui/DiscoveryPresenter.hpp"
#include "landrop/ui/TransferPresenter.hpp"
#include "landrop/ui/StoragePresenter.hpp"
#include "landrop/ui/SettingsPresenter.hpp"

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

public slots:
    void applyTheme(const QString& themeName);

private slots:
    void onNavigationItemChanged(int row);

private:
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

    void setupUi();
    void setupPresenters();
};

}
