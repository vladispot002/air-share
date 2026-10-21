#pragma once

#include <memory>
#include <string>
#include "landrop/ui/IView.hpp"
#include "landrop/core/SharedFolderManager.hpp"

namespace landrop::ui {

class StoragePresenter {
public:
    explicit StoragePresenter(IStorageView* view);
    ~StoragePresenter() = default;

    void setView(IStorageView* view);
    void initialize();

    void onSharedPathChanged(const std::string& newPath);
    void onRescanClicked();
    void onAddFileClicked(const std::string& filePath);
    void onRemoveFileClicked(const std::string& fileName);
    void onSetPasswordClicked(const std::string& password);
    void onClearPasswordClicked();

    const SharedFolderManager& getManager() const;

private:
    IStorageView* m_view{nullptr};
    SharedFolderManager m_folderManager;

    void refreshView();
};

}
