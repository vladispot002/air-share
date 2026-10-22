#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <vector>
#include "landrop/ui/IView.hpp"

namespace landrop::ui {

class DiscoveryPresenter;

class QtDiscoveryWidget : public QWidget, public IDiscoveryView {
    Q_OBJECT
public:
    explicit QtDiscoveryWidget(QWidget* parent = nullptr);
    ~QtDiscoveryWidget() override = default;

    void setPresenter(DiscoveryPresenter* presenter);

    // IDiscoveryView overrides
    void updateDeviceList(const std::vector<DeviceViewModel>& devices) override;
    void setScanningStatus(bool isScanning) override;
    void showNfcPairResult(bool success, const std::string& message) override;
    void showNotification(const std::string& text) override;

signals:
    void sendFileRequested(const QString& peerId, const QString& filePath);

private slots:
    void onRefreshClicked();
    void onToggleScanClicked();
    void onDirectConnectClicked();
    void onAddLoopbackClicked();
    void onDeviceSendClicked(const QString& peerId);
    void onDeviceRemoveClicked(const QString& peerId);

private:
    DiscoveryPresenter* m_presenter{nullptr};

    QLabel* m_myDeviceLabel{nullptr};
    QLabel* m_statusLabel{nullptr};
    QPushButton* m_scanToggleBtn{nullptr};
    QPushButton* m_refreshBtn{nullptr};
    QPushButton* m_directConnectBtn{nullptr};
    QPushButton* m_loopbackBtn{nullptr};

    QWidget* m_cardsContainer{nullptr};
    QVBoxLayout* m_cardsLayout{nullptr};
    QWidget* m_emptyStateWidget{nullptr};

    std::vector<DeviceViewModel> m_lastDevices;

    void setupUi();
    void updateMyDeviceCard();
    void renderDeviceCards(const std::vector<DeviceViewModel>& devices);
    void renderEmptyState();
};

}
