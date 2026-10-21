#include "landrop/ui/TransferPresenter.hpp"
#include "landrop/tasks/P2PTransferTask.hpp"
#include <algorithm>

namespace landrop::ui {

TransferPresenter::TransferPresenter(ITransferView* view)
    : m_view(view) {}

void TransferPresenter::setView(ITransferView* view) {
    m_view = view;
}

void TransferPresenter::initialize() {
    auto task1 = std::make_shared<P2PTransferTask>("task-001", "system_update.tar.gz", 25 * 1024 * 1024, TransferDirection::Download, "peer-local-01");
    task1->initialize();
    std::vector<uint8_t> dummy(10 * 1024 * 1024, 0x55);
    task1->processChunk(0, dummy);
    task1->updateSpeed(10 * 1024 * 1024, 1.5);

    auto task2 = std::make_shared<P2PTransferTask>("task-002", "presentation_final.pdf", 8 * 1024 * 1024, TransferDirection::Upload, "peer-local-02");
    task2->initialize();
    task2->updateSpeed(4 * 1024 * 1024, 1.0);

    m_tasks.push_back(task1);
    m_tasks.push_back(task2);

    refreshView();
}

void TransferPresenter::onSendFileClicked(const std::string& targetPeerId, const std::string& filePath) {
    std::string taskId = "task-" + std::to_string(m_tasks.size() + 1);
    auto task = std::make_shared<P2PTransferTask>(taskId, filePath, 5 * 1024 * 1024, TransferDirection::Upload, targetPeerId);
    task->initialize();
    m_tasks.push_back(task);
    refreshView();
}

void TransferPresenter::onPauseTransferClicked(const std::string& taskId) {
    auto task = findTask(taskId);
    if (task) {
        task->pause();
        refreshView();
    }
}

void TransferPresenter::onResumeTransferClicked(const std::string& taskId) {
    auto task = findTask(taskId);
    if (task) {
        task->resume();
        refreshView();
    }
}

void TransferPresenter::onCancelTransferClicked(const std::string& taskId) {
    auto task = findTask(taskId);
    if (task) {
        task->cancel();
        refreshView();
    }
}

void TransferPresenter::onClearHistoryClicked() {
    auto it = std::remove_if(m_tasks.begin(), m_tasks.end(), [](const std::shared_ptr<TransferTask>& t) {
        return t->getState() == TransferState::Completed || t->getState() == TransferState::Cancelled || t->getState() == TransferState::Failed;
    });
    m_tasks.erase(it, m_tasks.end());
    m_selectedIndex = 0;
    refreshView();
}

void TransferPresenter::onSelectTransferRow(size_t rowIndex) {
    if (rowIndex < m_tasks.size()) {
        m_selectedIndex = rowIndex;
        if (m_view) {
            const auto& t = m_tasks[rowIndex];
            TransferViewModel vm;
            vm.taskId = t->getTaskId();
            vm.fileName = t->getFileName();
            vm.fileSize = t->getFileSize();
            vm.bytesTransferred = t->getBytesTransferred();
            vm.progress = t->getProgressPercentage();
            vm.speedBytesPerSec = t->getCurrentSpeedBps();
            vm.stateStr = transferStateToString(t->getState());
            vm.direction = (t->getDirection() == TransferDirection::Upload) ? "Upload" : "Download";
            m_view->updateSelectedTransferDetails(vm);
        }
    }
}

void TransferPresenter::addTask(const std::shared_ptr<TransferTask>& task) {
    m_tasks.push_back(task);
    refreshView();
}

void TransferPresenter::tick() {
    for (auto& task : m_tasks) {
        if (task->getState() == TransferState::Transferring) {
            std::vector<uint8_t> block(128 * 1024, 0);
            task->processChunk(0, block);
            task->updateSpeed(128 * 1024, 0.2);
        }
    }
    refreshView();
}

const std::vector<std::shared_ptr<TransferTask>>& TransferPresenter::getTasks() const {
    return m_tasks;
}

void TransferPresenter::refreshView() {
    if (!m_view) {
        return;
    }
    std::vector<TransferViewModel> vms;
    for (const auto& t : m_tasks) {
        TransferViewModel vm;
        vm.taskId = t->getTaskId();
        vm.fileName = t->getFileName();
        vm.fileSize = t->getFileSize();
        vm.bytesTransferred = t->getBytesTransferred();
        vm.progress = t->getProgressPercentage();
        vm.speedBytesPerSec = t->getCurrentSpeedBps();
        vm.stateStr = transferStateToString(t->getState());
        vm.direction = (t->getDirection() == TransferDirection::Upload) ? "Upload" : "Download";
        vms.push_back(std::move(vm));
    }
    m_view->updateTransferTable(vms);
    if (!vms.empty() && m_selectedIndex < vms.size()) {
        m_view->updateSelectedTransferDetails(vms[m_selectedIndex]);
    }
}

std::shared_ptr<TransferTask> TransferPresenter::findTask(const std::string& taskId) {
    for (auto& t : m_tasks) {
        if (t->getTaskId() == taskId) {
            return t;
        }
    }
    return nullptr;
}

}
