#include "landrop/ui/qt/QtSettingsWidget.hpp"
#include "landrop/ui/SettingsPresenter.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QMetaObject>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QInputDialog>

namespace landrop::ui {

DirectoryValidation QtSettingsWidget::validateWritableDirectory(const QString& rawPath, bool createIfMissing) {
    QString trimmed = rawPath.trimmed();
    if (trimmed.isEmpty()) {
        return {false, "", "Шлях до папки не може бути порожнім."};
    }

    QFileInfo rawFi(trimmed);
    if (rawFi.exists() && !rawFi.isDir()) {
        return {false, trimmed, QString("Вказаний шлях не є каталогом:\n%1").arg(trimmed)};
    }

    QDir dir(trimmed);
    if (!dir.exists()) {
        if (createIfMissing) {
            if (!dir.mkpath(".")) {
                return {false, trimmed, QString("Не вдалося створити директорію:\n%1\nПеревірте права доступу до батьківського каталогу.").arg(trimmed)};
            }
        } else {
            return {false, trimmed, QString("Директорія не існує:\n%1").arg(trimmed)};
        }
    }

    QString canonical = dir.canonicalPath().isEmpty() ? dir.absolutePath() : dir.canonicalPath();
    QFileInfo fi(canonical);
    if (!fi.isDir()) {
        return {false, canonical, QString("Вказаний шлях не є каталогом:\n%1").arg(canonical)};
    }

    if (!fi.isReadable()) {
        return {false, canonical, QString("Каталог недоступний для читання:\n%1").arg(canonical)};
    }

    if (!fi.isWritable()) {
        return {false, canonical, QString("Відсутні права на запис у каталог:\n%1").arg(canonical)};
    }

    // Canary write probe test
    QString canaryPath = dir.filePath(QString(".landrop_probe_%1.tmp").arg(QCoreApplication::applicationPid()));
    QFile probe(canaryPath);
    if (!probe.open(QIODevice::WriteOnly)) {
        return {false, canonical, QString("Каталог захищений від запису або змонтований у режимі read-only:\n%1").arg(canonical)};
    }
    probe.write("canary", 6);
    probe.close();
    probe.remove();

    return {true, canonical, ""};
}

QtSettingsWidget::QtSettingsWidget(QWidget* parent)
    : QWidget(parent) {
    setupUi();
}

void QtSettingsWidget::setPresenter(SettingsPresenter* presenter) {
    m_presenter = presenter;
}

void QtSettingsWidget::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(16);

    // Header
    auto* titleLabel = new QLabel("⚙️ Settings & Diagnostics", this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    mainLayout->addWidget(titleLabel);

    // Settings Grid
    auto* topLayout = new QHBoxLayout();
    topLayout->setSpacing(16);

    // Group 1: Network & Storage
    auto* netGroup = new QGroupBox("Network && Storage", this);
    auto* netForm = new QFormLayout(netGroup);
    netForm->setContentsMargins(16, 20, 16, 16);
    netForm->setSpacing(12);

    m_portSpin = new QSpinBox(netGroup);
    m_portSpin->setRange(1024, 65535);
    m_portSpin->setValue(8848);

    m_speedLimitSpin = new QSpinBox(netGroup);
    m_speedLimitSpin->setRange(0, 1000000);
    m_speedLimitSpin->setValue(25000);
    m_speedLimitSpin->setSuffix(" KB/s");
    m_speedLimitSpin->setSpecialValueText("Unlimited (0 KB/s)");

    m_bufferSpin = new QSpinBox(netGroup);
    m_bufferSpin->setRange(16, 2048);
    m_bufferSpin->setValue(64);
    m_bufferSpin->setSuffix(" KB");

    auto* pathContainer = new QVBoxLayout();
    pathContainer->setSpacing(4);

    auto* pathLayout = new QHBoxLayout();
    pathLayout->setSpacing(8);
    m_downloadPathEdit = new QLineEdit("./downloads", netGroup);
    m_downloadPathEdit->setPlaceholderText("Оберіть або введіть шлях до папки...");
    connect(m_downloadPathEdit, &QLineEdit::textChanged, this, &QtSettingsWidget::updatePathStatusLabel);

    m_browsePathBtn = new QPushButton("📁 Обрати папку...", netGroup);
    m_browsePathBtn->setToolTip("Відкрити стандартний діалог вибору папки");
    connect(m_browsePathBtn, &QPushButton::clicked, this, &QtSettingsWidget::onBrowseDownloadPathClicked);

    m_createFolderBtn = new QPushButton("➕ Створити папку...", netGroup);
    m_createFolderBtn->setToolTip("Створити нову підпапку для збереження файлів");
    connect(m_createFolderBtn, &QPushButton::clicked, this, &QtSettingsWidget::onCreateFolderClicked);

    pathLayout->addWidget(m_downloadPathEdit, 1);
    pathLayout->addWidget(m_browsePathBtn);
    pathLayout->addWidget(m_createFolderBtn);
    pathContainer->addLayout(pathLayout);

    m_pathStatusLabel = new QLabel(netGroup);
    m_pathStatusLabel->setStyleSheet("font-size: 11px; margin-left: 2px;");
    pathContainer->addWidget(m_pathStatusLabel);

    netForm->addRow("Listening Port:", m_portSpin);
    netForm->addRow("Transfer Speed Limit:", m_speedLimitSpin);
    netForm->addRow("Chunk Buffer Size:", m_bufferSpin);
    netForm->addRow("Папка збереження:", pathContainer);
    topLayout->addWidget(netGroup);

    // Group 2: Protocol & Security
    auto* secGroup = new QGroupBox("Security && Protocols", this);
    auto* secLayout = new QVBoxLayout(secGroup);
    secLayout->setContentsMargins(16, 20, 16, 16);
    secLayout->setSpacing(12);

    m_autoAcceptCheck = new QCheckBox("Auto-accept transfers from known peers", secGroup);
    m_autoAcceptCheck->setChecked(true);

    m_encryptCheck = new QCheckBox("Enforce SHA-256 verification (Required by protocol)", secGroup);
    m_encryptCheck->setChecked(true);
    m_encryptCheck->setEnabled(false);
    m_encryptCheck->setToolTip("SHA-256 data integrity verification is mandatory in LAN-Drop.");

    m_airdropCheck = new QCheckBox("Enable AirDrop compatibility responder", secGroup);
    m_airdropCheck->setChecked(false);

    auto* themeRow = new QHBoxLayout();
    auto* themeLbl = new QLabel("Theme:", secGroup);
    m_themeCombo = new QComboBox(secGroup);
    m_themeCombo->addItem("Dark Theme (Default)");
    m_themeCombo->addItem("Light Theme");
    connect(m_themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QtSettingsWidget::onThemeComboChanged);
    themeRow->addWidget(themeLbl);
    themeRow->addWidget(m_themeCombo);

    secLayout->addWidget(m_autoAcceptCheck);
    secLayout->addWidget(m_encryptCheck);
    secLayout->addWidget(m_airdropCheck);
    secLayout->addLayout(themeRow);
    topLayout->addWidget(secGroup);

    mainLayout->addLayout(topLayout);

    // Save bar
    auto* saveBar = new QHBoxLayout();
    m_saveBtn = new QPushButton("💾 Save Settings", this);
    m_saveBtn->setObjectName("primaryBtn");
    connect(m_saveBtn, &QPushButton::clicked, this, &QtSettingsWidget::onSaveClicked);

    saveBar->addStretch();
    saveBar->addWidget(m_saveBtn);
    mainLayout->addLayout(saveBar);

    // Group 3: Diagnostics Log
    auto* logGroup = new QGroupBox("Network Activity && Diagnostics Log", this);
    auto* logLayout = new QVBoxLayout(logGroup);
    logLayout->setContentsMargins(16, 20, 16, 16);
    logLayout->setSpacing(10);

    m_logViewer = new QPlainTextEdit(logGroup);
    m_logViewer->setReadOnly(true);
    m_logViewer->setMaximumBlockCount(2000);
    logLayout->addWidget(m_logViewer);

    auto* logActions = new QHBoxLayout();
    logActions->setSpacing(10);
    m_clearLogsBtn = new QPushButton("Clear Log", logGroup);
    connect(m_clearLogsBtn, &QPushButton::clicked, this, &QtSettingsWidget::onClearLogsClicked);

    auto* exportBtn = new QPushButton("Export Log...", logGroup);
    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        QString file = QFileDialog::getSaveFileName(this, "Export Log", "landrop_diagnostics.log");
        if (!file.isEmpty()) {
            QFile f(file);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&f);
                out << m_logViewer->toPlainText();
                QMessageBox::information(this, "Log Exported", "Diagnostic log saved successfully.");
            } else {
                QMessageBox::warning(this, "Export Failed", "Failed to write diagnostic log to: " + file);
            }
        }
    });

    logActions->addWidget(m_clearLogsBtn);
    logActions->addWidget(exportBtn);
    logActions->addStretch();
    logLayout->addLayout(logActions);

    mainLayout->addWidget(logGroup, 1);
}

void QtSettingsWidget::loadSettings(uint16_t port, uint32_t speedLimitKbps, bool autoAccept, bool airdropEnabled) {
    QMetaObject::invokeMethod(this, [this, port, speedLimitKbps, autoAccept, airdropEnabled]() {
        m_portSpin->setValue(port);
        m_speedLimitSpin->setValue(static_cast<int>(speedLimitKbps));
        m_autoAcceptCheck->setChecked(autoAccept);
        m_airdropCheck->setChecked(airdropEnabled);
    }, Qt::QueuedConnection);
}

void QtSettingsWidget::appendDiagnosticLog(const std::string& logLine) {
    QMetaObject::invokeMethod(this, [this, logLine]() {
        QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        m_logViewer->appendPlainText(QString("[%1] %2").arg(timestamp).arg(QString::fromStdString(logLine)));
    }, Qt::QueuedConnection);
}

void QtSettingsWidget::showSaveSuccess() {
    QMetaObject::invokeMethod(this, [this]() {
        QMessageBox::information(this, "Settings Saved", "LAN-Drop preferences have been updated and saved successfully.");
    }, Qt::QueuedConnection);
}

void QtSettingsWidget::setDownloadsPath(const std::string& path) {
    QMetaObject::invokeMethod(this, [this, path]() {
        if (m_downloadPathEdit) {
            m_downloadPathEdit->setText(QString::fromStdString(path));
            updatePathStatusLabel(QString::fromStdString(path));
        }
    }, Qt::QueuedConnection);
}

void QtSettingsWidget::updatePathStatusLabel(const QString& text) {
    if (!m_pathStatusLabel) return;
    if (text.trimmed().isEmpty()) {
        m_pathStatusLabel->setText("<span style='color: #e53935;'>⚠ Шлях до папки не може бути порожнім</span>");
        return;
    }
    auto res = validateWritableDirectory(text, false);
    if (res.ok) {
        m_pathStatusLabel->setText("<span style='color: #43a047;'>✓ Папка існує та доступна для запису</span>");
    } else {
        QDir dir(text.trimmed());
        if (!dir.exists()) {
            m_pathStatusLabel->setText("<span style='color: #fb8c00;'>ℹ Папка буде створена автоматично при збереженні</span>");
        } else {
            m_pathStatusLabel->setText(QString("<span style='color: #e53935;'>✗ %1</span>").arg(res.errorMessage));
        }
    }
}

void QtSettingsWidget::onSaveClicked() {
    QString rawPath = m_downloadPathEdit->text().trimmed();
    auto val = validateWritableDirectory(rawPath, true);
    if (!val.ok) {
        QMessageBox::warning(this, "Помилка каталогу збереження",
            QString("Неможливо зберегти налаштування:\n%1\nБудь ласка, вкажіть або створіть доступну для запису директорію.").arg(val.errorMessage));
        return;
    }

    // Set normalized absolute path back into edit
    m_downloadPathEdit->setText(val.resolvedPath);
    updatePathStatusLabel(val.resolvedPath);

    if (m_presenter) {
        m_presenter->onPortChanged(static_cast<uint16_t>(m_portSpin->value()));
        m_presenter->onSpeedLimitChanged(static_cast<uint32_t>(m_speedLimitSpin->value()));
        m_presenter->onAutoAcceptToggled(m_autoAcceptCheck->isChecked());
        m_presenter->onAirDropToggled(m_airdropCheck->isChecked());
        m_presenter->onDownloadsPathChanged(val.resolvedPath.toStdString());
        m_presenter->onSaveSettingsClicked();
    }
}

void QtSettingsWidget::onClearLogsClicked() {
    m_logViewer->clear();
    if (m_presenter) {
        m_presenter->onClearLogsClicked();
    }
}

void QtSettingsWidget::onBrowseDownloadPathClicked() {
    QString current = m_downloadPathEdit->text().trimmed();
    if (current.isEmpty()) {
        current = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }
    QString dir = QFileDialog::getExistingDirectory(
        this,
        "Оберіть або створіть папку для отриманих файлів",
        current,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    if (!dir.isEmpty()) {
        auto val = validateWritableDirectory(dir, true);
        if (!val.ok) {
            QMessageBox::warning(this, "Помилка каталогу збереження", val.errorMessage);
            return;
        }
        m_downloadPathEdit->setText(val.resolvedPath);
        updatePathStatusLabel(val.resolvedPath);
    }
}

void QtSettingsWidget::onCreateFolderClicked() {
    QString current = m_downloadPathEdit->text().trimmed();
    if (current.isEmpty()) {
        current = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (current.isEmpty()) {
            current = QDir::currentPath();
        }
    }

    bool ok = false;
    QString folderName = QInputDialog::getText(
        this,
        "Створити нову папку",
        "Введіть назву нової папки або відносний шлях:",
        QLineEdit::Normal,
        "LAN-Drop-Received",
        &ok
    );
    if (!ok || folderName.trimmed().isEmpty()) return;

    QDir parentDir(current);
    if (!parentDir.exists()) {
        parentDir.setPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    }
    QString targetPath = parentDir.filePath(folderName.trimmed());
    auto val = validateWritableDirectory(targetPath, true);
    if (!val.ok) {
        QMessageBox::warning(this, "Помилка створення папки", val.errorMessage);
        return;
    }

    m_downloadPathEdit->setText(val.resolvedPath);
    updatePathStatusLabel(val.resolvedPath);
    QMessageBox::information(this, "Папку створено", QString("Нова папка успішно створена та обрана:\n%1").arg(val.resolvedPath));
}

void QtSettingsWidget::onThemeComboChanged(int index) {
    QString themeName = (index == 1) ? "light" : "dark";
    emit themeChanged(themeName);
}

}
