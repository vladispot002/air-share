#include "landrop/ui/qt/QtDiscoveryWidget.hpp"
#include "landrop/ui/DiscoveryPresenter.hpp"

#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QMetaObject>
#include <QScrollArea>

namespace landrop::ui {

QtDiscoveryWidget::QtDiscoveryWidget(QWidget* parent)
    : QWidget(parent) {
    setupUi();
}

void QtDiscoveryWidget::setPresenter(DiscoveryPresenter* presenter) {
    m_presenter = presenter;
    updateMyDeviceCard();
}

void QtDiscoveryWidget::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(16);

    // Top Header
    auto* headerLayout = new QHBoxLayout();
    auto* titleLabel = new QLabel("📡 Radar & Discovery", this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);

    m_statusLabel = new QLabel("Listening on LAN", this);
    m_statusLabel->setStyleSheet("color: #30d158; font-weight: bold; padding: 4px 8px; border-radius: 4px; background: rgba(48, 209, 88, 0.15);");

    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_statusLabel);
    mainLayout->addLayout(headerLayout);

    // My Device Card
    auto* myDeviceFrame = new QFrame(this);
    myDeviceFrame->setObjectName("myDeviceCard");
    auto* myDeviceLayout = new QHBoxLayout(myDeviceFrame);
    myDeviceLayout->setContentsMargins(16, 12, 16, 12);

    m_myDeviceLabel = new QLabel("🖥️ My Device: Scanning...", myDeviceFrame);
    QFont devFont = m_myDeviceLabel->font();
    devFont.setPointSize(13);
    devFont.setBold(true);
    m_myDeviceLabel->setFont(devFont);

    m_loopbackBtn = new QPushButton("💻 Add Loopback (127.0.0.1)", myDeviceFrame);
    m_loopbackBtn->setToolTip("Add local loopback node for instant self-transfer testing");
    connect(m_loopbackBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::onAddLoopbackClicked);

    myDeviceLayout->addWidget(m_myDeviceLabel);
    myDeviceLayout->addStretch();
    myDeviceLayout->addWidget(m_loopbackBtn);
    mainLayout->addWidget(myDeviceFrame);

    // Controls Bar
    auto* actionsLayout = new QHBoxLayout();
    m_refreshBtn = new QPushButton("↺ Broadcast Scan", this);
    m_refreshBtn->setObjectName("primaryBtn");
    connect(m_refreshBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::onRefreshClicked);

    m_scanToggleBtn = new QPushButton("⚡ Beacon: ON", this);
    connect(m_scanToggleBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::onToggleScanClicked);

    m_directConnectBtn = new QPushButton("🌐 Direct IP Connect", this);
    connect(m_directConnectBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::onDirectConnectClicked);

    actionsLayout->addWidget(m_refreshBtn);
    actionsLayout->addWidget(m_scanToggleBtn);
    actionsLayout->addWidget(m_directConnectBtn);
    actionsLayout->addStretch();
    mainLayout->addLayout(actionsLayout);

    // Device Cards Area (Scrollable)
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    m_cardsContainer = new QWidget(scrollArea);
    m_cardsLayout = new QVBoxLayout(m_cardsContainer);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(12);

    scrollArea->setWidget(m_cardsContainer);
    mainLayout->addWidget(scrollArea, 1);

    renderEmptyState();
}

void QtDiscoveryWidget::updateMyDeviceCard() {
    if (!m_presenter) return;
    std::string name = m_presenter->getLocalDeviceName();
    std::string ip = m_presenter->getLocalDeviceIp();
    uint16_t port = m_presenter->getLocalDevicePort();

    QString text = QString("🖥️ My Device: <b>%1</b> &nbsp;|&nbsp; Local IP: <b>%2:%3</b> &nbsp;|&nbsp; <span style='color:#30d158'>Discoverable</span>")
        .arg(QString::fromStdString(name))
        .arg(QString::fromStdString(ip))
        .arg(port);
    m_myDeviceLabel->setText(text);
}

void QtDiscoveryWidget::updateDeviceList(const std::vector<DeviceViewModel>& devices) {
    QMetaObject::invokeMethod(this, [this, devices]() {
        renderDeviceCards(devices);
    }, Qt::QueuedConnection);
}

void QtDiscoveryWidget::setScanningStatus(bool isScanning) {
    QMetaObject::invokeMethod(this, [this, isScanning]() {
        if (isScanning) {
            m_scanToggleBtn->setText("⚡ Beacon: ON");
            m_statusLabel->setText("Scanning Active");
            m_statusLabel->setStyleSheet("color: #30d158; font-weight: bold; padding: 4px 8px; border-radius: 4px; background: rgba(48, 209, 88, 0.15);");
        } else {
            m_scanToggleBtn->setText("⚡ Beacon: PAUSED");
            m_statusLabel->setText("Discovery Idle");
            m_statusLabel->setStyleSheet("color: #ff9f0a; font-weight: bold; padding: 4px 8px; border-radius: 4px; background: rgba(255, 159, 10, 0.15);");
        }
    }, Qt::QueuedConnection);
}

void QtDiscoveryWidget::showNfcPairResult(bool success, const std::string& message) {
    QMetaObject::invokeMethod(this, [this, success, message]() {
        if (success) {
            QMessageBox::information(this, "NFC Fast Pair", QString::fromStdString(message));
        } else {
            QMessageBox::warning(this, "NFC Fast Pair Failed", QString::fromStdString(message));
        }
    }, Qt::QueuedConnection);
}

void QtDiscoveryWidget::showNotification(const std::string& text) {
    QMetaObject::invokeMethod(this, [this, text]() {
        QMessageBox::information(this, "LAN-Drop Notification", QString::fromStdString(text));
    }, Qt::QueuedConnection);
}

void QtDiscoveryWidget::renderEmptyState() {
    // Clear existing widgets
    QLayoutItem* item;
    while ((item = m_cardsLayout->takeAt(0)) != nullptr) {
        if (item->widget()) delete item->widget();
        delete item;
    }

    m_emptyStateWidget = new QWidget(m_cardsContainer);
    auto* layout = new QVBoxLayout(m_emptyStateWidget);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(12);

    auto* iconLabel = new QLabel("📡", m_emptyStateWidget);
    QFont iconFont = iconLabel->font();
    iconFont.setPointSize(48);
    iconLabel->setFont(iconFont);
    iconLabel->setAlignment(Qt::AlignCenter);

    auto* header = new QLabel("Scanning for LAN-Drop Devices", m_emptyStateWidget);
    QFont hFont = header->font();
    hFont.setPointSize(16);
    hFont.setBold(true);
    header->setFont(hFont);
    header->setAlignment(Qt::AlignCenter);

    auto* desc = new QLabel("Open LAN-Drop on another Mac, PC, or mobile device connected to this Wi-Fi network.\nOr add a loopback address below to perform local self-testing.", m_emptyStateWidget);
    desc->setAlignment(Qt::AlignCenter);
    desc->setStyleSheet("color: #8e8e93; line-height: 1.4;");

    auto* btnLayout = new QHBoxLayout();
    btnLayout->setAlignment(Qt::AlignCenter);
    auto* scanBtn = new QPushButton("↺ Broadcast Scan", m_emptyStateWidget);
    scanBtn->setObjectName("primaryBtn");
    connect(scanBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::onRefreshClicked);

    auto* loopBtn = new QPushButton("💻 Add Loopback (127.0.0.1)", m_emptyStateWidget);
    connect(loopBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::onAddLoopbackClicked);

    btnLayout->addWidget(scanBtn);
    btnLayout->addWidget(loopBtn);

    layout->addSpacing(30);
    layout->addWidget(iconLabel);
    layout->addWidget(header);
    layout->addWidget(desc);
    layout->addSpacing(10);
    layout->addLayout(btnLayout);
    layout->addStretch();

    m_cardsLayout->addWidget(m_emptyStateWidget);
}

void QtDiscoveryWidget::renderDeviceCards(const std::vector<DeviceViewModel>& devices) {
    m_lastDevices = devices;

    // Clear layout
    QLayoutItem* item;
    while ((item = m_cardsLayout->takeAt(0)) != nullptr) {
        if (item->widget()) delete item->widget();
        delete item;
    }

    if (devices.empty()) {
        renderEmptyState();
        return;
    }

    for (const auto& dev : devices) {
        auto* card = new QFrame(m_cardsContainer);
        card->setObjectName("card");
        auto* cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(16, 12, 16, 12);

        QString icon = "🖥️";
        if (dev.type.find("Phone") != std::string::npos || dev.type.find("Galaxy") != std::string::npos || dev.type.find("iOS") != std::string::npos) {
            icon = "📱";
        } else if (dev.type.find("Cloud") != std::string::npos) {
            icon = "☁️";
        }

        auto* iconLbl = new QLabel(icon, card);
        QFont iconF = iconLbl->font();
        iconF.setPointSize(24);
        iconLbl->setFont(iconF);
        cardLayout->addWidget(iconLbl);

        auto* infoLayout = new QVBoxLayout();
        auto* nameLbl = new QLabel(QString("<b>%1</b>").arg(QString::fromStdString(dev.name)), card);
        auto* endpLbl = new QLabel(QString::fromStdString(dev.endpoint), card);
        endpLbl->setStyleSheet("color: #8e8e93; font-size: 11px;");
        infoLayout->addWidget(nameLbl);
        infoLayout->addWidget(endpLbl);
        cardLayout->addLayout(infoLayout, 1);

        auto* badge = new QLabel(QString::fromStdString(dev.type), card);
        badge->setStyleSheet("background: #007aff22; color: #007aff; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 11px;");
        cardLayout->addWidget(badge);

        QString peerId = QString::fromStdString(dev.id);
        auto* sendBtn = new QPushButton("Send File", card);
        sendBtn->setObjectName("primaryBtn");
        connect(sendBtn, &QPushButton::clicked, this, [this, peerId]() {
            onDeviceSendClicked(peerId);
        });
        cardLayout->addWidget(sendBtn);

        auto* removeBtn = new QPushButton("✕", card);
        removeBtn->setObjectName("dangerBtn");
        removeBtn->setToolTip("Remove peer from radar");
        connect(removeBtn, &QPushButton::clicked, this, [this, peerId]() {
            onDeviceRemoveClicked(peerId);
        });
        cardLayout->addWidget(removeBtn);

        m_cardsLayout->addWidget(card);
    }
    m_cardsLayout->addStretch();
}

void QtDiscoveryWidget::onRefreshClicked() {
    if (m_presenter) {
        m_presenter->onRefreshDevicesClicked();
    }
    updateMyDeviceCard();
}

void QtDiscoveryWidget::onToggleScanClicked() {
    static bool isScanning = true;
    isScanning = !isScanning;
    if (m_presenter) {
        m_presenter->onToggleScanClicked(isScanning);
    }
}

void QtDiscoveryWidget::onDirectConnectClicked() {
    bool ok = false;
    QString ip = QInputDialog::getText(this, "Direct Connect", "Enter Target IP Address:", QLineEdit::Normal, "192.168.1.100", &ok);
    if (!ok || ip.trimmed().isEmpty()) return;

    int port = QInputDialog::getInt(this, "Direct Connect", "Enter Port:", 8848, 1, 65535, 1, &ok);
    if (!ok) return;

    if (m_presenter) {
        m_presenter->onDirectConnectRequested(ip.toStdString(), static_cast<uint16_t>(port));
    }
}

void QtDiscoveryWidget::onAddLoopbackClicked() {
    if (m_presenter) {
        m_presenter->onDirectConnectRequested("127.0.0.1", 8848);
    }
}

void QtDiscoveryWidget::onDeviceSendClicked(const QString& peerId) {
    QString filePath = QFileDialog::getOpenFileName(this, "Select File to Send via LAN-Drop");
    if (filePath.isEmpty()) return;

    emit sendFileRequested(peerId, filePath);
}

void QtDiscoveryWidget::onDeviceRemoveClicked(const QString& peerId) {
    if (m_presenter) {
        m_presenter->onRemovePeerClicked(peerId.toStdString());
    }
}

}
