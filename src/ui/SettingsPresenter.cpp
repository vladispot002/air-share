#include "landrop/ui/SettingsPresenter.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

namespace landrop::ui {

SettingsPresenter::SettingsPresenter(ISettingsView* view)
    : m_view(view) {}

void SettingsPresenter::setView(ISettingsView* view) {
    m_view = view;
}

void SettingsPresenter::initialize() {
    m_port = 8848;
    m_speedLimitKbps = 25000;
    m_autoAccept = false;
    m_airdropEnabled = true;

    // Attempt to load saved JSON configuration if available
    loadSettingsFromFile("settings.json");

    addLog("LAN-Drop daemon started on port " + std::to_string(m_port));
    addLog("Scanning local subnet for peers...");
    addLog("Multicast beacon discovery active");
    addLog("SHA-256 integrity verification engine active");

    if (m_view) {
        m_view->loadSettings(m_port, m_speedLimitKbps, m_autoAccept, m_airdropEnabled);
    }
}

void SettingsPresenter::onPortChanged(uint16_t port) {
    m_port = port;
    addLog("Network listen port updated to " + std::to_string(port));
}

void SettingsPresenter::onSpeedLimitChanged(uint32_t speedKbps) {
    m_speedLimitKbps = speedKbps;
    addLog("Transfer speed limit set to " + (speedKbps == 0 ? "Unlimited" : std::to_string(speedKbps) + " KB/s"));
}

void SettingsPresenter::onAutoAcceptToggled(bool autoAccept) {
    m_autoAccept = autoAccept;
    addLog("Auto-accept incoming files: " + std::string(autoAccept ? "Enabled" : "Disabled"));
}

void SettingsPresenter::onAirDropToggled(bool airdropEnabled) {
    m_airdropEnabled = airdropEnabled;
    addLog("Multicast beacon discovery: " + std::string(airdropEnabled ? "Enabled" : "Disabled"));
}

void SettingsPresenter::onSaveSettingsClicked() {
    saveSettingsToFile("settings.json");
    addLog("Configuration saved to settings.json successfully.");
    if (m_view) {
        m_view->showSaveSuccess();
    }
}

void SettingsPresenter::onClearLogsClicked() {
    m_logs.clear();
    addLog("Diagnostics log cleared.");
}

void SettingsPresenter::addLog(const std::string& log) {
    m_logs.push_back(log);
    if (m_view) {
        m_view->appendDiagnosticLog(log);
    }
}

bool SettingsPresenter::loadFromJson(const std::string& jsonStr) {
    try {
        auto j = nlohmann::json::parse(jsonStr);
        if (j.contains("port")) m_port = j["port"].get<uint16_t>();
        if (j.contains("speedLimitKbps")) m_speedLimitKbps = j["speedLimitKbps"].get<uint32_t>();
        if (j.contains("autoAccept")) m_autoAccept = j["autoAccept"].get<bool>();
        if (j.contains("airdropEnabled")) m_airdropEnabled = j["airdropEnabled"].get<bool>();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string SettingsPresenter::saveToJson() const {
    nlohmann::json j;
    j["port"] = m_port;
    j["speedLimitKbps"] = m_speedLimitKbps;
    j["autoAccept"] = m_autoAccept;
    j["airdropEnabled"] = m_airdropEnabled;
    return j.dump(4);
}

bool SettingsPresenter::loadSettingsFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return loadFromJson(content);
}

bool SettingsPresenter::saveSettingsToFile(const std::string& filePath) const {
    std::ofstream file(filePath);
    if (!file.is_open()) return false;
    file << saveToJson();
    return true;
}

}
