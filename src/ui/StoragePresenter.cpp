#include "landrop/ui/StoragePresenter.hpp"
#include <fstream>
#include <filesystem>

#include <cstdlib>

namespace landrop::ui {

static std::string getDefaultSharedDirectory() {
    const char* custom = std::getenv("LANDROP_SHARED_DIR");
    if (custom && custom[0] != '\0') return std::string(custom);

    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::string(home) + "/LAN-Drop-Shared";
    }

    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile && userProfile[0] != '\0') {
        return std::string(userProfile) + "\\LAN-Drop-Shared";
    }

    return "./shared_drop";
}

StoragePresenter::StoragePresenter(IStorageView* view, std::string rootPath)
    : m_view(view),
      m_folderManager(rootPath.empty() ? getDefaultSharedDirectory() : std::move(rootPath)) {}

void StoragePresenter::setView(IStorageView* view) {
    m_view = view;
}

void StoragePresenter::initialize() {
    try {
        std::error_code ec;
        std::filesystem::create_directories(m_folderManager.getRootPath(), ec);
        if (!ec) {
            const std::vector<std::pair<std::string, size_t>> sampleFiles = {
                {"quarterly-report-Q3.pdf", 4 * 1024 * 1024},
                {"design-system-v2.fig", 12 * 1024 * 1024},
                {"deployment-bundle.tar.gz", 16 * 1024 * 1024},
                {"meeting-recordings.mp4", 24 * 1024 * 1024},
                {"client-contracts.zip", 2 * 1024 * 1024}
            };
            for (const auto& [fname, fsize] : sampleFiles) {
                std::filesystem::path p = std::filesystem::path(m_folderManager.getRootPath()) / fname;
                if (!std::filesystem::exists(p, ec)) {
                    std::ofstream out(p, std::ios::binary);
                    if (out.is_open()) {
                        std::string content = "LAN-Drop payload for " + fname;
                        out << content;
                        out.seekp(fsize > 0 ? fsize - 1 : 0);
                        out.write("\0", 1);
                    }
                }
            }
        }
        m_folderManager.scanDirectory();
        refreshView();
    } catch (...) {
        refreshView();
    }
}

void StoragePresenter::onSharedPathChanged(const std::string& newPath) {
    try {
        m_folderManager.setRootPath(newPath);
    } catch (const std::exception&) {
    }
    refreshView();
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
