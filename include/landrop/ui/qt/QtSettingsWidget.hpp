#pragma once

#include <QWidget>
#include <QSpinBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include "landrop/ui/IView.hpp"

namespace landrop::ui {

class SettingsPresenter;

struct DirectoryValidation {
    bool ok{false};
    QString resolvedPath;
    QString errorMessage;
};

class QtSettingsWidget : public QWidget, public ISettingsView {
    Q_OBJECT
public:
    explicit QtSettingsWidget(QWidget* parent = nullptr);
    ~QtSettingsWidget() override = default;

    void setPresenter(SettingsPresenter* presenter);

    static DirectoryValidation validateWritableDirectory(const QString& rawPath, bool createIfMissing = true);

    QComboBox* getThemeCombo() const { return m_themeCombo; }
    QSpinBox* getPortSpin() const { return m_portSpin; }
    QSpinBox* getSpeedLimitSpin() const { return m_speedLimitSpin; }
    QCheckBox* getAutoAcceptCheck() const { return m_autoAcceptCheck; }
    QCheckBox* getEncryptCheck() const { return m_encryptCheck; }
    QPushButton* getSaveBtn() const { return m_saveBtn; }
    QPlainTextEdit* getLogViewer() const { return m_logViewer; }
    QLineEdit* getDownloadPathEdit() const { return m_downloadPathEdit; }
    QPushButton* getBrowsePathBtn() const { return m_browsePathBtn; }
    QPushButton* getCreateFolderBtn() const { return m_createFolderBtn; }
    QLabel* getPathStatusLabel() const { return m_pathStatusLabel; }

    // ISettingsView overrides

    void loadSettings(uint16_t port, uint32_t speedLimitKbps, bool autoAccept, bool airdropEnabled) override;
    void appendDiagnosticLog(const std::string& logLine) override;
    void showSaveSuccess() override;
    void setDownloadsPath(const std::string& path) override;

signals:
    void themeChanged(const QString& themeName);

private slots:
    void onSaveClicked();
    void onClearLogsClicked();
    void onBrowseDownloadPathClicked();
    void onCreateFolderClicked();
    void updatePathStatusLabel(const QString& text);
    void onThemeComboChanged(int index);

private:
    SettingsPresenter* m_presenter{nullptr};

    QSpinBox* m_portSpin{nullptr};
    QSpinBox* m_speedLimitSpin{nullptr};
    QSpinBox* m_bufferSpin{nullptr};
    QLineEdit* m_downloadPathEdit{nullptr};
    QPushButton* m_browsePathBtn{nullptr};
    QPushButton* m_createFolderBtn{nullptr};
    QLabel* m_pathStatusLabel{nullptr};
    QCheckBox* m_autoAcceptCheck{nullptr};
    QCheckBox* m_encryptCheck{nullptr};
    QCheckBox* m_airdropCheck{nullptr};
    QComboBox* m_themeCombo{nullptr};
    QPlainTextEdit* m_logViewer{nullptr};
    QPushButton* m_saveBtn{nullptr};
    QPushButton* m_clearLogsBtn{nullptr};

    void setupUi();
};

}
