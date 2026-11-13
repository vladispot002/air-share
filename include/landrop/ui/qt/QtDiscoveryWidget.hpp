#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QStackedWidget>
#include <QLineEdit>
#include <QSpinBox>
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
    QPushButton* getLanTabBtn() const { return m_lanTabBtn; }
    QPushButton* getGlobalTabBtn() const { return m_globalTabBtn; }
    QFrame* getActiveSessionCard() const { return m_activeSessionCard; }
    QPushButton* getActiveSessionCloseBtn() const { return m_activeSessionCloseBtn; }

    // IDiscoveryView overrides

    void updateDeviceList(const std::vector<DeviceViewModel>& devices) override;
    void setScanningStatus(bool isScanning) override;
    void showNfcPairResult(bool success, const std::string& message) override;
    void showNotification(const std::string& text) override;

signals:
    void sendFileRequested(const QString& peerId, const QString& filePath);
    void hostRoomRequested(const QString& filePath = QString());
    void joinRoomRequested(const QString& roomCode);
    void joinRoomWithFileRequested(const QString& roomCode, const QString& filePath);
    void closeGlobalSessionRequested(const QString& roomCode);

public slots:
    void switchToLanMode();
    void switchToGlobalMode();
    void updateGlobalSession(const QString& roomCode, bool connected);

private slots:
    void onRefreshClicked();
    void onToggleScanClicked();
    void onDirectConnectClicked();
    void onAddLoopbackClicked();
    void onGlobalP2pClicked();
    void onDeviceSendClicked(const QString& peerId);
    void onDeviceRemoveClicked(const QString& peerId);
    void onGlobalHostClicked();
    void onGlobalJoinClicked();
    void onGlobalJoinWithFileClicked();
    void onActiveSessionSendClicked();
    void onActiveSessionCloseClicked();
    void onGlobalDirectConnectClicked();

private:
    DiscoveryPresenter* m_presenter{nullptr};

    QLabel* m_myDeviceLabel{nullptr};
    QLabel* m_statusLabel{nullptr};
    QPushButton* m_scanToggleBtn{nullptr};
    QPushButton* m_refreshBtn{nullptr};
    QPushButton* m_directConnectBtn{nullptr};
    QPushButton* m_loopbackBtn{nullptr};
    QPushButton* m_globalP2pBtn{nullptr};

    // Mode Selector (LAN vs Global P2P across different networks)
    QPushButton* m_lanTabBtn{nullptr};
    QPushButton* m_globalTabBtn{nullptr};
    QStackedWidget* m_modeStack{nullptr};
    QWidget* m_lanPage{nullptr};
    QWidget* m_globalPage{nullptr};

    // Global P2P Controls
    QLineEdit* m_globalRoomInput{nullptr};
    QLineEdit* m_globalDirectIpInput{nullptr};
    QSpinBox* m_globalDirectPortSpin{nullptr};
    QLabel* m_globalServerStatusLabel{nullptr};

    // Active Global Session Card (Global Tab)
    QFrame* m_activeSessionCard{nullptr};
    QLabel* m_activeSessionLabel{nullptr};
    QPushButton* m_activeSessionSendBtn{nullptr};
    QPushButton* m_activeSessionCloseBtn{nullptr};
    QString m_currentActiveRoomCode;

    QWidget* m_cardsContainer{nullptr};
    QVBoxLayout* m_cardsLayout{nullptr};
    QWidget* m_emptyStateWidget{nullptr};

    std::vector<DeviceViewModel> m_lastDevices;
    bool m_isScanning{true};

    void setupUi();
    void setupLanPage();
    void setupGlobalPage();
    void updateMyDeviceCard();
    void renderDeviceCards(const std::vector<DeviceViewModel>& devices);
    void renderEmptyState();
};

}
