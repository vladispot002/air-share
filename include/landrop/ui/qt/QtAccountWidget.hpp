#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <memory>
#include "landrop/ui/AccountPresenter.hpp"

namespace landrop::ui {

class QtAccountWidget : public QWidget {
    Q_OBJECT
public:
    explicit QtAccountWidget(QWidget* parent = nullptr);
    ~QtAccountWidget() override = default;

    void setPresenter(AccountPresenter* presenter);
    AccountPresenter* getPresenter() const { return m_presenter; }

    QLabel* getStatusLabel() const { return m_statusLabel; }
    QLabel* getUserDetailsLabel() const { return m_userDetailsLabel; }
    QPushButton* getAuthActionBtn() const { return m_authActionBtn; }
    QLineEdit* getAddContactInput() const { return m_addContactInput; }
    QPushButton* getAddContactBtn() const { return m_addContactBtn; }
    QListWidget* getContactsList() const { return m_contactsList; }
    QListWidget* getDevicesList() const { return m_devicesList; }
    QPushButton* getPairDeviceBtn() const { return m_pairDeviceBtn; }
    QListWidget* getHistoryList() const { return m_historyList; }
    QPushButton* getRefreshHistoryBtn() const { return m_refreshHistoryBtn; }
    QPushButton* getClearHistoryBtn() const { return m_clearHistoryBtn; }

    void updateView();

signals:
    void sendFileToContact(const QString& targetUserId, const QString& landropId);

private slots:
    void onAuthActionClicked();
    void onAddContactClicked();
    void onPairDeviceClicked();
    void onContactItemDoubleClicked(QListWidgetItem* item);
    void onRefreshHistoryClicked();
    void onClearHistoryClicked();

private:
    void setupUi();

    AccountPresenter* m_presenter{nullptr};

    QLabel* m_statusLabel{nullptr};
    QLabel* m_userDetailsLabel{nullptr};
    QPushButton* m_authActionBtn{nullptr};

    QLineEdit* m_emailInput{nullptr};
    QLineEdit* m_nameInput{nullptr};
    QGroupBox* m_loginBox{nullptr};

    QLineEdit* m_addContactInput{nullptr};
    QPushButton* m_addContactBtn{nullptr};
    QListWidget* m_contactsList{nullptr};

    QListWidget* m_devicesList{nullptr};
    QPushButton* m_pairDeviceBtn{nullptr};

    QListWidget* m_historyList{nullptr};
    QPushButton* m_refreshHistoryBtn{nullptr};
    QPushButton* m_clearHistoryBtn{nullptr};
};

} // namespace landrop::ui
