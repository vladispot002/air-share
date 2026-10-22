#pragma once

#include <QWidget>
#include <QListWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <vector>
#include "landrop/ui/IView.hpp"

namespace landrop::ui {

class StoragePresenter;

class QtStorageWidget : public QWidget, public IStorageView {
    Q_OBJECT
public:
    explicit QtStorageWidget(QWidget* parent = nullptr);
    ~QtStorageWidget() override = default;

    void setPresenter(StoragePresenter* presenter);

    // IStorageView overrides
    void setSharedPath(const std::string& path) override;
    void updateFileList(const std::vector<FileMetadata>& files) override;
    void setStorageStats(uint64_t totalBytes, size_t fileCount) override;
    void setPasswordProtectionStatus(bool isProtected) override;

private slots:
    void onAddFolderClicked();
    void onRemoveFolderClicked();
    void onSetPinClicked();
    void onClearPinClicked();
    void onRescanClicked();

private:
    StoragePresenter* m_presenter{nullptr};

    QLabel* m_pathLabel{nullptr};
    QLabel* m_statsLabel{nullptr};
    QLabel* m_pinStatusLabel{nullptr};
    QTableWidget* m_fileTable{nullptr};
    QPushButton* m_addFolderBtn{nullptr};
    QPushButton* m_removeFolderBtn{nullptr};
    QPushButton* m_setPinBtn{nullptr};
    QPushButton* m_clearPinBtn{nullptr};
    QPushButton* m_rescanBtn{nullptr};

    void setupUi();
};

}
