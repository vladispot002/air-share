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
    void onSaveSettingsClicked();
    void onClearLogsClicked();

    uint16_t getPort() const { return m_port; }
    uint32_t getSpeedLimitKbps() const { return m_speedLimitKbps; }
    bool isAutoAccept() const { return m_autoAccept; }
    bool isAirDropEnabled() const { return m_airdropEnabled; }

    void addLog(const std::string& log);

    bool loadFromJson(const std::string& jsonStr);
    std::string saveToJson() const;
    bool loadSettingsFromFile(const std::string& filePath = "settings.json");
    bool saveSettingsToFile(const std::string& filePath = "settings.json") const;

private:
    ISettingsView* m_view{nullptr};
    uint16_t m_port{8848};
    uint32_t m_speedLimitKbps{0};
    bool m_autoAccept{true};
    bool m_airdropEnabled{false};
    std::vector<std::string> m_logs;
};

}
