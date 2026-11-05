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

    static bool validateDirectConnectInput(const QString& ip, int port, QString* outError = nullptr);

    QPushButton* getScanToggleBtn() const { return m_scanToggleBtn; }
    QPushButton* getRefreshBtn() const { return m_refreshBtn; }
    QPushButton* getDirectConnectBtn() const { return m_directConnectBtn; }
    QPushButton* getLoopbackBtn() const { return m_loopbackBtn; }
    QPushButton* getGlobalP2pBtn() const { return m_globalP2pBtn; }
    QLabel* getStatusLabel() const { return m_statusLabel; }

    // IDiscoveryView overrides

    void updateDeviceList(const std::vector<DeviceViewModel>& devices) override;
    void setScanningStatus(bool isScanning) override;
    void showNfcPairResult(bool success, const std::string& message) override;
    void showNotification(const std::string& text) override;

signals:
    void sendFileRequested(const QString& peerId, const QString& filePath);
    void hostRoomRequested(const QString& filePath = QString());
    void joinRoomRequested(const QString& roomCode);

private slots:
    void onRefreshClicked();
    void onToggleScanClicked();
    void onDirectConnectClicked();
    void onAddLoopbackClicked();
    void onGlobalP2pClicked();
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
    QPushButton* m_globalP2pBtn{nullptr};

    QWidget* m_cardsContainer{nullptr};
    QVBoxLayout* m_cardsLayout{nullptr};
    QWidget* m_emptyStateWidget{nullptr};

    std::vector<DeviceViewModel> m_lastDevices;
    bool m_isScanning{true};

    void setupUi();
    void updateMyDeviceCard();
    void renderDeviceCards(const std::vector<DeviceViewModel>& devices);
    void renderEmptyState();
};

}
