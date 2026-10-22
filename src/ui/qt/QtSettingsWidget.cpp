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

namespace landrop::ui {

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

    auto* pathLayout = new QHBoxLayout();
    pathLayout->setSpacing(8);
    m_downloadPathEdit = new QLineEdit("./downloads", netGroup);
    m_browsePathBtn = new QPushButton("Browse...", netGroup);
    connect(m_browsePathBtn, &QPushButton::clicked, this, &QtSettingsWidget::onBrowseDownloadPathClicked);
    pathLayout->addWidget(m_downloadPathEdit);
    pathLayout->addWidget(m_browsePathBtn);

    netForm->addRow("Listening Port:", m_portSpin);
    netForm->addRow("Transfer Speed Limit:", m_speedLimitSpin);
    netForm->addRow("Chunk Buffer Size:", m_bufferSpin);
    netForm->addRow("Downloads Path:", pathLayout);
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

void QtSettingsWidget::onSaveClicked() {
    if (m_presenter) {
        m_presenter->onPortChanged(static_cast<uint16_t>(m_portSpin->value()));
        m_presenter->onSpeedLimitChanged(static_cast<uint32_t>(m_speedLimitSpin->value()));
        m_presenter->onAutoAcceptToggled(m_autoAcceptCheck->isChecked());
        m_presenter->onAirDropToggled(m_airdropCheck->isChecked());
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
    QString dir = QFileDialog::getExistingDirectory(this, "Select Download Directory", m_downloadPathEdit->text());
    if (!dir.isEmpty()) {
        m_downloadPathEdit->setText(dir);
    }
}

void QtSettingsWidget::onThemeComboChanged(int index) {
    QString themeName = (index == 1) ? "light" : "dark";
    emit themeChanged(themeName);
}

}
