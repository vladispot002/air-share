#include "landrop/ui/qt/QtAccountWidget.hpp"
#include <QMessageBox>
#include <QInputDialog>
#include <QSplitter>

namespace landrop::ui {

QtAccountWidget::QtAccountWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void QtAccountWidget::setPresenter(AccountPresenter* presenter) {
    m_presenter = presenter;
    if (m_presenter) {
        m_presenter->setOnStateChanged([this]() {
            updateView();
        });
    }
    updateView();
}

void QtAccountWidget::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    // Profile & Status Header
    auto* headerBox = new QGroupBox("Account & Identity (Optional)", this);
    auto* headerLayout = new QVBoxLayout(headerBox);

    auto* topRow = new QHBoxLayout();
    m_statusLabel = new QLabel("👤 Guest Mode (Zero-friction anonymous transfers active)", headerBox);
    m_statusLabel->setStyleSheet("font-weight: bold; font-size: 14px;");

    m_authActionBtn = new QPushButton("Sign In / Register", headerBox);
    m_authActionBtn->setFixedWidth(160);
    connect(m_authActionBtn, &QPushButton::clicked, this, &QtAccountWidget::onAuthActionClicked);

    topRow->addWidget(m_statusLabel, 1);
    topRow->addWidget(m_authActionBtn);
    headerLayout->addLayout(topRow);

    m_userDetailsLabel = new QLabel("Using local guest ID. Transfers work without an account.", headerBox);
    m_userDetailsLabel->setStyleSheet("color: #8b949e; font-size: 12px;");
    headerLayout->addWidget(m_userDetailsLabel);

    // Login Form Box (shown when expanding sign in)
    m_loginBox = new QGroupBox("Quick Sign In / Register", this);
    auto* loginLayout = new QHBoxLayout(m_loginBox);
    m_emailInput = new QLineEdit(m_loginBox);
    m_emailInput->setPlaceholderText("Email or @username");
    m_nameInput = new QLineEdit(m_loginBox);
    m_nameInput->setPlaceholderText("Display Name (optional)");
    auto* submitLoginBtn = new QPushButton("Continue", m_loginBox);
    auto* magicBtn = new QPushButton("✉️ Magic Link", m_loginBox);

    loginLayout->addWidget(new QLabel("Email / ID:", m_loginBox));
    loginLayout->addWidget(m_emailInput, 1);
    loginLayout->addWidget(new QLabel("Name:", m_loginBox));
    loginLayout->addWidget(m_nameInput, 1);
    loginLayout->addWidget(submitLoginBtn);
    loginLayout->addWidget(magicBtn);
    m_loginBox->setVisible(false);

    connect(submitLoginBtn, &QPushButton::clicked, this, [this]() {
        QString email = m_emailInput->text().trimmed();
        if (email.isEmpty()) {
            QMessageBox::warning(this, "Input Required", "Please enter your email or username.");
            return;
        }
        QString name = m_nameInput->text().trimmed();
        if (m_presenter) {
            std::string err;
            if (!m_presenter->loginWithEmail(email.toStdString(), name.toStdString(), &err)) {
                QMessageBox::warning(this, "Sign In Error", QString::fromStdString(err));
            } else {
                m_loginBox->setVisible(false);
                m_emailInput->clear();
                m_nameInput->clear();
            }
        }
    });

    connect(magicBtn, &QPushButton::clicked, this, [this]() {
        QString email = m_emailInput->text().trimmed();
        if (email.isEmpty()) {
            QMessageBox::warning(this, "Input Required", "Please enter your email to receive a magic link.");
            return;
        }
        if (m_presenter) {
            std::string token, err;
            if (m_presenter->requestMagicLink(email.toStdString(), &token, &err)) {
                bool ok = false;
                QString enteredToken = QInputDialog::getText(this, "Magic Link Verification",
                    "A verification link has been sent.\nEnter token or confirm below:",
                    QLineEdit::Normal, QString::fromStdString(token), &ok);
                if (ok && !enteredToken.trimmed().isEmpty()) {
                    std::string verifyErr;
                    if (m_presenter->verifyMagicLink(enteredToken.trimmed().toStdString(), &verifyErr)) {
                        m_loginBox->setVisible(false);
                        m_emailInput->clear();
                        m_nameInput->clear();
                        QMessageBox::information(this, "Success", "Authenticated via Magic Link!");
                    } else {
                        QMessageBox::warning(this, "Verification Failed", QString::fromStdString(verifyErr));
                    }
                }
            } else {
                QMessageBox::warning(this, "Magic Link Error", QString::fromStdString(err));
            }
        }
    });

    mainLayout->addWidget(headerBox);
    mainLayout->addWidget(m_loginBox);

    // Splitter for Contacts and Devices
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // Contacts Column
    auto* contactsBox = new QGroupBox("👥 Contacts & Recent Recipients", splitter);
    auto* contactsLayout = new QVBoxLayout(contactsBox);

    auto* addContactRow = new QHBoxLayout();
    m_addContactInput = new QLineEdit(contactsBox);
    m_addContactInput->setPlaceholderText("Search or Add @username");
    m_addContactBtn = new QPushButton("Add Contact", contactsBox);
    connect(m_addContactBtn, &QPushButton::clicked, this, &QtAccountWidget::onAddContactClicked);

    addContactRow->addWidget(m_addContactInput, 1);
    addContactRow->addWidget(m_addContactBtn);
    contactsLayout->addLayout(addContactRow);

    m_contactsList = new QListWidget(contactsBox);
    connect(m_contactsList, &QListWidget::itemDoubleClicked, this, &QtAccountWidget::onContactItemDoubleClicked);
    contactsLayout->addWidget(m_contactsList, 1);

    // Devices Column
    auto* devicesBox = new QGroupBox("💻 My Devices & Pairing", splitter);
    auto* devicesLayout = new QVBoxLayout(devicesBox);

    m_devicesList = new QListWidget(devicesBox);
    devicesLayout->addWidget(m_devicesList, 1);

    auto* devBtnRow = new QHBoxLayout();
    m_pairDeviceBtn = new QPushButton("📷 Pair New Device (QR)", devicesBox);
    connect(m_pairDeviceBtn, &QPushButton::clicked, this, &QtAccountWidget::onPairDeviceClicked);
    devBtnRow->addWidget(m_pairDeviceBtn);

    auto* revokeBtn = new QPushButton("Revoke Selected", devicesBox);
    connect(revokeBtn, &QPushButton::clicked, this, [this]() {
        auto* item = m_devicesList->currentItem();
        if (!item || !m_presenter) return;
        QString devId = item->data(Qt::UserRole).toString();
        if (!devId.isEmpty()) {
            std::string err;
            if (m_presenter->revokeDevice(devId.toStdString(), &err)) {
                m_presenter->refreshDevices();
            } else {
                QMessageBox::warning(this, "Revoke Failed", QString::fromStdString(err));
            }
        }
    });
    devBtnRow->addWidget(revokeBtn);
    devicesLayout->addLayout(devBtnRow);

    splitter->addWidget(contactsBox);
    splitter->addWidget(devicesBox);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(splitter, 1);

    // History Column / Box
    auto* historyBox = new QGroupBox("📜 Synchronized Cloud Transfer History", this);
    auto* historyLayout = new QVBoxLayout(historyBox);

    m_historyList = new QListWidget(historyBox);
    historyLayout->addWidget(m_historyList, 1);

    auto* histBtnRow = new QHBoxLayout();
    m_refreshHistoryBtn = new QPushButton("↺ Refresh History", historyBox);
    connect(m_refreshHistoryBtn, &QPushButton::clicked, this, &QtAccountWidget::onRefreshHistoryClicked);
    histBtnRow->addWidget(m_refreshHistoryBtn);

    m_clearHistoryBtn = new QPushButton("🗑️ Clear Cloud History", historyBox);
    connect(m_clearHistoryBtn, &QPushButton::clicked, this, &QtAccountWidget::onClearHistoryClicked);
    histBtnRow->addWidget(m_clearHistoryBtn);

    historyLayout->addLayout(histBtnRow);
    mainLayout->addWidget(historyBox, 1);
}

void QtAccountWidget::updateView() {
    if (!m_presenter) return;

    if (m_presenter->isGuest()) {
        m_statusLabel->setText("👤 Guest Mode (Zero-friction anonymous transfers active)");
        m_userDetailsLabel->setText(QString("Guest ID: %1. Accounts are optional.").arg(QString::fromStdString(m_presenter->getGuestId())));
        m_authActionBtn->setText("Sign In / Register");
        m_contactsList->clear();
        m_devicesList->clear();
        if (m_historyList) m_historyList->clear();
    } else {
        auto user = m_presenter->getCurrentUser();
        m_statusLabel->setText(QString("🛡️ %1 (%2)").arg(QString::fromStdString(user.displayName), QString::fromStdString(user.landropId)));
        m_userDetailsLabel->setText(QString("Email: %1 • Status: %2").arg(QString::fromStdString(user.email), QString::fromStdString(user.status)));
        m_authActionBtn->setText("Sign Out");

        // Contacts list
        m_contactsList->clear();
        for (const auto& c : m_presenter->getContacts()) {
            QString label = QString("%1 (%2) — [%3]")
                .arg(QString::fromStdString(c.displayName.empty() ? c.landropId : c.displayName))
                .arg(QString::fromStdString(c.landropId))
                .arg(QString::fromStdString(c.state));
            if (c.isFavorite) label = "⭐ " + label;
            auto* item = new QListWidgetItem(label, m_contactsList);
            item->setData(Qt::UserRole, QString::fromStdString(c.targetUserId));
            item->setData(Qt::UserRole + 1, QString::fromStdString(c.landropId));
            m_contactsList->addItem(item);
        }

        // Devices list
        m_devicesList->clear();
        for (const auto& d : m_presenter->getDevices()) {
            QString statusIcon = (d.presence == "online") ? "🟢" : "⚪";
            QString label = QString("%1 %2 (%3) — %4")
                .arg(statusIcon)
                .arg(QString::fromStdString(d.deviceName))
                .arg(QString::fromStdString(d.platform))
                .arg(QString::fromStdString(d.presence));
            auto* item = new QListWidgetItem(label, m_devicesList);
            item->setData(Qt::UserRole, QString::fromStdString(d.deviceId));
            m_devicesList->addItem(item);
        }

        // History list
        if (m_historyList) {
            m_historyList->clear();
            for (const auto& h : m_presenter->getHistory()) {
                QString dirIcon = (h.direction == "upload") ? "⬆️ Sent" : "⬇️ Received";
                QString fileSummary;
                if (!h.files.empty()) {
                    fileSummary = QString::fromStdString(h.files[0].fileName);
                    if (h.files.size() > 1) {
                        fileSummary += QString(" (+%1 more)").arg(h.files.size() - 1);
                    }
                } else {
                    fileSummary = "Files";
                }
                double mb = static_cast<double>(h.totalBytes) / (1024.0 * 1024.0);
                QString peerInfo = QString::fromStdString(!h.peerLandropId.empty() ? h.peerLandropId : (!h.peerDeviceName.empty() ? h.peerDeviceName : "Anonymous"));
                QString label = QString("%1: %2 (%3 MB) — Peer: %4 — [%5]")
                    .arg(dirIcon, fileSummary, QString::number(mb, 'f', 2), peerInfo, QString::fromStdString(h.status));
                auto* item = new QListWidgetItem(label, m_historyList);
                m_historyList->addItem(item);
            }
        }
    }
}

void QtAccountWidget::onAuthActionClicked() {
    if (!m_presenter) return;

    if (m_presenter->isGuest()) {
        m_loginBox->setVisible(!m_loginBox->isVisible());
    } else {
        m_presenter->logout();
        m_loginBox->setVisible(false);
    }
}

void QtAccountWidget::onAddContactClicked() {
    if (!m_presenter) return;
    QString target = m_addContactInput->text().trimmed();
    if (target.isEmpty()) {
        QMessageBox::warning(this, "Input Required", "Please enter a @landrop_id.");
        return;
    }
    std::string err;
    if (m_presenter->addContact(target.toStdString(), &err)) {
        m_addContactInput->clear();
        QMessageBox::information(this, "Success", "Contact request sent successfully!");
    } else {
        QMessageBox::warning(this, "Contact Request Failed", QString::fromStdString(err));
    }
}

void QtAccountWidget::onPairDeviceClicked() {
    if (!m_presenter || m_presenter->isGuest()) {
        QMessageBox::warning(this, "Authentication Required", "Sign in to generate a device pairing QR code.");
        return;
    }
    DevicePairingInfo session;
    std::string err;
    if (m_presenter->generatePairingSession(session, &err)) {
        QMessageBox::information(this, "Device Pairing QR Code",
            QString("Pairing Session Generated!\nSession ID: %1\nSecret: %2\nScan this code from your mobile or secondary device to pair.")
            .arg(QString::fromStdString(session.sessionId), QString::fromStdString(session.secret)));
    } else {
        QMessageBox::warning(this, "Pairing Error", QString::fromStdString(err));
    }
}

void QtAccountWidget::onContactItemDoubleClicked(QListWidgetItem* item) {
    if (!item) return;
    QString targetUserId = item->data(Qt::UserRole).toString();
    QString landropId = item->data(Qt::UserRole + 1).toString();
    if (!targetUserId.isEmpty()) {
        emit sendFileToContact(targetUserId, landropId);
    }
}

void QtAccountWidget::onRefreshHistoryClicked() {
    if (!m_presenter) return;
    std::string err;
    if (!m_presenter->refreshHistory(20, &err)) {
        if (!err.empty()) {
            QMessageBox::warning(this, "History Error", QString::fromStdString(err));
        }
    }
}

void QtAccountWidget::onClearHistoryClicked() {
    if (!m_presenter) return;
    if (m_presenter->isGuest()) return;
    auto reply = QMessageBox::question(this, "Clear History", "Are you sure you want to clear your synchronized transfer history?");
    if (reply == QMessageBox::Yes) {
        std::string err;
        if (!m_presenter->clearHistory(&err)) {
            QMessageBox::warning(this, "Clear History Failed", QString::fromStdString(err));
        }
    }
}

} // namespace landrop::ui
