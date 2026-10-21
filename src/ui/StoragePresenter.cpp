#include "landrop/ui/StoragePresenter.hpp"
#include <fstream>
#include <filesystem>

namespace landrop::ui {

StoragePresenter::StoragePresenter(IStorageView* view)
    : m_view(view), m_folderManager("./shared_drop") {}

void StoragePresenter::setView(IStorageView* view) {
    m_view = view;
}

void StoragePresenter::initialize() {
    std::filesystem::create_directories("./shared_drop");
    const std::vector<std::pair<std::string, size_t>> sampleFiles = {
        {"quarterly-report-Q3.pdf", 4 * 1024 * 1024},
        {"design-system-v2.fig", 12 * 1024 * 1024},
        {"deployment-bundle.tar.gz", 16 * 1024 * 1024},
        {"meeting-recordings.mp4", 24 * 1024 * 1024},
        {"client-contracts.zip", 2 * 1024 * 1024}
    };
    for (const auto& [fname, fsize] : sampleFiles) {
        std::string p = "./shared_drop/" + fname;
        if (!std::filesystem::exists(p)) {
            std::ofstream out(p, std::ios::binary);
            std::string content = "LAN-Drop payload for " + fname;
            out << content;
            out.seekp(fsize > 0 ? fsize - 1 : 0);
            out.write("\0", 1);
        }
    }

    m_folderManager.scanDirectory();
    refreshView();
}

void StoragePresenter::onSharedPathChanged(const std::string& newPath) {
    if (m_folderManager.setRootPath(newPath)) {
        refreshView();
    }
}

void StoragePresenter::onRescanClicked() {
    m_folderManager.scanDirectory();
    refreshView();
}

void StoragePresenter::onAddFileClicked(const std::string& filePath) {
    if (m_folderManager.addSharedFile(filePath)) {
        refreshView();
    }
}

void StoragePresenter::onRemoveFileClicked(const std::string& fileName) {
    if (m_folderManager.removeSharedFile(fileName)) {
        refreshView();
    }
}

void StoragePresenter::onSetPasswordClicked(const std::string& password) {
    m_folderManager.setAccessPassword(password);
    refreshView();
}

void StoragePresenter::onClearPasswordClicked() {
    m_folderManager.setAccessPassword("");
    refreshView();
}

const SharedFolderManager& StoragePresenter::getManager() const {
    return m_folderManager;
}

void StoragePresenter::refreshView() {
    if (!m_view) {
        return;
    }
    m_view->setSharedPath(m_folderManager.getRootPath());
    m_view->updateFileList(m_folderManager.getSharedFiles());
    m_view->setStorageStats(m_folderManager.getTotalSharedBytes(), m_folderManager.getSharedFileCount());
    m_view->setPasswordProtectionStatus(m_folderManager.hasPasswordProtection());
}

}
