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
    std::string sample1 = "./shared_drop/sample_document.txt";
    if (!std::filesystem::exists(sample1)) {
        std::ofstream out(sample1);
        out << "LAN-Drop shared document content sample.\n";
    }

    std::string sample2 = "./shared_drop/archive.bin";
    if (!std::filesystem::exists(sample2)) {
        std::ofstream out(sample2, std::ios::binary);
        std::vector<char> dummy(1024 * 128, 'X');
        out.write(dummy.data(), dummy.size());
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
