#include "landrop/ui/DiscoveryPresenter.hpp"
#include "landrop/peers/LocalPeer.hpp"
#include "landrop/peers/CloudPeer.hpp"

namespace landrop::ui {

DiscoveryPresenter::DiscoveryPresenter(IDiscoveryView* view)
    : m_view(view) {}

void DiscoveryPresenter::setView(IDiscoveryView* view) {
    m_view = view;
}

void DiscoveryPresenter::initialize() {
    m_beacon.start();
    m_isScanning = true;

    m_peers.push_back(std::make_shared<LocalPeer>("peer-local-01", "Desktop-Workstation", "192.168.1.100", 8848, "Linux", "PC", "255.255.255.0", false));
    m_peers.push_back(std::make_shared<LocalPeer>("peer-local-02", "Mobile-Android-NFC", "192.168.1.105", 8848, "Android", "Phone", "255.255.255.0", true));
    m_peers.push_back(std::make_shared<CloudPeer>("peer-cloud-01", "DropCloud-Relay", "https://relay.landrop.net", 443));

    for (const auto& peer : m_peers) {
        peer->initialize();
    }

    refreshView();
}

void DiscoveryPresenter::onRefreshDevicesClicked() {
    m_beacon.broadcastAnnouncement();
    refreshView();
    if (m_view) {
        m_view->showNotification("Devices refreshed");
    }
}

void DiscoveryPresenter::onSelectDeviceClicked(const std::string& peerId) {
    for (const auto& peer : m_peers) {
        if (peer->getId() == peerId) {
            m_selectedPeer = peer;
            if (m_view) {
                m_view->showNotification("Selected device: " + peer->getDisplayName());
            }
            return;
        }
    }
}

void DiscoveryPresenter::onNfcPairTriggered(const std::string& nfcPayload) {
    std::string name, uuid;
    uint16_t port = 0;
    if (MulticastBeacon::parsePayload(nfcPayload, name, uuid, port)) {
        auto paired = std::make_shared<LocalPeer>(uuid, name, "192.168.1.150", port, "Android/iOS", "NFC Paired Device", "255.255.255.0", true);
        paired->initialize();
        m_peers.push_back(paired);
        m_selectedPeer = paired;
        refreshView();
        if (m_view) {
            m_view->showNfcPairResult(true, "NFC Handshake successful: " + name);
        }
    } else {
        if (m_view) {
            m_view->showNfcPairResult(false, "Invalid NFC payload format");
        }
    }
}

void DiscoveryPresenter::onToggleScanClicked(bool enable) {
    m_isScanning = enable;
    if (m_view) {
        m_view->setScanningStatus(m_isScanning);
        m_view->showNotification(m_isScanning ? "Scanning started" : "Scanning paused");
    }
}

void DiscoveryPresenter::registerPeer(const std::shared_ptr<NetworkPeer>& peer) {
    m_peers.push_back(peer);
    refreshView();
}

std::shared_ptr<NetworkPeer> DiscoveryPresenter::getSelectedPeer() const {
    return m_selectedPeer;
}

const std::vector<std::shared_ptr<NetworkPeer>>& DiscoveryPresenter::getPeers() const {
    return m_peers;
}

void DiscoveryPresenter::refreshView() {
    if (!m_view) {
        return;
    }
    std::vector<DeviceViewModel> viewModels;
    for (const auto& peer : m_peers) {
        DeviceViewModel vm;
        vm.id = peer->getId();
        vm.name = peer->getDisplayName();
        vm.endpoint = peer->getEndpointInfo();
        vm.type = peer->getPeerType();
        vm.isOnline = peer->isOnline();
        if (auto local = std::dynamic_pointer_cast<LocalPeer>(peer)) {
            vm.signalRssi = local->getSignalStrengthRssi();
        } else {
            vm.signalRssi = -30;
        }
        viewModels.push_back(std::move(vm));
    }
    m_view->updateDeviceList(viewModels);
    m_view->setScanningStatus(m_isScanning);
}

}
