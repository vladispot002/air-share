#pragma once

#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <unordered_set>
#include "landrop/ui/IView.hpp"
#include "landrop/tasks/TransferTask.hpp"

namespace landrop::ui {

class TransferPresenter {
public:
    using SendFileCallback = std::function<void(const std::string& taskId, const std::string& targetPeerId, const std::string& filePath)>;
    using CancelCallback = std::function<void(const std::string& taskId)>;

    explicit TransferPresenter(ITransferView* view);
    ~TransferPresenter() = default;

    void setView(ITransferView* view);
    void initialize(bool populateSampleData = false);
    void loadSampleData();

    void onSendFileClicked(const std::string& targetPeerId, const std::string& filePath);
    void onPauseTransferClicked(const std::string& taskId);
    void onResumeTransferClicked(const std::string& taskId);
    void onCancelTransferClicked(const std::string& taskId);
    void onClearHistoryClicked();
    void onSelectTransferRow(size_t rowIndex);

    void addTask(const std::shared_ptr<TransferTask>& task);
    void addRealTask(const std::shared_ptr<TransferTask>& task);
    void updateTaskProgress(const std::string& taskId, uint64_t bytesTransferred, double speedBytesPerSec, TransferState state, const std::string& message = "");

    void setSendFileCallback(SendFileCallback cb) { m_sendFileCallback = std::move(cb); }
    void setCancelCallback(CancelCallback cb) { m_cancelCallback = std::move(cb); }

    void tick();

    void setSpeedLimit(uint32_t speedKbps) { m_speedLimitKbps = speedKbps; }
    uint32_t getSpeedLimit() const { return m_speedLimitKbps; }

    const std::vector<std::shared_ptr<TransferTask>>& getTasks() const;

private:
    ITransferView* m_view{nullptr};
    std::vector<std::shared_ptr<TransferTask>> m_tasks;
    std::unordered_set<std::string> m_realTaskIds;
    size_t m_selectedIndex{0};
    int m_nextTaskNumber{43};
    uint32_t m_speedLimitKbps{0};

    SendFileCallback m_sendFileCallback;
    CancelCallback m_cancelCallback;

    void refreshView();
    std::shared_ptr<TransferTask> findTask(const std::string& taskId);
};

}
