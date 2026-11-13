#include "landrop/ui/qt/QtDesktopApp.hpp"
#include "landrop/ui/qt/QtDiscoveryWidget.hpp"
#include "landrop/ui/qt/QtTransferWidget.hpp"
#include "landrop/ui/qt/QtStorageWidget.hpp"
#include "landrop/ui/qt/QtSettingsWidget.hpp"
#include "landrop/ui/qt/QtAccountWidget.hpp"
#include "landrop/ui/QrCodeGenerator.hpp"
#include "landrop/tasks/P2PTransferTask.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFile>
#include <QTextStream>
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QDir>
#include <thread>

namespace landrop::ui {

QtDesktopApp::QtDesktopApp(QWidget* parent)
    : QMainWindow(parent) {
    m_transferEngine = std::make_unique<TransferEngine>();
    setupUi();
    setupPresenters();
    applyTheme("dark");
}

QtDesktopApp::~QtDesktopApp() {
    *m_aliveToken = false;
    if (m_localWebBridge) {
        m_localWebBridge->stop();
    }
    if (m_transferEngine) {
        m_transferEngine->stopServer();
    }
#if defined(LANDROP_HAVE_WEBRTC)
    {
        std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
        for (auto& [id, coord] : m_globalCoordinators) {
            if (coord) {
                coord->cancel();
            }
        }
        m_globalCoordinators.clear();
    }
#endif
    {
        std::lock_guard<std::mutex> lock(m_workerThreadsMutex);
        for (auto& th : m_workerThreads) {
            if (th.joinable()) {
                th.join();
            }
        }
        m_workerThreads.clear();
    }
}

void QtDesktopApp::launchWorker(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(m_workerThreadsMutex);
    m_workerThreads.emplace_back([aliveToken = m_aliveToken, task = std::move(task)]() {
        if (aliveToken && *aliveToken) {
            task();
        }
    });
}

void QtDesktopApp::initialize() {
    // Presenters and UI initialized in constructor
}


void QtDesktopApp::setupUi() {
    setWindowTitle("LAN-Drop (Qt 6)");
    setWindowIcon(QIcon(":/resources/lan-drop.svg"));
    setMinimumSize(800, 540);
    resize(1080, 700);

    auto* central = new QWidget(this);
    central->setObjectName("central");
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
    auto* itemAccount = new QListWidgetItem("👤  Account", m_sidebar);

    QFont font = m_sidebar->font();
    font.setPointSize(13);
    m_sidebar->setFont(font);

    m_sidebar->addItem(itemRadar);
    m_sidebar->addItem(itemTransfers);
    m_sidebar->addItem(itemStorage);
    m_sidebar->addItem(itemSettings);
    m_sidebar->addItem(itemAccount);

    connect(m_sidebar, &QListWidget::currentRowChanged, this, &QtDesktopApp::onNavigationItemChanged);
    mainLayout->addWidget(m_sidebar);

    // Stacked widget for the 5 sections
    m_stackedWidget = new QStackedWidget(central);

    m_discoveryWidget = new QtDiscoveryWidget(m_stackedWidget);
    m_transferWidget = new QtTransferWidget(m_stackedWidget);
    m_storageWidget = new QtStorageWidget(m_stackedWidget);
    m_settingsWidget = new QtSettingsWidget(m_stackedWidget);
    m_accountWidget = new QtAccountWidget(m_stackedWidget);

    m_stackedWidget->addWidget(m_discoveryWidget);
    m_stackedWidget->addWidget(m_transferWidget);
    m_stackedWidget->addWidget(m_storageWidget);
    m_stackedWidget->addWidget(m_settingsWidget);
    m_stackedWidget->addWidget(m_accountWidget);

    mainLayout->addWidget(m_stackedWidget, 1);
    setCentralWidget(central);

    // Inter-screen signal wiring
    connect(m_discoveryWidget, &QtDiscoveryWidget::sendFileRequested, this, [this](const QString& peerId, const QString& filePath) {
        m_sidebar->setCurrentRow(1); // Switch to Transfers
        if (m_transferPresenter) {
            m_transferPresenter->onSendFileClicked(peerId.toStdString(), filePath.toStdString());
        }
    });

    connect(m_discoveryWidget, &QtDiscoveryWidget::hostRoomRequested, this, &QtDesktopApp::onHostRoomRequested);
    connect(m_discoveryWidget, &QtDiscoveryWidget::joinRoomRequested, this, &QtDesktopApp::onJoinRoomRequested);
    connect(m_discoveryWidget, &QtDiscoveryWidget::joinRoomWithFileRequested, this, &QtDesktopApp::onJoinRoomWithFileRequested);
    connect(m_discoveryWidget, &QtDiscoveryWidget::closeGlobalSessionRequested, this, &QtDesktopApp::onCloseGlobalSessionRequested);

    connect(m_settingsWidget, &QtSettingsWidget::themeChanged, this, &QtDesktopApp::applyTheme);

    connect(m_accountWidget, &QtAccountWidget::sendFileToContact, this, [this](const QString& /*targetUserId*/, const QString& landropId) {
        m_sidebar->setCurrentRow(1); // Switch to Transfers
        if (m_transferPresenter) {
            m_transferPresenter->onSendFileClicked(landropId.toStdString(), "");
        }
    });

    // Status bar
    statusBar()->showMessage("LAN-Drop Qt 6 Engine Ready • Beacon Active");
    m_sidebar->setCurrentRow(0);
}

void QtDesktopApp::setupPresenters() {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);

    const char* envSignal = std::getenv("LANDROP_SIGNALING_URL");
    std::string identityBase = envSignal ? envSignal : "https://landrop-server.duckdns.org";
    if (identityBase.rfind("ws://", 0) == 0) {
        identityBase = "http://" + identityBase.substr(5);
    } else if (identityBase.rfind("wss://", 0) == 0) {
        identityBase = "https://" + identityBase.substr(6);
    }
    size_t wsPos = identityBase.find("/ws");
    if (wsPos != std::string::npos) {
        identityBase = identityBase.substr(0, wsPos);
    }

    m_identityClient = std::make_shared<IdentityClient>(nullptr, identityBase);
    std::string sessionFile = (configDir + "/session.json").toStdString();
    m_identityClient->setSessionStorePath(sessionFile);
    m_identityClient->loadSession();

    m_accountPresenter = std::make_unique<AccountPresenter>(m_identityClient);
    if (!m_identityClient->isGuest()) {
        m_accountPresenter->refreshDevices();
        m_accountPresenter->refreshContacts();
        m_accountPresenter->refreshHistory();
    }
    if (m_accountWidget) {
        m_accountWidget->setPresenter(m_accountPresenter.get());
    }

    m_discoveryPresenter = std::make_unique<DiscoveryPresenter>(m_discoveryWidget);
    m_discoveryWidget->setPresenter(m_discoveryPresenter.get());

    m_transferPresenter = std::make_unique<TransferPresenter>(m_transferWidget);
    m_transferWidget->setPresenter(m_transferPresenter.get());

    QString standardShared = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/LAN-Drop-Shared";
    if (standardShared.isEmpty()) {
        standardShared = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/LAN-Drop-Shared";
    }
    if (standardShared.isEmpty()) {
        standardShared = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/shared_drop";
    }
    QDir().mkpath(standardShared);

    m_storagePresenter = std::make_unique<StoragePresenter>(m_storageWidget, standardShared.toStdString());
    m_storageWidget->setPresenter(m_storagePresenter.get());
    m_storageWidget->setSharedPath(standardShared.toStdString());

    m_settingsPresenter = std::make_unique<SettingsPresenter>(m_settingsWidget);
    m_settingsWidget->setPresenter(m_settingsPresenter.get());

    m_settingsPresenter->setConfigPath((configDir + "/settings.json").toStdString());

    m_transferPresenter->setStateStore(std::make_shared<TransferStateStore>((configDir + "/transfer_journal.json").toStdString()));

    m_discoveryPresenter->initialize();
    m_transferPresenter->initialize(false);
    m_storagePresenter->initialize();
    m_settingsPresenter->initialize();

    // Sync download directory from settings
    QString defaultDownloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (defaultDownloads.isEmpty()) {
        defaultDownloads = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/Downloads";
    }
    if (defaultDownloads.isEmpty()) {
        defaultDownloads = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/downloads";
    }
    QDir().mkpath(defaultDownloads);

    if (m_settingsPresenter) {
        std::string cfgDl = m_settingsPresenter->getDownloadsPath();
        if (cfgDl.empty() || cfgDl == "./downloads") {
            m_downloadsDirectory = defaultDownloads.toStdString();
            m_settingsPresenter->setDownloadsPath(m_downloadsDirectory);
        } else {
            m_downloadsDirectory = cfgDl;
        }
    } else {
        m_downloadsDirectory = defaultDownloads.toStdString();
    }

    if (m_transferWidget) {
        m_transferWidget->setDownloadsDirectory(m_downloadsDirectory);
    }
    if (m_settingsWidget) {
        m_settingsWidget->setDownloadsPath(m_downloadsDirectory);
    }

    m_transferPresenter->setSpeedLimit(m_settingsPresenter->getSpeedLimitKbps());

    // Connect transfer presenter to real TransferEngine
    m_transferPresenter->setSendFileCallback([this](const std::string& taskId, const std::string& targetPeerId, const std::string& filePath) {
        handleSendFile(taskId, targetPeerId, filePath);
    });

    m_transferPresenter->setSendBatchCallback([this](const std::string& taskId, const std::string& targetPeerId, const std::vector<BatchFileItem>& items, const std::string& batchName) {
        handleSendBatch(taskId, targetPeerId, items, batchName);
    });

    m_transferPresenter->setCancelCallback([this](const std::string& taskId) {
        if (m_transferEngine) {
            m_transferEngine->cancelTask(taskId);
        }
    });

    startBackgroundTransferServer();

    // Peer provider connects discovered peers to Transfer tab
    m_transferWidget->setPeerProvider([this]() {
        std::vector<std::pair<std::string, std::string>> result;
        if (m_discoveryPresenter) {
            for (const auto& p : m_discoveryPresenter->getPeers()) {
                result.emplace_back(p->getId(), p->getDisplayName() + " (" + p->getEndpointInfo() + ")");
            }
        }
        return result;
    });

    // Save directory change from Transfer tab
    connect(m_transferWidget, &QtTransferWidget::downloadsDirectoryChanged, this, [this](const QString& newPath) {
        m_downloadsDirectory = newPath.toStdString();
        if (m_settingsPresenter) {
            m_settingsPresenter->onDownloadsPathChanged(m_downloadsDirectory);
            m_settingsPresenter->saveSettings();
        }
        if (m_settingsWidget) {
            m_settingsWidget->setDownloadsPath(m_downloadsDirectory);
        }
        if (m_transferEngine) {
            m_transferEngine->setSaveDirectory(m_downloadsDirectory);
        }
        statusBar()->showMessage(QString("✓ Папку збереження оновлено: %1").arg(newPath), 4000);
    });

    // Settings change synchronization (speed limit, port, downloads directory)
    connect(m_settingsWidget->getSaveBtn(), &QPushButton::clicked, this, [this]() {
        if (!m_settingsPresenter) return;
        if (m_transferPresenter) {
            m_transferPresenter->setSpeedLimit(m_settingsPresenter->getSpeedLimitKbps());
        }
        std::string newDl = m_settingsPresenter->getDownloadsPath();
        if (!newDl.empty()) {
            m_downloadsDirectory = newDl;
            if (m_transferWidget) {
                m_transferWidget->setDownloadsDirectory(m_downloadsDirectory);
            }
            if (m_transferEngine) {
                m_transferEngine->setSaveDirectory(m_downloadsDirectory);
            }
        }
        uint16_t newPort = m_settingsPresenter->getPort();
        if (m_discoveryPresenter) {
            m_discoveryPresenter->setLocalDevicePort(newPort);
        }
        if (m_transferEngine && m_transferEngine->isServerRunning()) {
            if (m_transferEngine->getServerPort() != newPort) {
                m_transferEngine->stopServer();
                startBackgroundTransferServer();
            }
        }
    });

    // Periodic transfer engine tick timer for smooth UI progress
    m_transferTickTimer = new QTimer(this);
    connect(m_transferTickTimer, &QTimer::timeout, this, [this]() {
        if (m_transferPresenter) {
            m_transferPresenter->tick();
        }
    });
    m_transferTickTimer->start(300);

    // Start Local Web Bridge for browser-desktop interop (Workstream E2)
    m_localWebBridge = std::make_unique<LocalWebBridge>(8849);
    m_localWebBridge->setStorageDirectory(m_downloadsDirectory);
    m_localWebBridge->start([this]() {
        std::vector<BridgePeerInfo> list;
        if (m_discoveryPresenter) {
            for (const auto& p : m_discoveryPresenter->getPeers()) {
                list.push_back({p->getId(), p->getDisplayName(), p->getIpAddress(), p->getPort(), p->getPeerType()});
            }
        }
        return list;
    });

    std::string ip = m_discoveryPresenter->getLocalDeviceIp();
    uint16_t port = m_discoveryPresenter->getLocalDevicePort();
    statusBar()->showMessage(QString("My Device: %1:%2 • Discoverable on Local Network • Bridge Active").arg(QString::fromStdString(ip)).arg(port));
}

void QtDesktopApp::onNavigationItemChanged(int row) {
    if (row >= 0 && row < m_stackedWidget->count()) {
        m_stackedWidget->setCurrentIndex(row);
    }
}

void QtDesktopApp::applyTheme(const QString& themeName) {
    QString normTheme = themeName.toLower();
    QStringList candidates = {
        ":/resources/styles.qss",
        "resources/styles.qss",
        "../resources/styles.qss",
        QCoreApplication::applicationDirPath() + "/resources/styles.qss",
        QCoreApplication::applicationDirPath() + "/../resources/styles.qss",
        QCoreApplication::applicationDirPath() + "/../share/landrop/styles.qss",
        "/usr/share/landrop/styles.qss",
        "/usr/local/share/landrop/styles.qss"
    };

    QString fullQss;
    for (const auto& path : candidates) {
        QFile qssFile(path);
        if (qssFile.exists() && qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&qssFile);
            fullQss = stream.readAll();
            qssFile.close();
            break;
        }
    }

    QString themeClass = (normTheme == "light") ? "light-theme" : "dark-theme";
    setProperty("class", themeClass);
    if (centralWidget()) {
        centralWidget()->setProperty("class", themeClass);
    }
    if (m_stackedWidget) {
        m_stackedWidget->setProperty("class", themeClass);
    }

    if (!fullQss.isEmpty()) {
        qApp->setStyleSheet(fullQss);
    } else {
        if (normTheme == "light") {
            setStyleSheet("background: #f6f7fb; color: #1c1c1e;");
        } else {
            setStyleSheet("background: #1e1e24; color: #f3f3f5;");
        }
    }

    style()->unpolish(this);
    style()->polish(this);
    if (centralWidget()) {
        centralWidget()->style()->unpolish(centralWidget());
        centralWidget()->style()->polish(centralWidget());
    }
}

void QtDesktopApp::startBackgroundTransferServer() {
    if (!m_transferEngine) return;

    if (m_downloadsDirectory.empty()) {
        QString dlPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (dlPath.isEmpty()) {
            dlPath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/Downloads";
        }
        if (dlPath.isEmpty()) {
            dlPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/downloads";
        }
        m_downloadsDirectory = dlPath.toStdString();
    }
    QDir().mkpath(QString::fromStdString(m_downloadsDirectory));
    if (m_transferWidget) {
        m_transferWidget->setDownloadsDirectory(m_downloadsDirectory);
    }

    uint16_t listenPort = m_settingsPresenter ? m_settingsPresenter->getPort() : 8848;
    if (listenPort == 0) listenPort = 8848;

    if (m_discoveryPresenter) {
        m_discoveryPresenter->setLocalDevicePort(listenPort);
    }

    TransferOptions opts;
    opts.autoRenameDuplicates = true;
    if (m_settingsPresenter) {
        opts.speedLimitKbps = m_settingsPresenter->getSpeedLimitKbps();
    }

    bool started = m_transferEngine->startServer(
        listenPort,
        m_downloadsDirectory,
        opts,
        [this](const std::string& taskId, const std::string& fileName, uint64_t fileSize, const std::string& senderIp) -> bool {
            QMetaObject::invokeMethod(this, [this, taskId, fileName, fileSize, senderIp]() {
                if (m_transferPresenter) {
                    auto task = std::make_shared<P2PTransferTask>(taskId, fileName, fileSize, TransferDirection::Download, senderIp);
                    task->initialize();
                    task->setState(TransferState::Transferring);
                    m_transferPresenter->addRealTask(task);
                }
                statusBar()->showMessage(QString("Incoming transfer from %1: %2").arg(QString::fromStdString(senderIp)).arg(QString::fromStdString(fileName)));
            }, Qt::QueuedConnection);
            return true;
        },
        [this](const TransferProgressUpdate& upd) {
            QMetaObject::invokeMethod(this, [this, upd]() {
                if (m_transferPresenter) {
                    m_transferPresenter->updateTaskProgress(upd.taskId, upd.bytesTransferred, upd.speedBytesPerSec, upd.state, upd.message);
                }
            }, Qt::QueuedConnection);
        },
        [this](const std::string& taskId, const std::string& fileName, bool success, const std::string& savedPath, const std::string& error) {
            recordTransferHistory(taskId, "download", "LAN Direct", fileName, 0, success, "TcpDirect");
            QMetaObject::invokeMethod(this, [this, taskId, fileName, success, savedPath, error]() {
                if (success) {
                    statusBar()->showMessage(QString("✓ Received %1 (%2)").arg(QString::fromStdString(fileName)).arg(QString::fromStdString(savedPath)));
                } else {
                    statusBar()->showMessage(QString("✗ Transfer failed: %1").arg(QString::fromStdString(error)));
                    if (m_transferWidget) {
                        m_transferWidget->showTransferError(taskId, error);
                    }
                }
            }, Qt::QueuedConnection);
        }
    );

    if (!started) {
        statusBar()->showMessage(QString("Notice: Receiver port %1 unavailable. Direct sends still operational.").arg(listenPort));
    }
}

void QtDesktopApp::handleSendFile(const std::string& taskId, const std::string& targetPeerId, const std::string& filePath) {
    std::string remoteIp = "127.0.0.1";
    uint16_t remotePort = 8848;

    if (targetPeerId.rfind("webrtc:", 0) == 0) {
        std::shared_ptr<WebRtcSessionCoordinator> coordinator;
        {
            std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
            auto it = m_globalCoordinators.find(targetPeerId);
            if (it != m_globalCoordinators.end()) {
                coordinator = it->second;
            }
        }
        if (coordinator) {
            auto channel = coordinator->getChannel();
            if (channel && channel->isConnected()) {
                statusBar()->showMessage(QString("Streaming file over WebRTC to %1...").arg(QString::fromStdString(targetPeerId)));
                launchWorker([this, taskId, filePath, targetPeerId, channel]() {
                    // Temporarily signal standby receiver on this channel to yield
                    std::shared_ptr<std::atomic<bool>> rxCancel;
                    {
                        std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
                        auto it = m_activeRxCancelTokens.find(targetPeerId);
                        if (it != m_activeRxCancelTokens.end()) {
                            rxCancel = it->second;
                        }
                    }
                    if (rxCancel) {
                        rxCancel->store(true);
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }

                    TransferOptions opts;
                    opts.closeChannelOnCompletion = false;
                    if (m_settingsPresenter) {
                        opts.speedLimitKbps = m_settingsPresenter->getSpeedLimitKbps();
                    }

                    std::string err;
                    bool ok = m_transferEngine->sendFileOverChannel(
                        taskId,
                        filePath,
                        channel,
                        opts,
                        [this](const TransferProgressUpdate& upd) {
                            QMetaObject::invokeMethod(this, [this, upd]() {
                                if (m_transferPresenter) {
                                    m_transferPresenter->updateTaskProgress(upd.taskId, upd.bytesTransferred, upd.speedBytesPerSec, upd.state, upd.message);
                                }
                            }, Qt::QueuedConnection);
                        },
                        nullptr,
                        &err
                    );

                    // Restore standby receiver
                    if (rxCancel) {
                        rxCancel->store(false);
                    }

                    recordTransferHistory(taskId, "upload", targetPeerId, QFileInfo(QString::fromStdString(filePath)).fileName().toStdString(), QFileInfo(QString::fromStdString(filePath)).size(), ok, "WebRtcDataChannel");
                    QMetaObject::invokeMethod(this, [this, taskId, ok, err, targetPeerId]() {
                        if (ok) {
                            statusBar()->showMessage(QString("✓ File successfully sent over WebRTC to %1").arg(QString::fromStdString(targetPeerId)));
                        } else {
                            statusBar()->showMessage(QString("✗ WebRTC send error: %1").arg(QString::fromStdString(err)));
                            if (m_transferWidget) {
                                m_transferWidget->showTransferError(taskId, err);
                            }
                        }
                    }, Qt::QueuedConnection);
                });
                return;
            }
        }
    }

    if (targetPeerId.rfind("direct:", 0) == 0) {
        size_t colon = targetPeerId.rfind(':');
        if (colon != std::string::npos && colon > 7) {
            remoteIp = targetPeerId.substr(7, colon - 7);
            try {
                remotePort = static_cast<uint16_t>(std::stoi(targetPeerId.substr(colon + 1)));
            } catch (...) {
                remotePort = 8848;
            }
        }
    } else if (targetPeerId != "local-loopback") {
        if (m_discoveryPresenter) {
            for (const auto& p : m_discoveryPresenter->getPeers()) {
                if (p->getId() == targetPeerId) {
                    remoteIp = p->getIpAddress();
                    remotePort = p->getPort();
                    break;
                }
            }
        }
    }

    statusBar()->showMessage(QString("Connecting to %1:%2 to send file...").arg(QString::fromStdString(remoteIp)).arg(remotePort));

    launchWorker([this, taskId, filePath, remoteIp, remotePort, targetPeerId]() {
        TransferOptions opts;
        if (m_settingsPresenter) {
            opts.speedLimitKbps = m_settingsPresenter->getSpeedLimitKbps();
        }

        std::string err;
        bool ok = m_transferEngine->sendFile(
            taskId,
            filePath,
            remoteIp,
            remotePort,
            opts,
            [this](const TransferProgressUpdate& upd) {
                QMetaObject::invokeMethod(this, [this, upd]() {
                    if (m_transferPresenter) {
                        m_transferPresenter->updateTaskProgress(upd.taskId, upd.bytesTransferred, upd.speedBytesPerSec, upd.state, upd.message);
                    }
                }, Qt::QueuedConnection);
            },
            nullptr,
            &err
        );

        recordTransferHistory(taskId, "upload", targetPeerId, QFileInfo(QString::fromStdString(filePath)).fileName().toStdString(), QFileInfo(QString::fromStdString(filePath)).size(), ok, "TcpDirect");
        QMetaObject::invokeMethod(this, [this, taskId, ok, err, remoteIp, remotePort]() {
            if (ok) {
                statusBar()->showMessage(QString("✓ File successfully sent to %1:%2").arg(QString::fromStdString(remoteIp)).arg(remotePort));
            } else {
                statusBar()->showMessage(QString("✗ Send error: %1").arg(QString::fromStdString(err)));
                if (m_transferWidget) {
                    m_transferWidget->showTransferError(taskId, err);
                }
            }
        }, Qt::QueuedConnection);
    });
}

void QtDesktopApp::handleSendBatch(
    const std::string& taskId,
    const std::string& targetPeerId,
    const std::vector<BatchFileItem>& items,
    const std::string& batchName
) {
    std::string remoteIp = "127.0.0.1";
    uint16_t remotePort = 8848;

    if (targetPeerId.rfind("webrtc:", 0) == 0) {
        std::shared_ptr<WebRtcSessionCoordinator> coordinator;
        {
            std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
            auto it = m_globalCoordinators.find(targetPeerId);
            if (it != m_globalCoordinators.end()) {
                coordinator = it->second;
            }
        }
        if (coordinator) {
            auto channel = coordinator->getChannel();
            if (channel && channel->isConnected()) {
                statusBar()->showMessage(QString("Streaming batch over WebRTC to %1...").arg(QString::fromStdString(targetPeerId)));
                launchWorker([this, taskId, items, batchName, targetPeerId, channel]() {
                    TransferOptions opts;
                    opts.closeChannelOnCompletion = false;
                    if (m_settingsPresenter) {
                        opts.speedLimitKbps = m_settingsPresenter->getSpeedLimitKbps();
                    }

                    std::string err;
                    bool ok = m_transferEngine->sendBatchOverChannel(
                        taskId,
                        items,
                        batchName,
                        channel,
                        opts,
                        [this](const TransferProgressUpdate& upd) {
                            QMetaObject::invokeMethod(this, [this, upd]() {
                                if (m_transferPresenter) {
                                    m_transferPresenter->updateTaskProgress(upd.taskId, upd.bytesTransferred, upd.speedBytesPerSec, upd.state, upd.message);
                                }
                            }, Qt::QueuedConnection);
                        },
                        nullptr,
                        &err
                    );

                    uint64_t totalBytes = 0;
                    for (const auto& itm : items) totalBytes += itm.fileSize;
                    recordTransferHistory(taskId, "upload", targetPeerId, batchName, totalBytes, ok, "WebRtcDataChannel");

                    QMetaObject::invokeMethod(this, [this, taskId, ok, err, targetPeerId, batchName]() {
                        if (ok) {
                            statusBar()->showMessage(QString("✓ Batch '%1' successfully sent over WebRTC to %2").arg(QString::fromStdString(batchName)).arg(QString::fromStdString(targetPeerId)));
                        } else {
                            statusBar()->showMessage(QString("✗ WebRTC batch send error: %1").arg(QString::fromStdString(err)));
                            if (m_transferWidget) {
                                m_transferWidget->showTransferError(taskId, err);
                            }
                        }
                    }, Qt::QueuedConnection);
                });
                return;
            }
        }
    }

    if (targetPeerId.rfind("direct:", 0) == 0) {
        size_t colon = targetPeerId.rfind(':');
        if (colon != std::string::npos && colon > 7) {
            remoteIp = targetPeerId.substr(7, colon - 7);
            try {
                remotePort = static_cast<uint16_t>(std::stoi(targetPeerId.substr(colon + 1)));
            } catch (...) {
                remotePort = 8848;
            }
        }
    } else if (targetPeerId != "local-loopback") {
        if (m_discoveryPresenter) {
            for (const auto& p : m_discoveryPresenter->getPeers()) {
                if (p->getId() == targetPeerId) {
                    remoteIp = p->getIpAddress();
                    remotePort = p->getPort();
                    break;
                }
            }
        }
    }

    statusBar()->showMessage(QString("Connecting to %1:%2 to send batch '%3' (%4 files)...")
        .arg(QString::fromStdString(remoteIp))
        .arg(remotePort)
        .arg(QString::fromStdString(batchName))
        .arg(items.size()));

    launchWorker([this, taskId, items, batchName, remoteIp, remotePort, targetPeerId]() {
        TransferOptions opts;
        if (m_settingsPresenter) {
            opts.speedLimitKbps = m_settingsPresenter->getSpeedLimitKbps();
        }

        std::string err;
        bool ok = m_transferEngine->sendBatch(
            taskId,
            items,
            batchName,
            remoteIp,
            remotePort,
            opts,
            [this](const TransferProgressUpdate& upd) {
                QMetaObject::invokeMethod(this, [this, upd]() {
                    if (m_transferPresenter) {
                        m_transferPresenter->updateTaskProgress(upd.taskId, upd.bytesTransferred, upd.speedBytesPerSec, upd.state, upd.message);
                    }
                }, Qt::QueuedConnection);
            },
            nullptr,
            &err
        );

        uint64_t totalBytes = 0;
        for (const auto& itm : items) totalBytes += itm.fileSize;
        recordTransferHistory(taskId, "upload", targetPeerId, batchName, totalBytes, ok, "TcpDirect");

        QMetaObject::invokeMethod(this, [this, taskId, ok, err, remoteIp, remotePort, batchName]() {
            if (ok) {
                statusBar()->showMessage(QString("✓ Batch '%1' successfully sent to %2:%3")
                    .arg(QString::fromStdString(batchName))
                    .arg(QString::fromStdString(remoteIp))
                    .arg(remotePort));
            } else {
                statusBar()->showMessage(QString("✗ Batch send error: %1").arg(QString::fromStdString(err)));
                if (m_transferWidget) {
                    m_transferWidget->showTransferError(taskId, err);
                }
            }
        }, Qt::QueuedConnection);
    });
}

void QtDesktopApp::recordTransferHistory(
    const std::string& taskId,
    const std::string& direction,
    const std::string& peerId,
    const std::string& fileName,
    uint64_t fileSize,
    bool success,
    const std::string& transportType
) {
    if (!m_accountPresenter || m_accountPresenter->isGuest()) return;

    TransferHistoryItem item;
    item.transferId = taskId;
    item.direction = direction;
    item.peerLandropId = peerId;
    item.peerDeviceName = peerId;
    item.status = success ? "COMPLETED" : "FAILED";
    item.transportType = transportType;
    item.totalBytes = fileSize;

    if (!fileName.empty()) {
        TransferFileMetadata fm;
        fm.fileName = fileName;
        fm.fileSize = fileSize;
        item.files.push_back(fm);
    }

    launchWorker([this, item]() {
        if (m_accountPresenter) {
            std::string err;
            m_accountPresenter->recordTransfer(item, &err);
        }
    });
}

[[maybe_unused]] static std::string deriveWsHttpUrl(const std::string& wsUrl) {
    std::string httpUrl = wsUrl;
    if (httpUrl.rfind("wss://", 0) == 0) {
        httpUrl = "https://" + httpUrl.substr(6);
    } else if (httpUrl.rfind("ws://", 0) == 0) {
        httpUrl = "http://" + httpUrl.substr(5);
    }
    size_t pathPos = httpUrl.find('/', 8);
    if (pathPos != std::string::npos) {
        httpUrl = httpUrl.substr(0, pathPos);
    }
    return httpUrl;
}

void QtDesktopApp::onHostRoomRequested(const QString& filePath) {
#if !defined(LANDROP_HAVE_WEBRTC)
    (void)filePath;
    QMessageBox::information(this, "Global P2P Not Available",
        "WebRTC support is not compiled into this build. Rebuild with -DLANDROP_ENABLE_WEBRTC=ON.");
#else
    const char* envSignaling = std::getenv("LANDROP_SIGNALING_URL");
    std::string signalingWsUrl = (envSignaling && envSignaling[0] != '\0') ? envSignaling : "wss://landrop-server.duckdns.org/ws/signaling";

    CoordinatorOptions opts;
    opts.signalingHttpUrl = deriveWsHttpUrl(signalingWsUrl);
    opts.autoFetchTurn = true;
    opts.dataChannelLabel = "lan-drop-data";
    if (m_identityClient && !m_identityClient->isGuest()) {
        opts.authToken = m_identityClient->getAuthToken();
        opts.userId = m_identityClient->getCurrentUser().userId;
        opts.deviceId = m_identityClient->getCurrentDevice().deviceId;
    }

    auto coordinator = std::make_shared<WebRtcSessionCoordinator>("gui-host", WebRtcConfig{}, opts);
    std::string tempPendingId = "host-pending-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    {
        std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
        m_pendingCoordinators[tempPendingId] = coordinator;
    }

    coordinator->onError = [this, coordinator](const std::string& err) {
        std::string code = coordinator->getRoomCode();
        {
            std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
            if (!code.empty()) {
                m_pendingCoordinators.erase("host:" + code);
            }
        }
        QMetaObject::invokeMethod(this, [this, err]() {
            statusBar()->showMessage(QString("✗ WebRTC Signaling Error: %1").arg(QString::fromStdString(err)), 7000);
        }, Qt::QueuedConnection);
    };

    coordinator->onRoomReady = [this, coordinator, filePath, signalingWsUrl, tempPendingId](const std::string& code) {
        {
            std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
            m_pendingCoordinators["host:" + code] = coordinator;
            m_pendingCoordinators.erase(tempPendingId);
        }
        QMetaObject::invokeMethod(this, [this, coordinator, code, filePath, signalingWsUrl]() {
            auto* dlg = new QDialog(this);
            dlg->setWindowTitle("Optical Pairing — Global P2P Room");
            dlg->setMinimumWidth(320);
            auto* l = new QVBoxLayout(dlg);
            l->setSpacing(10);
            l->setContentsMargins(16, 16, 16, 16);

            auto* title = new QLabel("<b>Scan QR with LAN-Drop Mobile or Web</b>", dlg);
            title->setAlignment(Qt::AlignCenter);

            // Construct Universal Pairing URL
            std::string httpBase = deriveWsHttpUrl(signalingWsUrl);
            if (httpBase.empty()) {
                httpBase = "https://drop.local";
            }
            std::string pairingUrl = httpBase + "/#" + code;

            // Generate crisp high-DPI QR Code (with 4-module quiet zone per ADR-015)
            auto qr = QrCodeGenerator::encodeText(pairingUrl);
            QPixmap qrPix = qr.toQPixmap(220, 4, Qt::black, Qt::white);

            auto* qrFrame = new QFrame(dlg);
            qrFrame->setStyleSheet("background: #ffffff; padding: 12px; border-radius: 10px; border: 1px solid #d2d2d7;");
            auto* qrFrameLayout = new QVBoxLayout(qrFrame);
            qrFrameLayout->setContentsMargins(0, 0, 0, 0);

            auto* qrLabel = new QLabel(qrFrame);
            qrLabel->setPixmap(qrPix);
            qrLabel->setAlignment(Qt::AlignCenter);
            qrFrameLayout->addWidget(qrLabel);

            auto* codeLbl = new QLabel(QString::fromStdString(code), dlg);
            QFont f = codeLbl->font();
            f.setPointSize(18);
            f.setBold(true);
            codeLbl->setFont(f);
            codeLbl->setAlignment(Qt::AlignCenter);
            codeLbl->setStyleSheet("color: #007aff; padding: 8px; background: #e5f1ff; border-radius: 8px;");

            QString promptText = filePath.isEmpty()
                ? "Waiting for peer to scan or connect..."
                : QString("Ready to send: %1\nWaiting for receiver to join...").arg(QFileInfo(filePath).fileName());
            auto* status = new QLabel(promptText, dlg);
            status->setAlignment(Qt::AlignCenter);
            status->setStyleSheet("color: #8e8e93; font-size: 12px;");

            auto* btnLayout = new QHBoxLayout();
            auto* copyCodeBtn = new QPushButton("Copy Code", dlg);
            connect(copyCodeBtn, &QPushButton::clicked, [code, status]() {
                QApplication::clipboard()->setText(QString::fromStdString(code));
                status->setText("✓ Room code copied to clipboard!");
            });

            auto* copyLinkBtn = new QPushButton("Copy Pairing Link", dlg);
            connect(copyLinkBtn, &QPushButton::clicked, [pairingUrl, status]() {
                QApplication::clipboard()->setText(QString::fromStdString(pairingUrl));
                status->setText("✓ Pairing link copied to clipboard!");
            });
            btnLayout->addWidget(copyCodeBtn);
            btnLayout->addWidget(copyLinkBtn);

            auto* closeBtn = new QPushButton("Close", dlg);
            connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::accept);

            l->addWidget(title);
            l->addWidget(qrFrame, 0, Qt::AlignCenter);
            l->addWidget(codeLbl);
            l->addWidget(status);
            l->addLayout(btnLayout);
            l->addWidget(closeBtn);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        }, Qt::QueuedConnection);
    };

    coordinator->onChannelOpened = [this, coordinator, filePath](std::shared_ptr<WebRtcDataChannel> channel) {
        std::string roomCode = coordinator->getRoomCode();
        std::string peerId = "webrtc:" + roomCode;
        {
            std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
            m_globalCoordinators[peerId] = coordinator;
            m_pendingCoordinators.erase("host:" + roomCode);
            m_activeRxCancelTokens[peerId] = std::make_shared<std::atomic<bool>>(false);
        }
        QMetaObject::invokeMethod(this, [this, peerId, roomCode]() {
            if (m_discoveryWidget) {
                m_discoveryWidget->updateGlobalSession(QString::fromStdString(roomCode), true);
            }
            if (m_discoveryPresenter) {
                m_discoveryPresenter->onGlobalPeerConnected(
                    peerId,
                    "WebRTC Peer (" + roomCode + ")",
                    "WebRTC P2P"
                );
            }
            statusBar()->showMessage(QString("✓ WebRTC Global P2P Peer Connected (%1)").arg(QString::fromStdString(roomCode)), 5000);
        }, Qt::QueuedConnection);

        if (!filePath.isEmpty()) {
            std::string pathStd = filePath.toStdString();
            std::string taskId = "rtc-tx-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
            QFileInfo fi(filePath);
            uint64_t fileSize = fi.size();
            std::string fileName = fi.fileName().toStdString();

            QMetaObject::invokeMethod(this, [this, taskId, fileName, fileSize, peerId]() {
                if (m_transferPresenter) {
                    auto task = std::make_shared<P2PTransferTask>(taskId, fileName, fileSize, TransferDirection::Upload, peerId);
                    task->initialize();
                    task->setState(TransferState::Transferring);
                    m_transferPresenter->addRealTask(task);
                }
            }, Qt::QueuedConnection);

            launchWorker([this, coordinator, channel, taskId, pathStd, peerId, filePath, fileName, fileSize]() {
                TransferOptions opts;
                opts.closeChannelOnCompletion = false;
                if (m_settingsPresenter) {
                    opts.speedLimitKbps = m_settingsPresenter->getSpeedLimitKbps();
                }
                std::string err;
                bool ok = m_transferEngine->sendFileOverChannel(
                    taskId,
                    pathStd,
                    channel,
                    opts,
                    [this](const TransferProgressUpdate& upd) {
                        QMetaObject::invokeMethod(this, [this, upd]() {
                            if (m_transferPresenter) {
                                m_transferPresenter->updateTaskProgress(upd.taskId, upd.bytesTransferred, upd.speedBytesPerSec, upd.state, upd.message);
                            }
                        }, Qt::QueuedConnection);
                    },
                    nullptr,
                    &err
                );
                recordTransferHistory(taskId, "upload", peerId, fileName, fileSize, ok, "WebRtcDataChannel");
                QMetaObject::invokeMethod(this, [this, ok, err, peerId, fileName]() {
                    if (ok) {
                        statusBar()->showMessage(QString("✓ File '%1' successfully sent over WebRTC to %2").arg(QString::fromStdString(fileName)).arg(QString::fromStdString(peerId)), 5000);
                    } else {
                        statusBar()->showMessage(QString("✗ WebRTC send error: %1").arg(QString::fromStdString(err)), 6000);
                    }
                }, Qt::QueuedConnection);

                // After sending, enter receive loop to allow bidirectional transfers
                auto rxCancel = [this, peerId]() -> std::shared_ptr<std::atomic<bool>> {
                    std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
                    auto it = m_activeRxCancelTokens.find(peerId);
                    return (it != m_activeRxCancelTokens.end()) ? it->second : nullptr;
                }();

                while (coordinator->getState() == CoordinatorState::Connected && channel && channel->isConnected()) {
                    if (rxCancel && rxCancel->load()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        continue;
                    }
                    TransferOptions rxOpts;
                    rxOpts.autoRenameDuplicates = true;
                    rxOpts.closeChannelOnCompletion = false;
                    rxOpts.headerTimeoutMs = 0;
                    std::string rxErr;
                    std::string rxTaskId = "rtc-rx-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
                    std::string destDir = getDownloadsDirectory();
                    bool rxOk = m_transferEngine->receiveFileOverChannel(
                        rxTaskId,
                        destDir,
                        channel,
                        rxOpts,
                        [this](const TransferProgressUpdate& upd) {
                            QMetaObject::invokeMethod(this, [this, upd]() {
                                if (m_transferPresenter) {
                                    m_transferPresenter->updateTaskProgress(upd.taskId, upd.bytesTransferred, upd.speedBytesPerSec, upd.state, upd.message);
                                }
                            }, Qt::QueuedConnection);
                        },
                        rxCancel.get(),
                        &rxErr,
                        [this, coordinator](const std::string& tid, const std::string& fn, uint64_t fs, const std::string&) -> bool {
                            QMetaObject::invokeMethod(this, [this, coordinator, tid, fn, fs]() {
                                if (m_transferPresenter) {
                                    std::string peerDisplay = "WebRTC Peer (" + coordinator->getRoomCode() + ")";
                                    auto task = std::make_shared<P2PTransferTask>(tid, fn, fs, TransferDirection::Download, peerDisplay);
                                    task->initialize();
                                    task->setState(TransferState::Transferring);
                                    m_transferPresenter->addRealTask(task);
                                }
                                statusBar()->showMessage(QString("Incoming WebRTC transfer from %1: %2 (%3 bytes)")
                                    .arg(QString::fromStdString(coordinator->getRoomCode()))
                                    .arg(QString::fromStdString(fn))
                                    .arg(fs));
                            }, Qt::QueuedConnection);
                            return true;
                        },
                        [this, coordinator](const std::string&, const std::string& rxFileName, bool success, const std::string& savedPath, const std::string& rxError) {
                            (void)savedPath;
                            recordTransferHistory("rtc-rx-" + rxFileName, "download", "WebRTC Peer (" + coordinator->getRoomCode() + ")", rxFileName, 0, success, "WebRtcDataChannel");
                            QMetaObject::invokeMethod(this, [this, rxFileName, success, rxError]() {
                                if (success) {
                                    statusBar()->showMessage(QString("✓ Received file: %1").arg(QString::fromStdString(rxFileName)), 5000);
                                } else if (!rxError.empty()) {
                                    statusBar()->showMessage(QString("✗ Receive error: %1").arg(QString::fromStdString(rxError)), 6000);
                                }
                            }, Qt::QueuedConnection);
                        }
                    );
                    if (!rxOk) {
                        if (rxCancel && rxCancel->load()) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                            continue;
                        }
                        break;
                    }
                }
            });
        } else {
            // Hosting in Receive / Standby mode: listen for incoming files continuously
            launchWorker([this, coordinator, channel, peerId]() {
                auto rxCancel = [this, peerId]() -> std::shared_ptr<std::atomic<bool>> {
                    std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
                    auto it = m_activeRxCancelTokens.find(peerId);
                    return (it != m_activeRxCancelTokens.end()) ? it->second : nullptr;
                }();

                while (coordinator->getState() == CoordinatorState::Connected && channel && channel->isConnected()) {
                    if (rxCancel && rxCancel->load()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        continue;
                    }
                    TransferOptions rxOpts;
                    rxOpts.autoRenameDuplicates = true;
                    rxOpts.closeChannelOnCompletion = false;
                    rxOpts.headerTimeoutMs = 0; // Wait indefinitely for incoming file transfer
                    std::string rxErr;
                    std::string taskId = "rtc-rx-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
                    std::string destDir = getDownloadsDirectory();
                    bool ok = m_transferEngine->receiveFileOverChannel(
                        taskId,
                        destDir,
                        channel,
                        rxOpts,
                        [this](const TransferProgressUpdate& upd) {
                            QMetaObject::invokeMethod(this, [this, upd]() {
                                if (m_transferPresenter) {
                                    m_transferPresenter->updateTaskProgress(upd.taskId, upd.bytesTransferred, upd.speedBytesPerSec, upd.state, upd.message);
                                }
                            }, Qt::QueuedConnection);
                        },
                        rxCancel.get(), // cancelFlag
                        &rxErr,  // outError
                        [this, coordinator](const std::string& tid, const std::string& fn, uint64_t fs, const std::string&) -> bool {
                            QMetaObject::invokeMethod(this, [this, coordinator, tid, fn, fs]() {
                                if (m_transferPresenter) {
                                    std::string peerDisplay = "WebRTC Peer (" + coordinator->getRoomCode() + ")";
                                    auto task = std::make_shared<P2PTransferTask>(tid, fn, fs, TransferDirection::Download, peerDisplay);
                                    task->initialize();
                                    task->setState(TransferState::Transferring);
                                    m_transferPresenter->addRealTask(task);
                                }
                                statusBar()->showMessage(QString("Incoming WebRTC transfer from %1: %2 (%3 bytes)")
                                    .arg(QString::fromStdString(coordinator->getRoomCode()))
                                    .arg(QString::fromStdString(fn))
                                    .arg(fs));
                            }, Qt::QueuedConnection);
                            return true;
                        },
                        [this, coordinator](const std::string&, const std::string& fileName, bool success, const std::string& savedPath, const std::string& err) {
                            (void)savedPath;
                            recordTransferHistory("rtc-rx-" + fileName, "download", "WebRTC Peer (" + coordinator->getRoomCode() + ")", fileName, 0, success, "WebRtcDataChannel");
                            QMetaObject::invokeMethod(this, [this, fileName, success, err]() {
                                if (success) {
                                    statusBar()->showMessage(QString("✓ Received file: %1").arg(QString::fromStdString(fileName)), 5000);
                                } else if (!err.empty()) {
                                    statusBar()->showMessage(QString("✗ Receive error: %1").arg(QString::fromStdString(err)), 6000);
                                }
                            }, Qt::QueuedConnection);
                        }
                    );
                    if (!ok) {
                        if (rxCancel && rxCancel->load()) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                            continue;
                        }
                        break;
                    }
                }
            });
        }
    };

    statusBar()->showMessage("Creating Global P2P Room on signaling server...");
    launchWorker([this, coordinator, signalingWsUrl]() {
        bool ok = coordinator->startAsInitiator(signalingWsUrl);
        if (!ok) {
            QMetaObject::invokeMethod(this, [this]() {
                statusBar()->showMessage("✗ Failed to connect to WebRTC signaling server", 6000);
            }, Qt::QueuedConnection);
        }
    });
#endif
}

void QtDesktopApp::onJoinRoomRequested(const QString& roomCode) {
#if !defined(LANDROP_HAVE_WEBRTC)
    (void)roomCode;
    QMessageBox::information(this, "Global P2P Not Available",
        "WebRTC support is not compiled into this build. Rebuild with -DLANDROP_ENABLE_WEBRTC=ON.");
#else
    const char* envSignaling = std::getenv("LANDROP_SIGNALING_URL");
    std::string signalingWsUrl = (envSignaling && envSignaling[0] != '\0') ? envSignaling : "wss://landrop-server.duckdns.org/ws/signaling";

    CoordinatorOptions opts;
    opts.signalingHttpUrl = deriveWsHttpUrl(signalingWsUrl);
    opts.autoFetchTurn = true;
    opts.dataChannelLabel = "lan-drop-data";
    if (m_identityClient && !m_identityClient->isGuest()) {
        opts.authToken = m_identityClient->getAuthToken();
        opts.userId = m_identityClient->getCurrentUser().userId;
        opts.deviceId = m_identityClient->getCurrentDevice().deviceId;
    }

    auto coordinator = std::make_shared<WebRtcSessionCoordinator>("gui-joiner", WebRtcConfig{}, opts);
    {
        std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
        m_pendingCoordinators["join:" + roomCode.toStdString()] = coordinator;
    }

    coordinator->onError = [this, roomCode](const std::string& err) {
        {
            std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
            m_pendingCoordinators.erase("join:" + roomCode.toStdString());
        }
        QMetaObject::invokeMethod(this, [this, roomCode, err]() {
            statusBar()->showMessage(QString("✗ WebRTC Join Error (%1): %2").arg(roomCode).arg(QString::fromStdString(err)), 7000);
        }, Qt::QueuedConnection);
    };

    coordinator->onChannelOpened = [this, coordinator](std::shared_ptr<WebRtcDataChannel> channel) {
        std::string roomCode = coordinator->getRoomCode();
        std::string peerId = "webrtc:" + roomCode;
        {
            std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
            m_globalCoordinators[peerId] = coordinator;
            m_pendingCoordinators.erase("join:" + roomCode);
            m_activeRxCancelTokens[peerId] = std::make_shared<std::atomic<bool>>(false);
        }
        QMetaObject::invokeMethod(this, [this, peerId, roomCode]() {
            if (m_discoveryWidget) {
                m_discoveryWidget->updateGlobalSession(QString::fromStdString(roomCode), true);
            }
            if (m_discoveryPresenter) {
                m_discoveryPresenter->onGlobalPeerConnected(
                    peerId,
                    "WebRTC Peer (" + roomCode + ")",
                    "WebRTC P2P"
                );
            }
            statusBar()->showMessage(QString("✓ WebRTC Global P2P Peer Connected (%1)").arg(QString::fromStdString(roomCode)), 5000);
        }, Qt::QueuedConnection);

        launchWorker([this, coordinator, channel, peerId]() {
            auto rxCancel = [this, peerId]() -> std::shared_ptr<std::atomic<bool>> {
                std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
                auto it = m_activeRxCancelTokens.find(peerId);
                return (it != m_activeRxCancelTokens.end()) ? it->second : nullptr;
            }();

            while (coordinator->getState() == CoordinatorState::Connected && channel && channel->isConnected()) {
                if (rxCancel && rxCancel->load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
                TransferOptions rxOpts;
                rxOpts.autoRenameDuplicates = true;
                rxOpts.closeChannelOnCompletion = false;
                rxOpts.headerTimeoutMs = 0; // Wait indefinitely for incoming file transfer
                std::string rxErr;
                std::string taskId = "rtc-rx-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
                std::string destDir = getDownloadsDirectory();
                bool ok = m_transferEngine->receiveFileOverChannel(
                    taskId,
                    destDir,
                    channel,
                    rxOpts,
                    [this](const TransferProgressUpdate& upd) {
                        QMetaObject::invokeMethod(this, [this, upd]() {
                            if (m_transferPresenter) {
                                m_transferPresenter->updateTaskProgress(upd.taskId, upd.bytesTransferred, upd.speedBytesPerSec, upd.state, upd.message);
                            }
                        }, Qt::QueuedConnection);
                    },
                    rxCancel.get(), // cancelFlag
                    &rxErr,  // outError
                    [this, coordinator](const std::string& tid, const std::string& fn, uint64_t fs, const std::string&) -> bool {
                        QMetaObject::invokeMethod(this, [this, coordinator, tid, fn, fs]() {
                            if (m_transferPresenter) {
                                std::string peerDisplay = "WebRTC Peer (" + coordinator->getRoomCode() + ")";
                                auto task = std::make_shared<P2PTransferTask>(tid, fn, fs, TransferDirection::Download, peerDisplay);
                                task->initialize();
                                task->setState(TransferState::Transferring);
                                m_transferPresenter->addRealTask(task);
                            }
                            statusBar()->showMessage(QString("Incoming WebRTC transfer from %1: %2 (%3 bytes)")
                                .arg(QString::fromStdString(coordinator->getRoomCode()))
                                .arg(QString::fromStdString(fn))
                                .arg(fs));
                        }, Qt::QueuedConnection);
                        return true;
                    },
                    [this, coordinator](const std::string&, const std::string& fileName, bool success, const std::string& savedPath, const std::string& err) {
                        (void)savedPath;
                        recordTransferHistory("rtc-rx-" + fileName, "download", "WebRTC Peer (" + coordinator->getRoomCode() + ")", fileName, 0, success, "WebRtcDataChannel");
                        QMetaObject::invokeMethod(this, [this, fileName, success, err]() {
                            if (success) {
                                statusBar()->showMessage(QString("✓ Received file: %1").arg(QString::fromStdString(fileName)), 5000);
                            } else if (!err.empty()) {
                                statusBar()->showMessage(QString("✗ Receive error: %1").arg(QString::fromStdString(err)), 6000);
                            }
                        }, Qt::QueuedConnection);
                    }
                );
                if (!ok) {
                    if (rxCancel && rxCancel->load()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        continue;
                    }
                    break;
                }
            }
        });
    };

    statusBar()->showMessage(QString("Connecting to WebRTC room %1...").arg(roomCode));
    launchWorker([this, coordinator, signalingWsUrl, roomCode]() {
        bool ok = coordinator->startAsJoiner(signalingWsUrl, roomCode.toStdString());
        if (!ok) {
            QMetaObject::invokeMethod(this, [this, roomCode]() {
                statusBar()->showMessage(QString("✗ Failed to join WebRTC room %1").arg(roomCode), 6000);
            }, Qt::QueuedConnection);
        }
    });
#endif
}

void QtDesktopApp::onJoinRoomWithFileRequested(const QString& roomCode, const QString& filePath) {
#if !defined(LANDROP_HAVE_WEBRTC)
    (void)roomCode;
    (void)filePath;
    QMessageBox::information(this, "Global P2P Not Available",
        "WebRTC support is not compiled into this build. Rebuild with -DLANDROP_ENABLE_WEBRTC=ON.");
#else
    const char* envSignaling = std::getenv("LANDROP_SIGNALING_URL");
    std::string signalingWsUrl = (envSignaling && envSignaling[0] != '\0') ? envSignaling : "wss://landrop-server.duckdns.org/ws/signaling";

    CoordinatorOptions opts;
    opts.signalingHttpUrl = deriveWsHttpUrl(signalingWsUrl);
    opts.autoFetchTurn = true;
    opts.dataChannelLabel = "lan-drop-data";
    if (m_identityClient && !m_identityClient->isGuest()) {
        opts.authToken = m_identityClient->getAuthToken();
        opts.userId = m_identityClient->getCurrentUser().userId;
        opts.deviceId = m_identityClient->getCurrentDevice().deviceId;
    }

    auto coordinator = std::make_shared<WebRtcSessionCoordinator>("gui-joiner-sender", WebRtcConfig{}, opts);
    {
        std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
        m_pendingCoordinators["join-file:" + roomCode.toStdString()] = coordinator;
    }

    coordinator->onError = [this, roomCode](const std::string& err) {
        {
            std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
            m_pendingCoordinators.erase("join-file:" + roomCode.toStdString());
        }
        QMetaObject::invokeMethod(this, [this, roomCode, err]() {
            statusBar()->showMessage(QString("✗ WebRTC Join Error (%1): %2").arg(roomCode).arg(QString::fromStdString(err)), 7000);
        }, Qt::QueuedConnection);
    };

    coordinator->onChannelOpened = [this, coordinator, filePath](std::shared_ptr<WebRtcDataChannel> channel) {
        std::string roomCode = coordinator->getRoomCode();
        std::string peerId = "webrtc:" + roomCode;
        {
            std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
            m_globalCoordinators[peerId] = coordinator;
            m_pendingCoordinators.erase("join-file:" + roomCode);
            m_activeRxCancelTokens[peerId] = std::make_shared<std::atomic<bool>>(false);
        }
        QMetaObject::invokeMethod(this, [this, peerId, roomCode]() {
            if (m_discoveryWidget) {
                m_discoveryWidget->updateGlobalSession(QString::fromStdString(roomCode), true);
            }
            if (m_discoveryPresenter) {
                m_discoveryPresenter->onGlobalPeerConnected(
                    peerId,
                    "WebRTC Peer (" + roomCode + ")",
                    "WebRTC P2P"
                );
            }
            statusBar()->showMessage(QString("✓ WebRTC Global P2P Peer Connected (%1) - Streaming file...").arg(QString::fromStdString(roomCode)), 5000);
        }, Qt::QueuedConnection);

        // First, stream the selected file to the peer
        launchWorker([this, coordinator, channel, peerId, filePath]() {
            TransferOptions txOpts;
            txOpts.closeChannelOnCompletion = false;
            if (m_settingsPresenter) {
                txOpts.speedLimitKbps = m_settingsPresenter->getSpeedLimitKbps();
            }

            std::string taskId = "rtc-tx-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
            QFileInfo fi(filePath);
            uint64_t fileSize = fi.size();
            std::string fileName = fi.fileName().toStdString();

            QMetaObject::invokeMethod(this, [this, taskId, fileName, fileSize, peerId]() {
                if (m_transferPresenter) {
                    auto task = std::make_shared<P2PTransferTask>(taskId, fileName, fileSize, TransferDirection::Upload, peerId);
                    task->initialize();
                    task->setState(TransferState::Transferring);
                    m_transferPresenter->addRealTask(task);
                }
            }, Qt::QueuedConnection);

            std::string err;
            bool ok = m_transferEngine->sendFileOverChannel(
                taskId,
                filePath.toStdString(),
                channel,
                txOpts,
                [this](const TransferProgressUpdate& upd) {
                    QMetaObject::invokeMethod(this, [this, upd]() {
                        if (m_transferPresenter) {
                            m_transferPresenter->updateTaskProgress(upd.taskId, upd.bytesTransferred, upd.speedBytesPerSec, upd.state, upd.message);
                        }
                    }, Qt::QueuedConnection);
                },
                nullptr,
                &err
            );

            recordTransferHistory(taskId, "upload", peerId, fileName, fileSize, ok, "WebRtcDataChannel");

            QMetaObject::invokeMethod(this, [this, ok, err, peerId, fileName]() {
                if (ok) {
                    statusBar()->showMessage(QString("✓ File '%1' successfully sent over WebRTC to %2").arg(QString::fromStdString(fileName)).arg(QString::fromStdString(peerId)), 5000);
                } else {
                    statusBar()->showMessage(QString("✗ WebRTC send error: %1").arg(QString::fromStdString(err)), 6000);
                }
            }, Qt::QueuedConnection);

            auto rxCancel = [this, peerId]() -> std::shared_ptr<std::atomic<bool>> {
                std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
                auto it = m_activeRxCancelTokens.find(peerId);
                return (it != m_activeRxCancelTokens.end()) ? it->second : nullptr;
            }();

            // Keep channel open in receive standby loop
            while (coordinator->getState() == CoordinatorState::Connected && channel && channel->isConnected()) {
                if (rxCancel && rxCancel->load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
                TransferOptions rxOpts;
                rxOpts.autoRenameDuplicates = true;
                rxOpts.closeChannelOnCompletion = false;
                rxOpts.headerTimeoutMs = 0;
                std::string rxErr;
                std::string rxTaskId = "rtc-rx-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
                std::string destDir = getDownloadsDirectory();
                bool rxOk = m_transferEngine->receiveFileOverChannel(
                    rxTaskId,
                    destDir,
                    channel,
                    rxOpts,
                    [this](const TransferProgressUpdate& upd) {
                        QMetaObject::invokeMethod(this, [this, upd]() {
                            if (m_transferPresenter) {
                                m_transferPresenter->updateTaskProgress(upd.taskId, upd.bytesTransferred, upd.speedBytesPerSec, upd.state, upd.message);
                            }
                        }, Qt::QueuedConnection);
                    },
                    rxCancel.get(),
                    &rxErr,
                    [this, coordinator](const std::string& tid, const std::string& fn, uint64_t fs, const std::string&) -> bool {
                        QMetaObject::invokeMethod(this, [this, coordinator, tid, fn, fs]() {
                            if (m_transferPresenter) {
                                std::string peerDisplay = "WebRTC Peer (" + coordinator->getRoomCode() + ")";
                                auto task = std::make_shared<P2PTransferTask>(tid, fn, fs, TransferDirection::Download, peerDisplay);
                                task->initialize();
                                task->setState(TransferState::Transferring);
                                m_transferPresenter->addRealTask(task);
                            }
                            statusBar()->showMessage(QString("Incoming WebRTC transfer from %1: %2 (%3 bytes)")
                                .arg(QString::fromStdString(coordinator->getRoomCode()))
                                .arg(QString::fromStdString(fn))
                                .arg(fs));
                        }, Qt::QueuedConnection);
                        return true;
                    },
                    [this, coordinator](const std::string&, const std::string& fn, bool success, const std::string& savedPath, const std::string& rxError) {
                        (void)savedPath;
                        recordTransferHistory("rtc-rx-" + fn, "download", "WebRTC Peer (" + coordinator->getRoomCode() + ")", fn, 0, success, "WebRtcDataChannel");
                        QMetaObject::invokeMethod(this, [this, fn, success, rxError]() {
                            if (success) {
                                statusBar()->showMessage(QString("✓ Received file: %1").arg(QString::fromStdString(fn)), 5000);
                            } else if (!rxError.empty()) {
                                statusBar()->showMessage(QString("✗ Receive error: %1").arg(QString::fromStdString(rxError)), 6000);
                            }
                        }, Qt::QueuedConnection);
                    }
                );
                if (!rxOk) {
                    if (rxCancel && rxCancel->load()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        continue;
                    }
                    break;
                }
            }
        });
    };

    statusBar()->showMessage(QString("Connecting to WebRTC room %1 to send file...").arg(roomCode));
    launchWorker([this, coordinator, signalingWsUrl, roomCode]() {
        bool ok = coordinator->startAsJoiner(signalingWsUrl, roomCode.toStdString());
        if (!ok) {
            QMetaObject::invokeMethod(this, [this, roomCode]() {
                statusBar()->showMessage(QString("✗ Failed to join WebRTC room %1").arg(roomCode), 6000);
            }, Qt::QueuedConnection);
        }
    });
#endif
}

void QtDesktopApp::onCloseGlobalSessionRequested(const QString& roomCode) {
    std::string codeStr = roomCode.toStdString();
    std::string peerId = "webrtc:" + codeStr;
    std::shared_ptr<WebRtcSessionCoordinator> coordinator;
    std::shared_ptr<std::atomic<bool>> rxCancel;
    {
        std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
        auto it = m_globalCoordinators.find(peerId);
        if (it != m_globalCoordinators.end()) {
            coordinator = it->second;
            m_globalCoordinators.erase(it);
        }
        m_pendingCoordinators.erase("host:" + codeStr);
        m_pendingCoordinators.erase("join:" + codeStr);
        m_pendingCoordinators.erase("join-file:" + codeStr);
        auto itRx = m_activeRxCancelTokens.find(peerId);
        if (itRx != m_activeRxCancelTokens.end()) {
            rxCancel = itRx->second;
            m_activeRxCancelTokens.erase(itRx);
        }
    }
    if (rxCancel) {
        rxCancel->store(true);
    }
    if (coordinator) {
        coordinator->close();
    }
    if (m_discoveryWidget) {
        m_discoveryWidget->updateGlobalSession(QString(), false);
    }
    statusBar()->showMessage(QString("Closed WebRTC session %1").arg(roomCode), 3000);
}

}
