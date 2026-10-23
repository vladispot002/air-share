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
#include <QClipboard>
#include <QApplication>
#include <QToolTip>
#include <QStyle>

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
    m_pinStatusLabel->setObjectName("badgeWarning");

    topPathRow->addWidget(m_pathLabel);
    topPathRow->addStretch();
    topPathRow->addWidget(m_pinStatusLabel);
    pathLayout->addLayout(topPathRow);

    auto* actionsRow = new QHBoxLayout();
    actionsRow->setSpacing(10);
    m_addFolderBtn = new QPushButton("📂 Change Folder...", pathFrame);
    m_addFolderBtn->setObjectName("primaryBtn");
    connect(m_addFolderBtn, &QPushButton::clicked, this, &QtStorageWidget::onAddFolderClicked);

    m_setPinBtn = new QPushButton("🔒 Set Access PIN", pathFrame);
    connect(m_setPinBtn, &QPushButton::clicked, this, &QtStorageWidget::onSetPinClicked);

    m_clearPinBtn = new QPushButton("Clear PIN", pathFrame);
    m_clearPinBtn->setEnabled(false);
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
    m_fileTable->verticalHeader()->setVisible(false);
    m_fileTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileTable->setAlternatingRowColors(false);

    connect(m_fileTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int /*col*/) {
        if (row >= 0 && m_fileTable->item(row, 2)) {
            QString hash = m_fileTable->item(row, 2)->text();
            QApplication::clipboard()->setText(hash);
            QToolTip::showText(QCursor::pos(), "Checksum copied to clipboard!", this, QRect(), 2000);
        }
    });

    mainLayout->addWidget(m_fileTable, 1);

    // Stats bar
    m_statsLabel = new QLabel("Total Storage: 0 B | Shared Files: 0", this);
    m_statsLabel->setObjectName("secondaryLabel");
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
            m_fileTable->setItem(row, 1, new QTableWidgetItem(formatBytes(f.fileSize)));
            m_fileTable->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(f.fileSha256)));
        }
    }, Qt::QueuedConnection);
}

void QtStorageWidget::setStorageStats(uint64_t totalBytes, size_t fileCount) {
    QMetaObject::invokeMethod(this, [this, totalBytes, fileCount]() {
        m_statsLabel->setText(QString("Total Storage: %1 | Shared Files: %2").arg(formatBytes(totalBytes)).arg(fileCount));
    }, Qt::QueuedConnection);
}

void QtStorageWidget::setPasswordProtectionStatus(bool isProtected) {
    QMetaObject::invokeMethod(this, [this, isProtected]() {
        if (isProtected) {
            m_pinStatusLabel->setText("Security: 🔒 PIN Protected");
            m_pinStatusLabel->setObjectName("badgeSuccess");
            m_clearPinBtn->setEnabled(true);
        } else {
            m_pinStatusLabel->setText("Security: No PIN");
            m_pinStatusLabel->setObjectName("badgeWarning");
            m_clearPinBtn->setEnabled(false);
        }
        m_pinStatusLabel->style()->unpolish(m_pinStatusLabel);
        m_pinStatusLabel->style()->polish(m_pinStatusLabel);
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
    QString pin = QInputDialog::getText(this, "Set Access PIN", "Enter PIN (4-32 characters):", QLineEdit::Password, "", &ok);
    if (!ok || pin.isEmpty()) return;

    if (pin.length() < 4 || pin.length() > 32) {
        QMessageBox::warning(this, "Invalid PIN", "Access PIN must be between 4 and 32 characters.");
        return;
    }

    if (m_presenter) {
        m_presenter->onSetPasswordClicked(pin.toStdString());
    }
}

void QtStorageWidget::onClearPinClicked() {
    if (!m_presenter) return;

    bool ok = false;
    QString pin = QInputDialog::getText(this, "Clear Access PIN", "Enter current PIN to confirm removal:", QLineEdit::Password, "", &ok);
    if (!ok || pin.isEmpty()) return;

    if (!m_presenter->getManager().verifyAccessPassword(pin.toStdString())) {
        QMessageBox::critical(this, "Access Denied", "Incorrect PIN. Cannot clear protection.");
        return;
    }

    m_presenter->onClearPasswordClicked();
}

void QtStorageWidget::onRescanClicked() {
    if (m_presenter) {
        m_presenter->onRescanClicked();
    }
}

}
