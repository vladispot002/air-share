#include "landrop/ui/qt/QtTransferWidget.hpp"
#include "landrop/ui/TransferPresenter.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QMetaObject>
#include <QDir>
#include <QStandardPaths>
#include <QFrame>
#include <algorithm>
#include "landrop/ui/qt/QtSettingsWidget.hpp"

namespace landrop::ui {

static QString formatBytes(uint64_t bytes) {
    if (bytes < 1024) return QString::number(bytes) + " B";
    double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024.0) return QString("%1 KB").arg(kb, 0, 'f', 1);
    double mb = kb / 1024.0;
    if (mb < 1024.0) return QString("%1 MB").arg(mb, 0, 'f', 2);
    double gb = mb / 1024.0;
    return QString("%1 GB").arg(gb, 0, 'f', 2);
}

QtTransferWidget::QtTransferWidget(QWidget* parent)
    : QWidget(parent) {
    setupUi();
}

void QtTransferWidget::setPresenter(TransferPresenter* presenter) {
    m_presenter = presenter;
}

void QtTransferWidget::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(16);

    // Header
    auto* headerLayout = new QHBoxLayout();
    auto* titleLabel = new QLabel("⇄ Transfer Manager", this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);

    m_speedSummaryLabel = new QLabel("Active: 0 | Total: 0.0 MB/s", this);
    m_speedSummaryLabel->setObjectName("secondaryLabel");

    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_speedSummaryLabel);
    mainLayout->addLayout(headerLayout);

    // Location panel: displays and manages save directory
    auto* locationFrame = new QFrame(this);
    locationFrame->setObjectName("card");
    auto* locationLayout = new QHBoxLayout(locationFrame);
    locationLayout->setContentsMargins(14, 8, 14, 8);
    locationLayout->setSpacing(10);

    auto* locTitle = new QLabel("📥 Папка для отриманих файлів:", locationFrame);
    locTitle->setStyleSheet("font-weight: 600;");

    m_downloadsPathDisplay = new QLabel(locationFrame);
    m_downloadsPathDisplay->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_downloadsPathDisplay->setStyleSheet("font-family: monospace; color: #007aff;");

    m_selectFolderBtn = new QPushButton("📁 Обрати папку...", locationFrame);
    m_selectFolderBtn->setToolTip("Змінити або створити папку для збереження отриманих файлів");
    connect(m_selectFolderBtn, &QPushButton::clicked, this, &QtTransferWidget::onSelectFolderClicked);

    m_openFolderBtn = new QPushButton("📂 Відкрити папку", locationFrame);
    m_openFolderBtn->setToolTip("Відкрити папку у системному менеджері файлів");
    connect(m_openFolderBtn, &QPushButton::clicked, this, &QtTransferWidget::onOpenFolderClicked);

    locationLayout->addWidget(locTitle);
    locationLayout->addWidget(m_downloadsPathDisplay, 1);
    locationLayout->addWidget(m_selectFolderBtn);
    locationLayout->addWidget(m_openFolderBtn);
    mainLayout->addWidget(locationFrame);

    updateLocationBar();

    // Actions bar
    auto* actionsLayout = new QHBoxLayout();
    actionsLayout->setSpacing(10);
    m_sendFileBtn = new QPushButton("⬆ Send File...", this);
    m_sendFileBtn->setObjectName("primaryBtn");
    connect(m_sendFileBtn, &QPushButton::clicked, this, &QtTransferWidget::onSendFileClicked);

    m_pauseBtn = new QPushButton("⏸ Pause", this);
    m_pauseBtn->setEnabled(false);
    m_pauseBtn->setToolTip("Direct TCP streaming does not support pause. Use Cancel instead.");
    connect(m_pauseBtn, &QPushButton::clicked, this, &QtTransferWidget::onPauseClicked);

    m_resumeBtn = new QPushButton("▶ Resume", this);
    m_resumeBtn->setEnabled(false);
    m_resumeBtn->setToolTip("Direct TCP streaming does not support resume. Restart the transfer instead.");
    connect(m_resumeBtn, &QPushButton::clicked, this, &QtTransferWidget::onResumeClicked);

    m_cancelBtn = new QPushButton("⏹ Cancel", this);
    m_cancelBtn->setObjectName("dangerBtn");
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QtTransferWidget::onCancelClicked);

    m_clearBtn = new QPushButton("Clear Finished", this);
    connect(m_clearBtn, &QPushButton::clicked, this, &QtTransferWidget::onClearClicked);

    actionsLayout->addWidget(m_sendFileBtn);
    actionsLayout->addWidget(m_pauseBtn);
    actionsLayout->addWidget(m_resumeBtn);
    actionsLayout->addWidget(m_cancelBtn);
    actionsLayout->addWidget(m_clearBtn);
    actionsLayout->addStretch();
    mainLayout->addLayout(actionsLayout);

    // Table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(7);
    QStringList headers;
    headers << "ID" << "File Name" << "Direction" << "Size" << "Progress" << "Speed" << "Status";
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(false);
    connect(m_table, &QTableWidget::cellClicked, this, &QtTransferWidget::onTableRowSelected);

    mainLayout->addWidget(m_table, 1);

    // Details label
    m_detailsLabel = new QLabel("Select a transfer to view details and chunk verification metrics.", this);
    m_detailsLabel->setObjectName("card");
    mainLayout->addWidget(m_detailsLabel);
}

void QtTransferWidget::updateActionButtons() {
    if (m_selectedTaskId.empty()) {
        m_pauseBtn->setEnabled(false);
        m_resumeBtn->setEnabled(false);
        m_cancelBtn->setEnabled(false);
        return;
    }

    auto it = std::find_if(m_currentTransfers.begin(), m_currentTransfers.end(), [this](const TransferViewModel& item) {
        return item.taskId == m_selectedTaskId;
    });

    if (it == m_currentTransfers.end()) {
        m_pauseBtn->setEnabled(false);
        m_resumeBtn->setEnabled(false);
        m_cancelBtn->setEnabled(false);
        return;
    }

    const std::string& st = it->stateStr;
    bool isActive = (st == "Transferring" || st == "Connecting" || st == "Running" || st == "Active");

    // Direct TCP streaming lacks byte-offset resume support; keep pause/resume disabled
    m_pauseBtn->setEnabled(false);
    m_pauseBtn->setToolTip("Direct TCP streaming does not support pause. Use Cancel instead.");
    m_resumeBtn->setEnabled(false);
    m_resumeBtn->setToolTip("Direct TCP streaming does not support resume. Restart the transfer instead.");

    m_cancelBtn->setEnabled(isActive);
}

void QtTransferWidget::updateTransferTable(const std::vector<TransferViewModel>& transfers) {
    QMetaObject::invokeMethod(this, [this, transfers]() {
        m_currentTransfers = transfers;
        m_table->setRowCount(static_cast<int>(transfers.size()));

        double totalSpeed = 0.0;
        int activeCount = 0;

        for (size_t i = 0; i < transfers.size(); ++i) {
            const auto& t = transfers[i];
            int row = static_cast<int>(i);

            m_table->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(t.taskId)));
            m_table->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(t.fileName)));

            QString dirText = (t.direction == "Upload") ? "⬆ Upload" : "⬇ Download";
            m_table->setItem(row, 2, new QTableWidgetItem(dirText));
            m_table->setItem(row, 3, new QTableWidgetItem(formatBytes(t.fileSize)));

            // Progress bar
            auto* pBar = new QProgressBar();
            pBar->setRange(0, 100);
            pBar->setValue(static_cast<int>(t.progress));
            pBar->setFormat(QString("%1%").arg(t.progress, 0, 'f', 1));
            pBar->setFixedHeight(18);
            m_table->setCellWidget(row, 4, pBar);

            double speedMb = t.speedBytesPerSec / (1024.0 * 1024.0);
            m_table->setItem(row, 5, new QTableWidgetItem(QString("%1 MB/s").arg(speedMb, 0, 'f', 2)));
            m_table->setItem(row, 6, new QTableWidgetItem(QString::fromStdString(t.stateStr)));

            totalSpeed += speedMb;
            if (t.stateStr == "Transferring" || t.stateStr == "Connecting" || t.stateStr == "Running" || t.stateStr == "Active") {
                activeCount++;
            }
        }

        m_speedSummaryLabel->setText(QString("Active: %1 | Total Speed: %2 MB/s").arg(activeCount).arg(totalSpeed, 0, 'f', 2));

        if (transfers.empty()) {
            m_detailsLabel->setText("No active or completed transfers. Select a device on the Radar tab to send files.");
            m_clearBtn->setEnabled(false);
        } else {
            m_clearBtn->setEnabled(true);
        }

        updateActionButtons();
    }, Qt::QueuedConnection);
}

void QtTransferWidget::updateSelectedTransferDetails(const TransferViewModel& item) {
    QMetaObject::invokeMethod(this, [this, item]() {
        double speedMb = item.speedBytesPerSec / (1024.0 * 1024.0);

        QString detail = QString("<b>Task:</b> %1 &nbsp;•&nbsp; <b>File:</b> %2 &nbsp;•&nbsp; <b>Transferred:</b> %3 / %4 (%5%) &nbsp;•&nbsp; <b>Speed:</b> %6 MB/s &nbsp;•&nbsp; <b>State:</b> %7")
            .arg(QString::fromStdString(item.taskId))
            .arg(QString::fromStdString(item.fileName))
            .arg(formatBytes(item.bytesTransferred))
            .arg(formatBytes(item.fileSize))
            .arg(item.progress, 0, 'f', 1)
            .arg(speedMb, 0, 'f', 2)
            .arg(QString::fromStdString(item.stateStr));
        m_detailsLabel->setText(detail);
        updateActionButtons();
    }, Qt::QueuedConnection);
}

void QtTransferWidget::showTransferError(const std::string& taskId, const std::string& error) {
    QMetaObject::invokeMethod(this, [this, taskId, error]() {
        QMessageBox::critical(this, "Transfer Error", QString("Task %1 encountered an error:\n%2").arg(QString::fromStdString(taskId)).arg(QString::fromStdString(error)));
    }, Qt::QueuedConnection);
}

void QtTransferWidget::initiateSendFile(const std::string& targetPeerId, const std::string& filePath) {
    if (m_presenter) {
        m_presenter->onSendFileClicked(targetPeerId, filePath);
    }
}

void QtTransferWidget::onPauseClicked() {
    if (m_selectedTaskId.empty()) return;
    if (m_presenter) {
        m_presenter->onPauseTransferClicked(m_selectedTaskId);
    }
}

void QtTransferWidget::onResumeClicked() {
    if (m_selectedTaskId.empty()) return;
    if (m_presenter) {
        m_presenter->onResumeTransferClicked(m_selectedTaskId);
    }
}

void QtTransferWidget::onCancelClicked() {
    if (m_selectedTaskId.empty()) return;
    if (m_presenter) {
        m_presenter->onCancelTransferClicked(m_selectedTaskId);
    }
}

void QtTransferWidget::onClearClicked() {
    if (m_presenter) {
        m_presenter->onClearHistoryClicked();
        m_selectedTaskId.clear();
        updateActionButtons();
    }
}

void QtTransferWidget::onSendFileClicked() {
    QString filePath = QFileDialog::getOpenFileName(this, "Select File to Send via LAN-Drop");
    if (filePath.isEmpty()) return;

    std::string targetPeer = "local-loopback";
    std::vector<std::pair<std::string, std::string>> peers;
    if (m_peerProvider) {
        peers = m_peerProvider();
    }

    QStringList items;
    for (const auto& [id, name] : peers) {
        items << QString::fromStdString(name);
    }
    items << "💻 Local Loopback (127.0.0.1:8848)";
    items << "🌐 Enter Custom IP / Port...";

    bool ok = false;
    QString chosen = QInputDialog::getItem(this, "Select Target Peer", "Choose device to send file to:", items, 0, false, &ok);
    if (!ok) return;

    if (chosen == "🌐 Enter Custom IP / Port...") {
        QString ip = QInputDialog::getText(this, "Target Device IP", "Enter destination IP address:", QLineEdit::Normal, "192.168.1.100", &ok);
        if (!ok || ip.trimmed().isEmpty()) return;
        int port = QInputDialog::getInt(this, "Target Device Port", "Enter destination port:", 8848, 1024, 65535, 1, &ok);
        if (!ok) return;
        targetPeer = "direct:" + ip.trimmed().toStdString() + ":" + std::to_string(port);
    } else if (chosen == "💻 Local Loopback (127.0.0.1:8848)") {
        targetPeer = "local-loopback";
    } else {
        for (size_t i = 0; i < peers.size(); ++i) {
            if (chosen == QString::fromStdString(peers[i].second)) {
                targetPeer = peers[i].first;
                break;
            }
        }
    }

    if (m_presenter) {
        m_presenter->onSendFileClicked(targetPeer, filePath.toStdString());
    }
}

void QtTransferWidget::setDownloadsDirectory(std::string path) {
    m_downloadsDirectory = std::move(path);
    updateLocationBar();
}

void QtTransferWidget::updateLocationBar() {
    if (!m_downloadsPathDisplay) return;
    QString display = QString::fromStdString(m_downloadsDirectory);
    if (display.isEmpty()) {
        display = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (display.isEmpty()) {
            display = QDir::currentPath() + "/downloads";
        }
    }
    m_downloadsPathDisplay->setText(display);
    m_downloadsPathDisplay->setToolTip(QString("Повний шлях до папки завантажень:\n%1").arg(display));
}

void QtTransferWidget::onSelectFolderClicked() {
    QString current = QString::fromStdString(m_downloadsDirectory);
    if (current.isEmpty()) {
        current = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }
    QString dir = QFileDialog::getExistingDirectory(
        this,
        "Оберіть або створіть папку для отриманих файлів",
        current,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    if (dir.isEmpty()) return;

    auto val = QtSettingsWidget::validateWritableDirectory(dir, true);
    if (!val.ok) {
        QMessageBox::warning(this, "Помилка каталогу збереження", val.errorMessage);
        return;
    }

    setDownloadsDirectory(val.resolvedPath.toStdString());
    emit downloadsDirectoryChanged(val.resolvedPath);
}

void QtTransferWidget::onOpenFolderClicked() {
    QString path = QString::fromStdString(m_downloadsDirectory);
    if (path.isEmpty()) {
        path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (path.isEmpty()) {
            path = QDir::currentPath() + "/downloads";
        }
    }
    QDir().mkpath(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void QtTransferWidget::onTableRowSelected(int row, int /*column*/) {
    if (row < 0 || row >= static_cast<int>(m_currentTransfers.size())) return;
    m_selectedTaskId = m_currentTransfers[static_cast<size_t>(row)].taskId;
    updateActionButtons();
    if (m_presenter) {
        m_presenter->onSelectTransferRow(static_cast<size_t>(row));
    }
}

}
