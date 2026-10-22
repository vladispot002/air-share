#include "landrop/ui/qt/QtDiscoveryWidget.hpp"
#include "landrop/ui/DiscoveryPresenter.hpp"

#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QMetaObject>
#include <QScrollArea>
#include <QHostAddress>
#include <QTimer>
#include <QStyle>

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
    m_statusLabel->setObjectName("badgeSuccess");

    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_statusLabel);
    mainLayout->addLayout(headerLayout);

    // My Device Card
    auto* myDeviceFrame = new QFrame(this);
    myDeviceFrame->setObjectName("myDeviceCard");
    auto* myDeviceLayout = new QHBoxLayout(myDeviceFrame);
    myDeviceLayout->setContentsMargins(16, 12, 16, 12);
    myDeviceLayout->setSpacing(12);

    m_myDeviceLabel = new QLabel("🖥️ My Device: Scanning...", myDeviceFrame);
    QFont devFont = m_myDeviceLabel->font();
    devFont.setPointSize(13);
    devFont.setBold(true);
    m_myDeviceLabel->setFont(devFont);

    m_loopbackBtn = new QPushButton("💻 Add Loopback (127.0.0.1)", myDeviceFrame);
    m_loopbackBtn->setToolTip("Add local loopback node for instant self-transfer testing");
    connect(m_loopbackBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::onAddLoopbackClicked);

    myDeviceLayout->addWidget(m_myDeviceLabel, 1);
    myDeviceLayout->addWidget(m_loopbackBtn, 0, Qt::AlignRight);
    mainLayout->addWidget(myDeviceFrame);

    // Controls Bar
    auto* actionsLayout = new QHBoxLayout();
    actionsLayout->setSpacing(10);
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

    QString text = QString("🖥️ My Device: <b>%1</b> &nbsp;•&nbsp; %2:%3 &nbsp;•&nbsp; <span style='color:#30d158'>● Discoverable</span>")
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
            m_statusLabel->setObjectName("badgeSuccess");
        } else {
            m_scanToggleBtn->setText("⚡ Beacon: PAUSED");
            m_statusLabel->setText("Discovery Idle");
            m_statusLabel->setObjectName("badgeWarning");
        }
        m_statusLabel->style()->unpolish(m_statusLabel);
        m_statusLabel->style()->polish(m_statusLabel);
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
        if (m_statusLabel) {
            m_statusLabel->setText(QString::fromStdString(text));
            m_statusLabel->setObjectName("badgeInfo");
            m_statusLabel->style()->unpolish(m_statusLabel);
            m_statusLabel->style()->polish(m_statusLabel);

            // Revert back to scanning status after 4 seconds
            QTimer::singleShot(4000, this, [this]() {
                setScanningStatus(true);
            });
        }
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
    desc->setObjectName("secondaryLabel");

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
        cardLayout->setSpacing(12);

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
        infoLayout->setSpacing(2);
        auto* nameLbl = new QLabel(QString("<b>%1</b>").arg(QString::fromStdString(dev.name)), card);
        auto* endpLbl = new QLabel(QString::fromStdString(dev.endpoint), card);
        endpLbl->setObjectName("secondaryLabel");
        infoLayout->addWidget(nameLbl);
        infoLayout->addWidget(endpLbl);
        cardLayout->addLayout(infoLayout, 1);

        auto* badge = new QLabel(QString::fromStdString(dev.type), card);
        badge->setObjectName("badgeInfo");
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

bool QtDiscoveryWidget::validateDirectConnectInput(const QString& ip, int port, QString* outError) {
    QString trimmed = ip.trimmed();
    if (trimmed.isEmpty()) {
        if (outError) *outError = "IP address cannot be empty.";
        return false;
    }

    QHostAddress addr;
    if (!addr.setAddress(trimmed)) {
        if (outError) *outError = "Invalid IP address format: " + trimmed;
        return false;
    }

    if (port <= 0 || port > 65535) {
        if (outError) *outError = "Port must be in range 1-65535: " + QString::number(port);
        return false;
    }

    return true;
}

void QtDiscoveryWidget::onDirectConnectClicked() {
    bool ok = false;
    QString ip = QInputDialog::getText(this, "Direct Connect", "Enter Target IP Address:", QLineEdit::Normal, "192.168.1.100", &ok);
    if (!ok || ip.trimmed().isEmpty()) return;

    int port = QInputDialog::getInt(this, "Direct Connect", "Enter Port:", 8848, 1, 65535, 1, &ok);
    if (!ok) return;

    QString errorMsg;
    if (!validateDirectConnectInput(ip, port, &errorMsg)) {
        QMessageBox::warning(this, "Direct Connect Error", errorMsg);
        return;
    }

    if (m_presenter) {
        m_presenter->onDirectConnectRequested(ip.trimmed().toStdString(), static_cast<uint16_t>(port));
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
