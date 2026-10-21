#pragma once

#include <memory>
#include <vector>
#include <string>
#include "landrop/ui/IView.hpp"
#include "landrop/tasks/TransferTask.hpp"

namespace landrop::ui {

class TransferPresenter {
public:
    explicit TransferPresenter(ITransferView* view);
    ~TransferPresenter() = default;

    void setView(ITransferView* view);
    void initialize();

    void onSendFileClicked(const std::string& targetPeerId, const std::string& filePath);
    void onPauseTransferClicked(const std::string& taskId);
    void onResumeTransferClicked(const std::string& taskId);
    void onCancelTransferClicked(const std::string& taskId);
    void onClearHistoryClicked();
    void onSelectTransferRow(size_t rowIndex);

    void addTask(const std::shared_ptr<TransferTask>& task);
    void tick();

    const std::vector<std::shared_ptr<TransferTask>>& getTasks() const;

private:
    ITransferView* m_view{nullptr};
    std::vector<std::shared_ptr<TransferTask>> m_tasks;
    size_t m_selectedIndex{0};

    void refreshView();
    std::shared_ptr<TransferTask> findTask(const std::string& taskId);
};

}
