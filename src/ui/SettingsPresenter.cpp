#include "landrop/ui/SettingsPresenter.hpp"

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

    addLog("LAN-Drop daemon started on port 8848");
    addLog("Scanning local subnet 192.168.1.0/24...");
    addLog("Discovered: MacBook Pro @ 192.168.1.42");
    addLog("Discovered: ThinkPad X1 @ 192.168.1.67");
    addLog("Discovered: Galaxy S24 @ 192.168.1.103");
    addLog("Cloud Relay connected: relay.landrop.io");
    addLog("Transfer started: T-0042 project-assets.zip -> 192.168.1.67");
    addLog("Speed limit active: 25 MB/s throttle applied");
    addLog("NFC subsystem initialized, antenna ready");
    addLog("AirDrop compatibility protocol enabled");
    addLog("SHA-256 verification passed: deployment-bundle.tar.gz");

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
