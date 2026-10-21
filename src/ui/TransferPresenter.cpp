#include "landrop/ui/TransferPresenter.hpp"
#include "landrop/tasks/P2PTransferTask.hpp"
#include <algorithm>
#include <filesystem>

namespace landrop::ui {

TransferPresenter::TransferPresenter(ITransferView* view)
    : m_view(view) {}

void TransferPresenter::setView(ITransferView* view) {
    m_view = view;
}

void TransferPresenter::initialize() {
    auto task1 = std::make_shared<P2PTransferTask>("T-0042", "project-assets.zip", 248ULL * 1024 * 1024, TransferDirection::Upload, "peer-thinkpad");
    task1->initialize();
    task1->setState(TransferState::Transferring);
    task1->setBytesTransferred(static_cast<uint64_t>(248ULL * 1024 * 1024 * 0.67));
    task1->updateSpeed(static_cast<uint64_t>(24.5 * 1024 * 1024), 1.0);

    auto task2 = std::make_shared<P2PTransferTask>("T-0041", "backup-2026-10.tar", 1200ULL * 1024 * 1024, TransferDirection::Upload, "peer-cloud");
    task2->initialize();
    task2->setBytesTransferred(1200ULL * 1024 * 1024);
    task2->setState(TransferState::Completed);

    auto task3 = std::make_shared<P2PTransferTask>("T-0040", "client-presentation.pdf", static_cast<uint64_t>(18.3 * 1024 * 1024), TransferDirection::Download, "peer-macbook");
    task3->initialize();
    task3->setBytesTransferred(static_cast<uint64_t>(18.3 * 1024 * 1024));
    task3->setState(TransferState::Completed);

    auto task4 = std::make_shared<P2PTransferTask>("T-0039", "system-logs.tar.gz", 342ULL * 1024 * 1024, TransferDirection::Download, "peer-galaxy");
    task4->initialize();
    task4->setState(TransferState::Transferring);
    task4->setBytesTransferred(static_cast<uint64_t>(342ULL * 1024 * 1024 * 0.34));
    task4->updateSpeed(static_cast<uint64_t>(12.1 * 1024 * 1024), 1.0);

    auto task5 = std::make_shared<P2PTransferTask>("T-0038", "design-exports.fig", static_cast<uint64_t>(87.4 * 1024 * 1024), TransferDirection::Upload, "peer-thinkpad");
    task5->initialize();
    task5->setBytesTransferred(static_cast<uint64_t>(87.4 * 1024 * 1024));
    task5->setState(TransferState::Completed);

    auto task6 = std::make_shared<P2PTransferTask>("T-0037", "raw-footage-edit.mov", 4700ULL * 1024 * 1024, TransferDirection::Download, "peer-macbook");
    task6->initialize();
    task6->setState(TransferState::Transferring);
    task6->setBytesTransferred(static_cast<uint64_t>(4700ULL * 1024 * 1024 * 0.08));
    task6->updateSpeed(static_cast<uint64_t>(31.2 * 1024 * 1024), 1.0);

    m_tasks.push_back(task1);
    m_tasks.push_back(task2);
    m_tasks.push_back(task3);
    m_tasks.push_back(task4);
    m_tasks.push_back(task5);
    m_tasks.push_back(task6);

    refreshView();
}

void TransferPresenter::onSendFileClicked(const std::string& targetPeerId, const std::string& filePath) {
    std::error_code ec;
    std::filesystem::path p(filePath);
    if (!std::filesystem::is_regular_file(p, ec)) {
        if (m_view) {
            m_view->showTransferError("-", "File not found: " + filePath);
        }
        return;
    }
    uint64_t size = std::filesystem::file_size(p, ec);
    if (ec) {
        size = 0;
    }
    std::string taskId = "T-" + std::to_string(m_nextTaskNumber++);
    auto task = std::make_shared<P2PTransferTask>(taskId, p.filename().string(), size, TransferDirection::Upload, targetPeerId);
    task->initialize();
    m_tasks.push_back(task);
    m_selectedIndex = m_tasks.size() - 1;
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
    if (task && task->getState() != TransferState::Completed) {
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
    constexpr double kTickSeconds = 0.3;
    constexpr double kDefaultSpeed = 8.0 * 1024 * 1024;
    for (auto& task : m_tasks) {
        if (task->getState() == TransferState::Connecting) {
            task->setState(TransferState::Transferring);
        }
        if (task->getState() != TransferState::Transferring) {
            continue;
        }
        double speed = task->getCurrentSpeedBps();
        if (speed <= 0.0) {
            speed = kDefaultSpeed;
            task->updateSpeed(static_cast<uint64_t>(speed), 1.0);
        }
        uint64_t step = static_cast<uint64_t>(speed * kTickSeconds);
        uint64_t total = task->getFileSize();
        uint64_t next = std::min(total, task->getBytesTransferred() + step);
        task->setBytesTransferred(next);
        if (next >= total) {
            task->setState(TransferState::Completed);
            task->updateSpeed(0, 1.0);
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
