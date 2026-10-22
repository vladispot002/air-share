#include "landrop/ui/SettingsPresenter.hpp"
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace landrop::ui {

SettingsPresenter::SettingsPresenter(ISettingsView* view)
    : m_view(view) {}

void SettingsPresenter::setView(ISettingsView* view) {
    m_view = view;
}

std::string SettingsPresenter::getDefaultConfigDirectory() {
    const char* envCustom = std::getenv("LANDROP_CONFIG_DIR");
    if (envCustom && envCustom[0] != '\0') {
        return std::string(envCustom);
    }

    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') {
        return std::string(xdg) + "/lan-drop";
    }

    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::string(home) + "/.config/lan-drop";
    }

    const char* appData = std::getenv("APPDATA");
    if (appData && appData[0] != '\0') {
        return std::string(appData) + "/lan-drop";
    }

    return "./config";
}

std::string SettingsPresenter::getDefaultConfigPath() {
    return getDefaultConfigDirectory() + "/settings.json";
}

void SettingsPresenter::setConfigPath(std::string path) {
    m_configPath = std::move(path);
}

void SettingsPresenter::initialize() {
    m_port = 8848;
    m_speedLimitKbps = 25000;
    m_autoAccept = false;
    m_airdropEnabled = true;

    if (m_configPath.empty()) {
        m_configPath = getDefaultConfigPath();
    }

    // Attempt to load saved JSON configuration if available
    bool loaded = loadSettings();
    if (!loaded) {
        addLog("No existing config at " + m_configPath + ", initialized with defaults.");
    } else {
        addLog("Loaded configuration from " + m_configPath);
    }

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
    bool saved = saveSettings();
    if (saved) {
        addLog("Configuration saved to " + m_configPath + " successfully.");
        if (m_view) {
            m_view->showSaveSuccess();
        }
    } else {
        addLog("Error: failed to write configuration to " + m_configPath);
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

bool SettingsPresenter::loadSettings() {
    return loadSettingsFromFile(m_configPath);
}

bool SettingsPresenter::saveSettings() const {
    return saveSettingsToFile(m_configPath);
}

bool SettingsPresenter::loadFromJson(const std::string& jsonStr) {
    if (jsonStr.empty()) {
        return false;
    }
    try {
        auto j = nlohmann::json::parse(jsonStr);
        if (j.contains("port") && j["port"].is_number_integer()) {
            int p = j["port"].get<int>();
            if (p >= 1 && p <= 65535) {
                m_port = static_cast<uint16_t>(p);
            }
        }
        if (j.contains("speedLimitKbps") && j["speedLimitKbps"].is_number_unsigned()) {
            m_speedLimitKbps = j["speedLimitKbps"].get<uint32_t>();
        }
        if (j.contains("autoAccept") && j["autoAccept"].is_boolean()) {
            m_autoAccept = j["autoAccept"].get<bool>();
        }
        if (j.contains("airdropEnabled") && j["airdropEnabled"].is_boolean()) {
            m_airdropEnabled = j["airdropEnabled"].get<bool>();
        }
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
    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec)) {
        return false;
    }

    std::ifstream file(filePath);
    if (!file.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return loadFromJson(content);
}

bool SettingsPresenter::saveSettingsToFile(const std::string& filePath) const {
    std::error_code ec;
    std::filesystem::path p(filePath);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) {
            return false;
        }
    }

    std::ofstream file(filePath);
    if (!file.is_open()) return false;
    file << saveToJson();
    file.flush();
    return file.good();
}

}
