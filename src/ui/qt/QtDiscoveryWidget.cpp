#include "landrop/ui/qt/QtDiscoveryWidget.hpp"
#include "landrop/ui/DiscoveryPresenter.hpp"
#include "landrop/core/WebRtcSessionCoordinator.hpp"

#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QMetaObject>
#include <QScrollArea>
#include <QHostAddress>
#include <QTimer>
#include <QStyle>
#include <QStackedWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QGroupBox>

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
    mainLayout->setSpacing(14);

    // Top Header
    auto* headerLayout = new QHBoxLayout();
    auto* titleLabel = new QLabel("📡 Radar & Network Discovery", this);
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

    // Mode Bar: Switch between Local LAN and Different Networks (Global P2P)
    auto* modeBarLayout = new QHBoxLayout();
    modeBarLayout->setSpacing(8);

    m_lanTabBtn = new QPushButton("📡 Локальна мережа (LAN / Wi-Fi)", this);
    m_lanTabBtn->setCheckable(true);
    m_lanTabBtn->setChecked(true);
    m_lanTabBtn->setCursor(Qt::PointingHandCursor);
    m_lanTabBtn->setStyleSheet("background-color: #007aff; color: #ffffff; font-weight: bold; border-radius: 6px; padding: 8px 16px; border: 1px solid #0062cc;");
    connect(m_lanTabBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::switchToLanMode);

    m_globalTabBtn = new QPushButton("🌐 Різні мережі (Інтернет / Web-клієнт / Global P2P)", this);
    m_globalTabBtn->setCheckable(true);
    m_globalTabBtn->setChecked(false);
    m_globalTabBtn->setCursor(Qt::PointingHandCursor);
    m_globalTabBtn->setStyleSheet("background-color: transparent; color: #9da3af; font-weight: 500; border: 1px solid #3e3e4e; border-radius: 6px; padding: 8px 16px;");
    connect(m_globalTabBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::switchToGlobalMode);

    modeBarLayout->addWidget(m_lanTabBtn);
    modeBarLayout->addWidget(m_globalTabBtn);
    modeBarLayout->addStretch();
    mainLayout->addLayout(modeBarLayout);

    // Stacked widget for the 2 modes
    m_modeStack = new QStackedWidget(this);

    // Page 0: LAN
    m_lanPage = new QWidget(m_modeStack);
    setupLanPage();
    m_modeStack->addWidget(m_lanPage);

    // Page 1: Global P2P (Different Networks)
    m_globalPage = new QWidget(m_modeStack);
    setupGlobalPage();
    m_modeStack->addWidget(m_globalPage);

    mainLayout->addWidget(m_modeStack, 1);
}

void QtDiscoveryWidget::setupLanPage() {
    auto* lanLayout = new QVBoxLayout(m_lanPage);
    lanLayout->setContentsMargins(0, 0, 0, 0);
    lanLayout->setSpacing(12);

    // My Device Card
    auto* myDeviceFrame = new QFrame(m_lanPage);
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
    lanLayout->addWidget(myDeviceFrame);

    // Suggestion banner to switch to Cross-Network mode
    auto* bannerFrame = new QFrame(m_lanPage);
    bannerFrame->setObjectName("card");
    bannerFrame->setStyleSheet("background-color: rgba(0, 122, 255, 0.08); border: 1px solid rgba(0, 122, 255, 0.25); border-radius: 8px; padding: 10px;");
    auto* bannerLayout = new QHBoxLayout(bannerFrame);
    bannerLayout->setContentsMargins(12, 8, 12, 8);
    auto* bannerText = new QLabel("💡 <b>Потрібно передати файл на пристрій в іншій Wi-Fi мережі, мобільному інтернеті або через браузер?</b>", bannerFrame);
    auto* bannerSwitchBtn = new QPushButton("🌐 Перемкнутися на «Різні мережі»", bannerFrame);
    bannerSwitchBtn->setObjectName("primaryBtn");
    connect(bannerSwitchBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::switchToGlobalMode);
    bannerLayout->addWidget(bannerText, 1);
    bannerLayout->addWidget(bannerSwitchBtn, 0);
    lanLayout->addWidget(bannerFrame);

    // Controls Bar
    auto* actionsLayout = new QHBoxLayout();
    actionsLayout->setSpacing(10);
    m_refreshBtn = new QPushButton("↺ Broadcast Scan", m_lanPage);
    m_refreshBtn->setObjectName("primaryBtn");
    connect(m_refreshBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::onRefreshClicked);

    m_scanToggleBtn = new QPushButton("⚡ Beacon: ON", m_lanPage);
    connect(m_scanToggleBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::onToggleScanClicked);

    m_directConnectBtn = new QPushButton("🌐 Direct IP Connect", m_lanPage);
    connect(m_directConnectBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::onDirectConnectClicked);

    m_globalP2pBtn = new QPushButton("🔗 Підключити пристрій (Різні мережі / Web)", m_lanPage);
    m_globalP2pBtn->setToolTip("Підключити пристрій через WebRTC / QR-код або 8-символьний Crockford код");
    connect(m_globalP2pBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::switchToGlobalMode);

    actionsLayout->addWidget(m_refreshBtn);
    actionsLayout->addWidget(m_scanToggleBtn);
    actionsLayout->addWidget(m_directConnectBtn);
    actionsLayout->addWidget(m_globalP2pBtn);
    actionsLayout->addStretch();
    lanLayout->addLayout(actionsLayout);

    // Device Cards Area (Scrollable)
    auto* scrollArea = new QScrollArea(m_lanPage);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    m_cardsContainer = new QWidget(scrollArea);
    m_cardsLayout = new QVBoxLayout(m_cardsContainer);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(12);

    scrollArea->setWidget(m_cardsContainer);
    lanLayout->addWidget(scrollArea, 1);

    renderEmptyState();
}

void QtDiscoveryWidget::setupGlobalPage() {
    auto* scrollArea = new QScrollArea(m_globalPage);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* container = new QWidget(scrollArea);
    auto* globalLayout = new QVBoxLayout(container);
    globalLayout->setContentsMargins(0, 0, 0, 0);
    globalLayout->setSpacing(14);

    // 1. Hero Card
    auto* heroFrame = new QFrame(container);
    heroFrame->setObjectName("card");
    heroFrame->setStyleSheet("background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #1a233a, stop:1 #1c2b48); border: 1px solid #2d4373; border-radius: 12px; padding: 16px;");
    auto* heroLayout = new QVBoxLayout(heroFrame);
    heroLayout->setSpacing(8);

    auto* heroTitle = new QLabel("🌐 Підключення між різними мережами (Global P2P через Інтернет)", heroFrame);
    QFont hf = heroTitle->font();
    hf.setPointSize(16);
    hf.setBold(true);
    heroTitle->setFont(hf);
    heroTitle->setStyleSheet("color: #ffffff;");

    auto* heroDesc = new QLabel(
        "Передавайте файли без прив'язки до однієї Wi-Fi мережі! Працює через Інтернет, мобільний зв'язок (4G/5G) або між домашньою та офісною мережами за допомогою наскрізного WebRTC шифрування.",
        heroFrame
    );
    heroDesc->setWordWrap(true);
    heroDesc->setStyleSheet("color: #cbd5e1; font-size: 13px; line-height: 1.4;");

    const char* envSignaling = std::getenv("LANDROP_SIGNALING_URL");
    QString sigHost = (envSignaling && envSignaling[0] != '\0') ? QString::fromUtf8(envSignaling) : "landrop-server.duckdns.org";
    m_globalServerStatusLabel = new QLabel(QString("● Підключено до глобального WebRTC координатора: <b>%1</b> (Захист TLS/E2E)").arg(sigHost), heroFrame);
    m_globalServerStatusLabel->setStyleSheet("color: #34c759; font-size: 12px; font-weight: 500;");

    heroLayout->addWidget(heroTitle);
    heroLayout->addWidget(heroDesc);
    heroLayout->addWidget(m_globalServerStatusLabel);
    globalLayout->addWidget(heroFrame);

    // Active Global Session Card (shown when WebRTC peer is connected)
    m_activeSessionCard = new QFrame(container);
    m_activeSessionCard->setObjectName("card");
    m_activeSessionCard->setStyleSheet("background: #0f241d; border: 1px solid #238636; border-radius: 12px; padding: 12px 16px;");
    m_activeSessionCard->setVisible(false);

    auto* activeLayout = new QHBoxLayout(m_activeSessionCard);
    activeLayout->setContentsMargins(12, 10, 12, 10);
    activeLayout->setSpacing(12);

    m_activeSessionLabel = new QLabel(m_activeSessionCard);
    m_activeSessionLabel->setStyleSheet("color: #3fb950; font-weight: bold; font-size: 13px;");

    m_activeSessionSendBtn = new QPushButton("📁 Надіслати файл", m_activeSessionCard);
    m_activeSessionSendBtn->setObjectName("primaryBtn");
    m_activeSessionSendBtn->setCursor(Qt::PointingHandCursor);
    connect(m_activeSessionSendBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::onActiveSessionSendClicked);

    m_activeSessionCloseBtn = new QPushButton("✕ Закрити", m_activeSessionCard);
    m_activeSessionCloseBtn->setCursor(Qt::PointingHandCursor);
    connect(m_activeSessionCloseBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::onActiveSessionCloseClicked);

    activeLayout->addWidget(m_activeSessionLabel, 1);
    activeLayout->addWidget(m_activeSessionSendBtn);
    activeLayout->addWidget(m_activeSessionCloseBtn);
    globalLayout->addWidget(m_activeSessionCard);

    // 2. Action Cards Layout (Side by Side)
    auto* cardsLayout = new QHBoxLayout();
    cardsLayout->setSpacing(14);

    // Card A: Host Room (Send file across networks)
    auto* hostCard = new QFrame(container);
    hostCard->setObjectName("card");
    auto* hostLayout = new QVBoxLayout(hostCard);
    hostLayout->setContentsMargins(18, 16, 18, 16);
    hostLayout->setSpacing(12);

    auto* hostTitle = new QLabel("📤 Надіслати файл (Створити кімнату)", hostCard);
    QFont htf = hostTitle->font();
    htf.setPointSize(14);
    htf.setBold(true);
    hostTitle->setFont(htf);

    auto* hostDesc = new QLabel("Оберіть файл на комп'ютері. Застосунок створить 8-символьний код кімнати та QR-код для Web-клієнта або мобільного застосунку в іншій мережі.", hostCard);
    hostDesc->setWordWrap(true);
    hostDesc->setObjectName("secondaryLabel");

    auto* hostSendBtn = new QPushButton("📁 Обрати файл та показати QR-код", hostCard);
    hostSendBtn->setObjectName("primaryBtn");
    hostSendBtn->setCursor(Qt::PointingHandCursor);
    connect(hostSendBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::onGlobalHostClicked);

    auto* hostPairBtn = new QPushButton("🔗 Сполучити пристрій (без файлу)", hostCard);
    hostPairBtn->setCursor(Qt::PointingHandCursor);
    connect(hostPairBtn, &QPushButton::clicked, this, [this]() { emit hostRoomRequested(QString()); });

    hostLayout->addWidget(hostTitle);
    hostLayout->addWidget(hostDesc);
    hostLayout->addStretch();
    hostLayout->addWidget(hostSendBtn);
    hostLayout->addWidget(hostPairBtn);
    cardsLayout->addWidget(hostCard, 1);

    // Card B: Join Room (Receive file from another network)
    auto* joinCard = new QFrame(container);
    joinCard->setObjectName("card");
    auto* joinLayout = new QVBoxLayout(joinCard);
    joinLayout->setContentsMargins(18, 16, 18, 16);
    joinLayout->setSpacing(12);

    auto* joinTitle = new QLabel("📥 Приєднатися до кімнати (Ввести код)", joinCard);
    QFont jtf = joinTitle->font();
    jtf.setPointSize(14);
    jtf.setBold(true);
    joinTitle->setFont(jtf);

    auto* joinDesc = new QLabel("Введіть 8-значний код від іншого пристрою (наприклад, DROP-K8M4-P9X2 або посилання з браузера):", joinCard);
    joinDesc->setWordWrap(true);
    joinDesc->setObjectName("secondaryLabel");

    m_globalRoomInput = new QLineEdit(joinCard);
    m_globalRoomInput->setPlaceholderText("наприклад: DROP-K8M4-P9X2");
    m_globalRoomInput->setStyleSheet("font-size: 14px; font-weight: bold; letter-spacing: 1px; padding: 8px 10px;");

    auto* joinBtn = new QPushButton("📥 Приєднатися (Очікувати файл)", joinCard);
    joinBtn->setObjectName("secondaryBtn");
    joinBtn->setCursor(Qt::PointingHandCursor);
    connect(joinBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::onGlobalJoinClicked);
    connect(m_globalRoomInput, &QLineEdit::returnPressed, this, &QtDiscoveryWidget::onGlobalJoinClicked);

    auto* joinSendBtn = new QPushButton("📤 Приєднатися та надіслати файл...", joinCard);
    joinSendBtn->setObjectName("primaryBtn");
    joinSendBtn->setCursor(Qt::PointingHandCursor);
    connect(joinSendBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::onGlobalJoinWithFileClicked);

    joinLayout->addWidget(joinTitle);
    joinLayout->addWidget(joinDesc);
    joinLayout->addWidget(m_globalRoomInput);
    joinLayout->addStretch();
    joinLayout->addWidget(joinBtn);
    joinLayout->addWidget(joinSendBtn);
    cardsLayout->addWidget(joinCard, 1);

    globalLayout->addLayout(cardsLayout);

    // 3. Direct WAN/IP Connect Card
    auto* directCard = new QFrame(container);
    directCard->setObjectName("card");
    auto* directLayout = new QHBoxLayout(directCard);
    directLayout->setContentsMargins(16, 12, 16, 12);
    directLayout->setSpacing(12);

    auto* directInfoLayout = new QVBoxLayout();
    auto* directTitle = new QLabel("🌐 Пряме IP-підключення (Публічний IP / VPN / Локальний хост)", directCard);
    QFont dtf = directTitle->font();
    dtf.setBold(true);
    directTitle->setFont(dtf);
    auto* directDesc = new QLabel("Підключіться до конкретної зовнішньої IP-адреси, Tailscale, WireGuard чи статичного порту:", directCard);
    directDesc->setObjectName("secondaryLabel");
    directInfoLayout->addWidget(directTitle);
    directInfoLayout->addWidget(directDesc);
    directLayout->addLayout(directInfoLayout, 1);

    m_globalDirectIpInput = new QLineEdit("192.168.1.100", directCard);
    m_globalDirectIpInput->setPlaceholderText("IP-адреса або хост");
    m_globalDirectIpInput->setFixedWidth(160);

    m_globalDirectPortSpin = new QSpinBox(directCard);
    m_globalDirectPortSpin->setRange(1, 65535);
    m_globalDirectPortSpin->setValue(8848);
    m_globalDirectPortSpin->setFixedWidth(80);

    auto* directBtn = new QPushButton("Підключитися", directCard);
    connect(directBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::onGlobalDirectConnectClicked);

    directLayout->addWidget(m_globalDirectIpInput);
    directLayout->addWidget(m_globalDirectPortSpin);
    directLayout->addWidget(directBtn);

    globalLayout->addWidget(directCard);
    globalLayout->addStretch();

    scrollArea->setWidget(container);
    auto* pageLayout = new QVBoxLayout(m_globalPage);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addWidget(scrollArea);
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

    auto* header = new QLabel("Пошук пристроїв LAN-Drop...", m_emptyStateWidget);
    QFont hFont = header->font();
    hFont.setPointSize(16);
    hFont.setBold(true);
    header->setFont(hFont);
    header->setAlignment(Qt::AlignCenter);

    auto* desc = new QLabel("Відкрийте LAN-Drop у браузері або на іншому комп'ютері чи смартфоні.\nТакож ви можете миттєво підключитися через QR-код або спільний код кімнати.", m_emptyStateWidget);
    desc->setAlignment(Qt::AlignCenter);
    desc->setObjectName("secondaryLabel");

    auto* btnLayout = new QHBoxLayout();
    btnLayout->setAlignment(Qt::AlignCenter);
    auto* pairBtn = new QPushButton("🔗 Підключити пристрій (Web / QR-код)", m_emptyStateWidget);
    pairBtn->setObjectName("primaryBtn");
    connect(pairBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::onGlobalP2pClicked);

    auto* scanBtn = new QPushButton("↺ Оновити пошук", m_emptyStateWidget);
    connect(scanBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::onRefreshClicked);

    auto* loopBtn = new QPushButton("💻 Add Loopback (127.0.0.1)", m_emptyStateWidget);
    connect(loopBtn, &QPushButton::clicked, this, &QtDiscoveryWidget::onAddLoopbackClicked);

    btnLayout->addWidget(pairBtn);
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
    m_isScanning = !m_isScanning;
    if (m_presenter) {
        m_presenter->onToggleScanClicked(m_isScanning);
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

void QtDiscoveryWidget::onGlobalP2pClicked() {
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Підключення пристрою (Web / Global P2P)");
    msgBox.setText("<b>Підключення пристроїв (Zero-Config WebRTC)</b><br><br>"
                   "Передавайте файли між Desktop та Web у локальній мережі або через Інтернет без обов'язкової реєстрації.<br>"
                   "Оберіть дію:");
    auto* sendBtn = msgBox.addButton("📷 Створити кімнату (показати QR-код)", QMessageBox::ActionRole);
    auto* receiveBtn = msgBox.addButton("⌨️ Приєднатися до кімнати (ввести код)", QMessageBox::ActionRole);
    auto* pairBtn = msgBox.addButton("🔗 Сполучити з Web-клієнтом", QMessageBox::ActionRole);
    msgBox.addButton("Скасувати", QMessageBox::RejectRole);

    msgBox.exec();

    if (msgBox.clickedButton() == sendBtn) {
        QString filePath = QFileDialog::getOpenFileName(this, "Оберіть файл для надсилання (Global P2P)");
        if (filePath.isEmpty()) return;
        emit hostRoomRequested(filePath);
    } else if (msgBox.clickedButton() == receiveBtn) {
        bool ok = false;
        QString input = QInputDialog::getText(
            this,
            "Отримати файл (Приєднатися до кімнати)",
            "Введіть 8-значний код кімнати від відправника (наприклад, DROP-K8M4-P9X2 або K8M4P9X2):",
            QLineEdit::Normal,
            "",
            &ok
        );
        if (ok && !input.trimmed().isEmpty()) {
            std::string err;
            std::string norm = WebRtcSessionCoordinator::normalizeRoomCode(input.trimmed().toStdString(), &err);
            if (norm.empty()) {
                QMessageBox::warning(this, "Некоректний код кімнати", QString::fromStdString("Помилка: " + err));
                return;
            }
            emit joinRoomRequested(QString::fromStdString(norm));
        }
    } else if (msgBox.clickedButton() == pairBtn) {
        emit hostRoomRequested(QString());
    }
}

void QtDiscoveryWidget::onDeviceSendClicked(const QString& peerId) {
    QString filePath = QFileDialog::getOpenFileName(this, "Select File to Send via LAN-Drop");
    if (filePath.isEmpty()) return;

    emit sendFileRequested(peerId, filePath);
}

void QtDiscoveryWidget::switchToLanMode() {
    if (m_modeStack) m_modeStack->setCurrentIndex(0);
    if (m_lanTabBtn) {
        m_lanTabBtn->setChecked(true);
        m_lanTabBtn->setStyleSheet("background-color: #007aff; color: #ffffff; font-weight: bold; border-radius: 6px; padding: 8px 16px; border: 1px solid #0062cc;");
    }
    if (m_globalTabBtn) {
        m_globalTabBtn->setChecked(false);
        m_globalTabBtn->setStyleSheet("background-color: transparent; color: #9da3af; font-weight: 500; border: 1px solid #3e3e4e; border-radius: 6px; padding: 8px 16px;");
    }
    if (m_statusLabel) {
        m_statusLabel->setText(m_isScanning ? "Listening on LAN" : "Discovery Idle");
    }
}

void QtDiscoveryWidget::switchToGlobalMode() {
    if (m_modeStack) m_modeStack->setCurrentIndex(1);
    if (m_lanTabBtn) {
        m_lanTabBtn->setChecked(false);
        m_lanTabBtn->setStyleSheet("background-color: transparent; color: #9da3af; font-weight: 500; border: 1px solid #3e3e4e; border-radius: 6px; padding: 8px 16px;");
    }
    if (m_globalTabBtn) {
        m_globalTabBtn->setChecked(true);
        m_globalTabBtn->setStyleSheet("background-color: #007aff; color: #ffffff; font-weight: bold; border-radius: 6px; padding: 8px 16px; border: 1px solid #0062cc;");
    }
    if (m_statusLabel) {
        m_statusLabel->setText("Global P2P Active");
    }
}

void QtDiscoveryWidget::onGlobalHostClicked() {
    QString filePath = QFileDialog::getOpenFileName(this, "Оберіть файл для надсилання в іншу мережу (Global P2P)");
    if (filePath.isEmpty()) return;
    emit hostRoomRequested(filePath);
}

void QtDiscoveryWidget::onGlobalJoinClicked() {
    if (!m_globalRoomInput) return;
    QString input = m_globalRoomInput->text().trimmed();
    if (input.isEmpty()) {
        QMessageBox::information(this, "Введіть код кімнати", "Будь ласка, введіть 8-символьний код кімнати від відправника (наприклад: DROP-K8M4-P9X2 або K8M4P9X2).");
        return;
    }

    std::string err;
    std::string norm = WebRtcSessionCoordinator::normalizeRoomCode(input.toStdString(), &err);
    if (norm.empty()) {
        QMessageBox::warning(this, "Некоректний код кімнати", QString::fromStdString("Помилка валідації коду: " + err));
        return;
    }

    emit joinRoomRequested(QString::fromStdString(norm));
}

void QtDiscoveryWidget::onGlobalDirectConnectClicked() {
    if (!m_globalDirectIpInput || !m_globalDirectPortSpin) return;
    QString ip = m_globalDirectIpInput->text().trimmed();
    int port = m_globalDirectPortSpin->value();

    QString errorMsg;
    if (!validateDirectConnectInput(ip, port, &errorMsg)) {
        QMessageBox::warning(this, "Помилка прямого підключення", errorMsg);
        return;
    }

    if (m_presenter) {
        m_presenter->onDirectConnectRequested(ip.toStdString(), static_cast<uint16_t>(port));
    }
}

void QtDiscoveryWidget::updateGlobalSession(const QString& roomCode, bool connected) {
    if (!m_activeSessionCard || !m_activeSessionLabel) return;
    if (connected && !roomCode.isEmpty()) {
        m_currentActiveRoomCode = roomCode;
        m_activeSessionLabel->setText(QString("🟢 Активне WebRTC P2P з'єднання: <b>%1</b>").arg(roomCode));
        m_activeSessionCard->setVisible(true);
    } else {
        m_currentActiveRoomCode.clear();
        m_activeSessionCard->setVisible(false);
    }
}

void QtDiscoveryWidget::onGlobalJoinWithFileClicked() {
    if (!m_globalRoomInput) return;
    QString input = m_globalRoomInput->text().trimmed();
    if (input.isEmpty()) {
        QMessageBox::information(this, "Введіть код кімнати", "Будь ласка, введіть 8-символьний код кімнати від отримувача (наприклад: DROP-K8M4-P9X2 або посилання з браузера).");
        return;
    }

    std::string err;
    std::string norm = WebRtcSessionCoordinator::normalizeRoomCode(input.toStdString(), &err);
    if (norm.empty()) {
        QMessageBox::warning(this, "Некоректний код кімнати", QString::fromStdString("Помилка валідації коду: " + err));
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(this, "Оберіть файл для надсилання в кімнату");
    if (filePath.isEmpty()) return;

    emit joinRoomWithFileRequested(QString::fromStdString(norm), filePath);
}

void QtDiscoveryWidget::onActiveSessionSendClicked() {
    if (m_currentActiveRoomCode.isEmpty()) return;
    QString filePath = QFileDialog::getOpenFileName(this, "Оберіть файл для надсилання підключеному пристрою");
    if (filePath.isEmpty()) return;
    emit sendFileRequested("webrtc:" + m_currentActiveRoomCode, filePath);
}

void QtDiscoveryWidget::onActiveSessionCloseClicked() {
    if (m_currentActiveRoomCode.isEmpty()) return;
    QString code = m_currentActiveRoomCode;
    updateGlobalSession(QString(), false);
    emit closeGlobalSessionRequested(code);
}

void QtDiscoveryWidget::onDeviceRemoveClicked(const QString& peerId) {
    if (m_presenter) {
        m_presenter->onRemovePeerClicked(peerId.toStdString());
    }
}

}
