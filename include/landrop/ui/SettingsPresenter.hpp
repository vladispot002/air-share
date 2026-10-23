#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "landrop/ui/IView.hpp"

namespace landrop::ui {

class SettingsPresenter {
public:
    explicit SettingsPresenter(ISettingsView* view);
    ~SettingsPresenter() = default;

    void setView(ISettingsView* view);
    void initialize();

    void onPortChanged(uint16_t port);
    void onSpeedLimitChanged(uint32_t speedKbps);
    void onAutoAcceptToggled(bool autoAccept);
    void onAirDropToggled(bool airdropEnabled);
    void onDownloadsPathChanged(const std::string& path);
    void onSaveSettingsClicked();
    void onClearLogsClicked();

    uint16_t getPort() const { return m_port; }
    uint32_t getSpeedLimitKbps() const { return m_speedLimitKbps; }
    bool isAutoAccept() const { return m_autoAccept; }
    bool isAirDropEnabled() const { return m_airdropEnabled; }
    const std::string& getDownloadsPath() const { return m_downloadsPath; }
    void setDownloadsPath(std::string path);

    void addLog(const std::string& log);

    static std::string getDefaultConfigDirectory();
    static std::string getDefaultConfigPath();

    void setConfigPath(std::string path);
    const std::string& getConfigPath() const { return m_configPath; }

    bool loadSettings();
    bool saveSettings() const;

    bool loadFromJson(const std::string& jsonStr);
    std::string saveToJson() const;
    bool loadSettingsFromFile(const std::string& filePath);
    bool saveSettingsToFile(const std::string& filePath) const;

private:
    ISettingsView* m_view{nullptr};
    std::string m_configPath;
    uint16_t m_port{8848};
    uint32_t m_speedLimitKbps{0};
    bool m_autoAccept{true};
    bool m_airdropEnabled{false};
    std::string m_downloadsPath{"./downloads"};
    std::vector<std::string> m_logs;
};

}
