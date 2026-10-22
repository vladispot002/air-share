#include "landrop/ui/qt/QtTransferWidget.hpp"
#include "landrop/ui/TransferPresenter.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QMetaObject>

namespace landrop::ui {

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
    m_speedSummaryLabel->setStyleSheet("color: #8e8e93; font-size: 12px; font-weight: 500;");

    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_speedSummaryLabel);
    mainLayout->addLayout(headerLayout);

    // Actions bar
    auto* actionsLayout = new QHBoxLayout();
    m_sendFileBtn = new QPushButton("⬆ Send File...", this);
    m_sendFileBtn->setObjectName("primaryBtn");
    connect(m_sendFileBtn, &QPushButton::clicked, this, &QtTransferWidget::onSendFileClicked);

    m_pauseBtn = new QPushButton("⏸ Pause", this);
    connect(m_pauseBtn, &QPushButton::clicked, this, &QtTransferWidget::onPauseClicked);

    m_resumeBtn = new QPushButton("▶ Resume", this);
    connect(m_resumeBtn, &QPushButton::clicked, this, &QtTransferWidget::onResumeClicked);

    m_cancelBtn = new QPushButton("⏹ Cancel", this);
    m_cancelBtn->setObjectName("dangerBtn");
    connect(m_cancelBtn, &QPushButton::clicked, this, &QtTransferWidget::onCancelClicked);

    m_clearBtn = new QPushButton("Clear Finished", this);
    connect(m_clearBtn, &QPushButton::clicked, this, &QtTransferWidget::onClearClicked);

    m_openFolderBtn = new QPushButton("📂 Open Downloads", this);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &QtTransferWidget::onOpenFolderClicked);

    actionsLayout->addWidget(m_sendFileBtn);
    actionsLayout->addWidget(m_pauseBtn);
    actionsLayout->addWidget(m_resumeBtn);
    actionsLayout->addWidget(m_cancelBtn);
    actionsLayout->addWidget(m_clearBtn);
    actionsLayout->addStretch();
    actionsLayout->addWidget(m_openFolderBtn);
    mainLayout->addLayout(actionsLayout);

    // Table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(7);
    QStringList headers;
    headers << "ID" << "File Name" << "Direction" << "Size" << "Progress" << "Speed" << "Status";
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(false);
    connect(m_table, &QTableWidget::cellClicked, this, &QtTransferWidget::onTableRowSelected);

    mainLayout->addWidget(m_table, 1);

    // Details label
    m_detailsLabel = new QLabel("Select a transfer to view details and chunk verification metrics.", this);
    m_detailsLabel->setStyleSheet("color: #8e8e93; font-size: 12px; padding: 6px; background: rgba(128,128,128,0.1); border-radius: 6px;");
    mainLayout->addWidget(m_detailsLabel);
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
            m_table->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(t.direction)));

            double sizeMb = static_cast<double>(t.fileSize) / (1024.0 * 1024.0);
            m_table->setItem(row, 3, new QTableWidgetItem(QString("%1 MB").arg(sizeMb, 0, 'f', 2)));

            // Progress bar
            auto* pBar = new QProgressBar();
            pBar->setRange(0, 100);
            pBar->setValue(static_cast<int>(t.progress));
            pBar->setFormat(QString("%1%").arg(t.progress, 0, 'f', 1));
            pBar->setFixedHeight(20);
            m_table->setCellWidget(row, 4, pBar);

            double speedMb = t.speedBytesPerSec / (1024.0 * 1024.0);
            m_table->setItem(row, 5, new QTableWidgetItem(QString("%1 MB/s").arg(speedMb, 0, 'f', 2)));
            m_table->setItem(row, 6, new QTableWidgetItem(QString::fromStdString(t.stateStr)));

            totalSpeed += speedMb;
            if (t.stateStr == "Running" || t.stateStr == "Active") {
                activeCount++;
            }
        }

        m_speedSummaryLabel->setText(QString("Active: %1 | Total Speed: %2 MB/s").arg(activeCount).arg(totalSpeed, 0, 'f', 2));
    }, Qt::QueuedConnection);
}

void QtTransferWidget::updateSelectedTransferDetails(const TransferViewModel& item) {
    QMetaObject::invokeMethod(this, [this, item]() {
        double transferredMb = static_cast<double>(item.bytesTransferred) / (1024.0 * 1024.0);
        double totalMb = static_cast<double>(item.fileSize) / (1024.0 * 1024.0);
        double speedMb = item.speedBytesPerSec / (1024.0 * 1024.0);

        QString detail = QString("<b>Task:</b> %1 &nbsp;|&nbsp; <b>File:</b> %2 &nbsp;|&nbsp; <b>Transferred:</b> %3 / %4 MB (%5%) &nbsp;|&nbsp; <b>Speed:</b> %6 MB/s &nbsp;|&nbsp; <b>State:</b> %7")
            .arg(QString::fromStdString(item.taskId))
            .arg(QString::fromStdString(item.fileName))
            .arg(transferredMb, 0, 'f', 2)
            .arg(totalMb, 0, 'f', 2)
            .arg(item.progress, 0, 'f', 1)
            .arg(speedMb, 0, 'f', 2)
            .arg(QString::fromStdString(item.stateStr));
        m_detailsLabel->setText(detail);
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
    }
}

void QtTransferWidget::onSendFileClicked() {
    QString filePath = QFileDialog::getOpenFileName(this, "Select File to Send");
    if (filePath.isEmpty()) return;

    if (m_presenter) {
        m_presenter->onSendFileClicked("local-loopback", filePath.toStdString());
    }
}

void QtTransferWidget::onOpenFolderClicked() {
    QString path = QDir::currentPath() + "/downloads";
    QDir().mkpath(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void QtTransferWidget::onTableRowSelected(int row, int /*column*/) {
    if (row < 0 || row >= static_cast<int>(m_currentTransfers.size())) return;
    m_selectedTaskId = m_currentTransfers[static_cast<size_t>(row)].taskId;
    if (m_presenter) {
        m_presenter->onSelectTransferRow(static_cast<size_t>(row));
    }
}

}
