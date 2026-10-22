#include "landrop/ui/qt/QtStorageWidget.hpp"
#include "landrop/ui/StoragePresenter.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QMetaObject>

namespace landrop::ui {

QtStorageWidget::QtStorageWidget(QWidget* parent)
    : QWidget(parent) {
    setupUi();
}

void QtStorageWidget::setPresenter(StoragePresenter* presenter) {
    m_presenter = presenter;
}

void QtStorageWidget::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(16);

    // Header
    auto* titleLabel = new QLabel("📁 Shared Storage & Remote Access", this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    mainLayout->addWidget(titleLabel);

    // Path & PIN status bar
    auto* pathFrame = new QFrame(this);
    pathFrame->setObjectName("card");
    auto* pathLayout = new QVBoxLayout(pathFrame);

    auto* topPathRow = new QHBoxLayout();
    m_pathLabel = new QLabel("Active Shared Folder: <b>./shared</b>", pathFrame);
    m_pinStatusLabel = new QLabel("Security: No PIN", pathFrame);
    m_pinStatusLabel->setStyleSheet("color: #ff9f0a; font-weight: bold; background: rgba(255,159,10,0.15); padding: 3px 8px; border-radius: 4px;");

    topPathRow->addWidget(m_pathLabel);
    topPathRow->addStretch();
    topPathRow->addWidget(m_pinStatusLabel);
    pathLayout->addLayout(topPathRow);

    auto* actionsRow = new QHBoxLayout();
    m_addFolderBtn = new QPushButton("📂 Change Folder...", pathFrame);
    m_addFolderBtn->setObjectName("primaryBtn");
    connect(m_addFolderBtn, &QPushButton::clicked, this, &QtStorageWidget::onAddFolderClicked);

    m_setPinBtn = new QPushButton("🔒 Set Access PIN", pathFrame);
    connect(m_setPinBtn, &QPushButton::clicked, this, &QtStorageWidget::onSetPinClicked);

    m_clearPinBtn = new QPushButton("Clear PIN", pathFrame);
    connect(m_clearPinBtn, &QPushButton::clicked, this, &QtStorageWidget::onClearPinClicked);

    m_rescanBtn = new QPushButton("↺ Rescan", pathFrame);
    connect(m_rescanBtn, &QPushButton::clicked, this, &QtStorageWidget::onRescanClicked);

    actionsRow->addWidget(m_addFolderBtn);
    actionsRow->addWidget(m_setPinBtn);
    actionsRow->addWidget(m_clearPinBtn);
    actionsRow->addWidget(m_rescanBtn);
    actionsRow->addStretch();
    pathLayout->addLayout(actionsRow);

    mainLayout->addWidget(pathFrame);

    // File Table
    m_fileTable = new QTableWidget(this);
    m_fileTable->setColumnCount(3);
    QStringList headers;
    headers << "File Name" << "Size" << "SHA-256 Checksum";
    m_fileTable->setHorizontalHeaderLabels(headers);
    m_fileTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_fileTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_fileTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mainLayout->addWidget(m_fileTable, 1);

    // Stats bar
    m_statsLabel = new QLabel("Total Storage: 0.0 MB | Shared Files: 0", this);
    m_statsLabel->setStyleSheet("color: #8e8e93; font-size: 12px;");
    mainLayout->addWidget(m_statsLabel);
}

void QtStorageWidget::setSharedPath(const std::string& path) {
    QMetaObject::invokeMethod(this, [this, path]() {
        m_pathLabel->setText(QString("Active Shared Folder: <b>%1</b>").arg(QString::fromStdString(path)));
    }, Qt::QueuedConnection);
}

void QtStorageWidget::updateFileList(const std::vector<FileMetadata>& files) {
    QMetaObject::invokeMethod(this, [this, files]() {
        m_fileTable->setRowCount(static_cast<int>(files.size()));
        for (size_t i = 0; i < files.size(); ++i) {
            const auto& f = files[i];
            int row = static_cast<int>(i);

            m_fileTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(f.fileName)));

            double sizeMb = static_cast<double>(f.fileSize) / (1024.0 * 1024.0);
            m_fileTable->setItem(row, 1, new QTableWidgetItem(QString("%1 MB").arg(sizeMb, 0, 'f', 2)));

            m_fileTable->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(f.fileSha256)));
        }
    }, Qt::QueuedConnection);
}

void QtStorageWidget::setStorageStats(uint64_t totalBytes, size_t fileCount) {
    QMetaObject::invokeMethod(this, [this, totalBytes, fileCount]() {
        double mb = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
        m_statsLabel->setText(QString("Total Storage: %1 MB | Shared Files: %2").arg(mb, 0, 'f', 2).arg(fileCount));
    }, Qt::QueuedConnection);
}

void QtStorageWidget::setPasswordProtectionStatus(bool isProtected) {
    QMetaObject::invokeMethod(this, [this, isProtected]() {
        if (isProtected) {
            m_pinStatusLabel->setText("Security: 🔒 PIN Protected");
            m_pinStatusLabel->setStyleSheet("color: #30d158; font-weight: bold; background: rgba(48,209,88,0.15); padding: 3px 8px; border-radius: 4px;");
        } else {
            m_pinStatusLabel->setText("Security: No PIN");
            m_pinStatusLabel->setStyleSheet("color: #ff9f0a; font-weight: bold; background: rgba(255,159,10,0.15); padding: 3px 8px; border-radius: 4px;");
        }
    }, Qt::QueuedConnection);
}

void QtStorageWidget::onAddFolderClicked() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Directory to Share via LAN-Drop");
    if (dir.isEmpty()) return;

    if (m_presenter) {
        m_presenter->onSharedPathChanged(dir.toStdString());
    }
}

void QtStorageWidget::onRemoveFolderClicked() {
    if (m_presenter) {
        m_presenter->onSharedPathChanged("./shared");
    }
}

void QtStorageWidget::onSetPinClicked() {
    bool ok = false;
    QString pin = QInputDialog::getText(this, "Set Access PIN", "Enter PIN (4-16 characters):", QLineEdit::Password, "", &ok);
    if (!ok || pin.isEmpty()) return;

    if (m_presenter) {
        m_presenter->onSetPasswordClicked(pin.toStdString());
    }
}

void QtStorageWidget::onClearPinClicked() {
    if (m_presenter) {
        m_presenter->onClearPasswordClicked();
    }
}

void QtStorageWidget::onRescanClicked() {
    if (m_presenter) {
        m_presenter->onRescanClicked();
    }
}

}
