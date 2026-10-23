#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <vector>
#include <functional>
#include <utility>
#include "landrop/ui/IView.hpp"

namespace landrop::ui {

class TransferPresenter;

class QtTransferWidget : public QWidget, public ITransferView {
    Q_OBJECT
public:
    explicit QtTransferWidget(QWidget* parent = nullptr);
    ~QtTransferWidget() override = default;

    void setPresenter(TransferPresenter* presenter);

    // ITransferView overrides
    void updateTransferTable(const std::vector<TransferViewModel>& transfers) override;
    void updateSelectedTransferDetails(const TransferViewModel& item) override;
    void showTransferError(const std::string& taskId, const std::string& error) override;

    void initiateSendFile(const std::string& targetPeerId, const std::string& filePath);

    void setPeerProvider(std::function<std::vector<std::pair<std::string, std::string>>()> provider) {
        m_peerProvider = std::move(provider);
    }

    void setDownloadsDirectory(std::string path);
    const std::string& getDownloadsDirectory() const { return m_downloadsDirectory; }

    QPushButton* getPauseBtn() const { return m_pauseBtn; }
    QPushButton* getResumeBtn() const { return m_resumeBtn; }
    QPushButton* getCancelBtn() const { return m_cancelBtn; }
    QPushButton* getClearBtn() const { return m_clearBtn; }
    QPushButton* getSendFileBtn() const { return m_sendFileBtn; }
    QPushButton* getSelectFolderBtn() const { return m_selectFolderBtn; }
    QPushButton* getOpenFolderBtn() const { return m_openFolderBtn; }
    QLabel* getDownloadsPathDisplay() const { return m_downloadsPathDisplay; }
    QLabel* getSpeedSummaryLabel() const { return m_speedSummaryLabel; }
    QTableWidget* getTable() const { return m_table; }

signals:
    void downloadsDirectoryChanged(const QString& newPath);

private slots:
    void onPauseClicked();
    void onResumeClicked();
    void onCancelClicked();
    void onClearClicked();
    void onSendFileClicked();
    void onSelectFolderClicked();
    void onOpenFolderClicked();
    void onTableRowSelected(int row, int column);

private:
    TransferPresenter* m_presenter{nullptr};

    QTableWidget* m_table{nullptr};
    QLabel* m_detailsLabel{nullptr};
    QLabel* m_speedSummaryLabel{nullptr};
    QLabel* m_downloadsPathDisplay{nullptr};
    QPushButton* m_pauseBtn{nullptr};
    QPushButton* m_resumeBtn{nullptr};
    QPushButton* m_cancelBtn{nullptr};
    QPushButton* m_clearBtn{nullptr};
    QPushButton* m_sendFileBtn{nullptr};
    QPushButton* m_selectFolderBtn{nullptr};
    QPushButton* m_openFolderBtn{nullptr};

    std::vector<TransferViewModel> m_currentTransfers;
    std::string m_selectedTaskId;
    std::string m_downloadsDirectory;
    std::function<std::vector<std::pair<std::string, std::string>>()> m_peerProvider;

    void setupUi();
    void updateActionButtons();
    void updateLocationBar();
};

}
