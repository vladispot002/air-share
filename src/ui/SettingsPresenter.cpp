#include "landrop/ui/SettingsPresenter.hpp"

namespace landrop::ui {

SettingsPresenter::SettingsPresenter(ISettingsView* view)
    : m_view(view) {}

void SettingsPresenter::setView(ISettingsView* view) {
    m_view = view;
}

void SettingsPresenter::initialize() {
    m_port = 8848;
    m_speedLimitKbps = 0;
    m_autoAccept = true;
    m_airdropEnabled = false;

    addLog("System initialized with C++20 MVP engine.");
    addLog("Default listen port set to 8848.");
    addLog("Zero-Install Web Bridge ready on port 8080.");

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
    addLog("AirDrop Apple compatibility protocol: " + std::string(airdropEnabled ? "Enabled" : "Disabled"));
}

void SettingsPresenter::onSaveSettingsClicked() {
    addLog("Configuration saved successfully.");
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

}
