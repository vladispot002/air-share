#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <vector>
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

private slots:
    void onPauseClicked();
    void onResumeClicked();
    void onCancelClicked();
    void onClearClicked();
    void onSendFileClicked();
    void onOpenFolderClicked();
    void onTableRowSelected(int row, int column);

private:
    TransferPresenter* m_presenter{nullptr};

    QTableWidget* m_table{nullptr};
    QLabel* m_detailsLabel{nullptr};
    QLabel* m_speedSummaryLabel{nullptr};
    QPushButton* m_pauseBtn{nullptr};
    QPushButton* m_resumeBtn{nullptr};
    QPushButton* m_cancelBtn{nullptr};
    QPushButton* m_clearBtn{nullptr};
    QPushButton* m_sendFileBtn{nullptr};
    QPushButton* m_openFolderBtn{nullptr};

    std::vector<TransferViewModel> m_currentTransfers;
    std::string m_selectedTaskId;

    void setupUi();
};

}
