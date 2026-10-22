#include "landrop/ui/qt/QtDesktopApp.hpp"
#include "landrop/ui/qt/QtDiscoveryWidget.hpp"
#include "landrop/ui/qt/QtTransferWidget.hpp"
#include "landrop/ui/qt/QtStorageWidget.hpp"
#include "landrop/ui/qt/QtSettingsWidget.hpp"

#include <QHBoxLayout>
#include <QFile>
#include <QTextStream>
#include <QApplication>

namespace landrop::ui {

QtDesktopApp::QtDesktopApp(QWidget* parent)
    : QMainWindow(parent) {
    setupUi();
    setupPresenters();
    applyTheme("dark");
}

QtDesktopApp::~QtDesktopApp() = default;

void QtDesktopApp::setupUi() {
    setWindowTitle("LAN-Drop (Qt 6)");
    setMinimumSize(960, 640);
    resize(1080, 720);

    auto* central = new QWidget(this);
    auto* mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Sidebar
    m_sidebar = new QListWidget(central);
    m_sidebar->setObjectName("sidebar");
    m_sidebar->setFixedWidth(220);
    m_sidebar->setFocusPolicy(Qt::NoFocus);

    auto* itemRadar = new QListWidgetItem("📡  Radar", m_sidebar);
    auto* itemTransfers = new QListWidgetItem("⇄  Transfers", m_sidebar);
    auto* itemStorage = new QListWidgetItem("📁  Storage", m_sidebar);
    auto* itemSettings = new QListWidgetItem("⚙️  Settings", m_sidebar);

    QFont font = m_sidebar->font();
    font.setPointSize(13);
    m_sidebar->setFont(font);

    m_sidebar->addItem(itemRadar);
    m_sidebar->addItem(itemTransfers);
    m_sidebar->addItem(itemStorage);
    m_sidebar->addItem(itemSettings);

    connect(m_sidebar, &QListWidget::currentRowChanged, this, &QtDesktopApp::onNavigationItemChanged);
    mainLayout->addWidget(m_sidebar);

    // Stacked widget for the 4 sections
    m_stackedWidget = new QStackedWidget(central);

    m_discoveryWidget = new QtDiscoveryWidget(m_stackedWidget);
    m_transferWidget = new QtTransferWidget(m_stackedWidget);
    m_storageWidget = new QtStorageWidget(m_stackedWidget);
    m_settingsWidget = new QtSettingsWidget(m_stackedWidget);

    m_stackedWidget->addWidget(m_discoveryWidget);
    m_stackedWidget->addWidget(m_transferWidget);
    m_stackedWidget->addWidget(m_storageWidget);
    m_stackedWidget->addWidget(m_settingsWidget);

    mainLayout->addWidget(m_stackedWidget, 1);
    setCentralWidget(central);

    // Inter-screen signal wiring
    connect(m_discoveryWidget, &QtDiscoveryWidget::sendFileRequested, this, [this](const QString& peerId, const QString& filePath) {
        m_sidebar->setCurrentRow(1); // Switch to Transfers
        m_transferWidget->initiateSendFile(peerId.toStdString(), filePath.toStdString());
    });

    connect(m_settingsWidget, &QtSettingsWidget::themeChanged, this, &QtDesktopApp::applyTheme);

    // Status bar
    statusBar()->showMessage("LAN-Drop Qt 6 Engine Ready • Beacon Active");
    m_sidebar->setCurrentRow(0);
}

void QtDesktopApp::setupPresenters() {
    m_discoveryPresenter = std::make_unique<DiscoveryPresenter>(m_discoveryWidget);
    m_discoveryWidget->setPresenter(m_discoveryPresenter.get());

    m_transferPresenter = std::make_unique<TransferPresenter>(m_transferWidget);
    m_transferWidget->setPresenter(m_transferPresenter.get());

    m_storagePresenter = std::make_unique<StoragePresenter>(m_storageWidget);
    m_storageWidget->setPresenter(m_storagePresenter.get());

    m_settingsPresenter = std::make_unique<SettingsPresenter>(m_settingsWidget);
    m_settingsWidget->setPresenter(m_settingsPresenter.get());

    m_discoveryPresenter->initialize();
    m_transferPresenter->initialize();
    m_storagePresenter->initialize();
    m_settingsPresenter->initialize();

    std::string ip = m_discoveryPresenter->getLocalDeviceIp();
    uint16_t port = m_discoveryPresenter->getLocalDevicePort();
    statusBar()->showMessage(QString("My Device: %1:%2 • Discoverable on Local Network").arg(QString::fromStdString(ip)).arg(port));
}

void QtDesktopApp::onNavigationItemChanged(int row) {
    if (row >= 0 && row < m_stackedWidget->count()) {
        m_stackedWidget->setCurrentIndex(row);
    }
}

void QtDesktopApp::applyTheme(const QString& themeName) {
    QFile qssFile("resources/styles.qss");
    if (!qssFile.exists()) {
        // Fallback search relative to binary
        qssFile.setFileName("../resources/styles.qss");
    }

    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&qssFile);
        QString fullQss = stream.readAll();
        qssFile.close();

        QString themeClass = (themeName == "light") ? "light-theme" : "dark-theme";
        setProperty("class", themeClass);
        centralWidget()->setProperty("class", themeClass);

        qApp->setStyleSheet(fullQss);
        style()->unpolish(this);
        style()->polish(this);
        centralWidget()->style()->unpolish(centralWidget());
        centralWidget()->style()->polish(centralWidget());
    }
}

}
