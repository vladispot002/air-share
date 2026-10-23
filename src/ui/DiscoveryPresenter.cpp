#include "landrop/ui/DiscoveryPresenter.hpp"
#include "landrop/peers/LocalPeer.hpp"
#include "landrop/peers/CloudPeer.hpp"
#include "landrop/mobile/NfcPairingSession.hpp"
#include "landrop/utils/SocketCompat.hpp"
#include <QHostInfo>
#include <QNetworkInterface>
#include <QHostAddress>
#include <cstring>
#include <algorithm>

namespace landrop::ui {

DiscoveryPresenter::DiscoveryPresenter(IDiscoveryView* view)
    : m_view(view) {}

void DiscoveryPresenter::setView(IDiscoveryView* view) {
    m_view = view;
}

void DiscoveryPresenter::initialize() {
    m_beacon.setDeviceName(getLocalDeviceName());
    m_beacon.setDiscoveryCallback([this](const std::string& name, const std::string& uuid, const std::string& ip, uint16_t port) {
        onPeerDiscovered(name, uuid, ip, port);
    });
    m_beacon.start();
    m_isScanning = true;

    {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        m_peers.clear();
    }

    m_beacon.broadcastAnnouncement();
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
    std::string displayName;
    {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        for (const auto& peer : m_peers) {
            if (peer->getId() == peerId) {
                m_selectedPeer = peer;
                displayName = peer->getDisplayName();
                break;
            }
        }
    }
    if (!displayName.empty()) {
        refreshView();
        if (m_view) {
            m_view->showNotification("Selected device: " + displayName);
        }
    }
}

void DiscoveryPresenter::onDirectConnectRequested(const std::string& ipOrHost, uint16_t port) {
    if (ipOrHost.empty()) {
        if (m_view) {
            m_view->showNotification("Error: IP address or hostname cannot be empty");
        }
        return;
    }
    if (port == 0) {
        port = 8848;
    }

    std::string targetEndpoint = ipOrHost + ":" + std::to_string(port);
    std::string directPeerId = "peer-direct-" + ipOrHost;
    std::string directPeerIdWithPort = "peer-direct-" + ipOrHost + "-" + std::to_string(port);
    std::shared_ptr<NetworkPeer> existing;
    {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        for (const auto& peer : m_peers) {
            if (peer->getEndpointInfo() == targetEndpoint ||
                peer->getId() == directPeerId ||
                peer->getId() == directPeerIdWithPort) {
                m_selectedPeer = peer;
                existing = peer;
                break;
            }
        }
    }
    if (existing) {
        refreshView();
        if (m_view) {
            m_view->showNotification("Selected existing peer: " + existing->getDisplayName());
        }
        return;
    }

    std::string peerId = directPeerId;
    std::string displayName = "Direct Node (" + ipOrHost + ")";
    auto newPeer = std::make_shared<LocalPeer>(
        peerId,
        displayName,
        ipOrHost,
        port,
        "Direct LAN",
        "Manual Connection",
        "255.255.255.0",
        true
    );
    newPeer->initialize();
    {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        m_peers.push_back(newPeer);
        m_selectedPeer = newPeer;
    }
    refreshView();
    if (m_view) {
        m_view->showNotification("Added direct peer: " + displayName + " [" + ipOrHost + ":" + std::to_string(port) + "]");
    }
}

void DiscoveryPresenter::onNfcPairTriggered(const std::string& nfcPayload) {
    std::string name, uuid, ip = "192.168.1.150";
    uint16_t port = 0;
    bool ok = false;

    mobile::NfcPairingSession session("desktop-local", "LAN-Drop Desktop");
    std::optional<mobile::NfcPairingData> data;
    try {
        data = session.parseNdefPayload(nfcPayload);
    } catch (const std::exception&) {
        data.reset();
    }
    if (data) {
        name = data->deviceName;
        uuid = data->deviceId.empty() ? ("nfc-" + data->ipAddress) : data->deviceId;
        ip = data->ipAddress;
        port = data->port;
        ok = !name.empty() && !ip.empty();
    } else if (MulticastBeacon::parsePayload(nfcPayload, name, uuid, port)) {
        ok = true;
    }

    if (!ok) {
        if (m_view) {
            m_view->showNfcPairResult(false, "Invalid NFC payload format");
        }
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        for (const auto& peer : m_peers) {
            if (peer->getId() == uuid) {
                m_selectedPeer = peer;
                if (m_view) {
                    m_view->showNfcPairResult(true, "Already paired: " + peer->getDisplayName());
                }
                return;
            }
        }

        auto paired = std::make_shared<LocalPeer>(uuid, name, ip, port, "Android/iOS", "NFC Paired Device", "255.255.255.0", true);
        paired->initialize();
        m_peers.push_back(paired);
        m_selectedPeer = paired;
    }
    refreshView();
    if (m_view) {
        m_view->showNfcPairResult(true, "NFC Handshake successful: " + name);
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
    {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        m_peers.push_back(peer);
    }
    refreshView();
}

std::shared_ptr<NetworkPeer> DiscoveryPresenter::getSelectedPeer() const {
    std::lock_guard<std::mutex> lock(m_peersMutex);
    return m_selectedPeer;
}

std::vector<std::shared_ptr<NetworkPeer>> DiscoveryPresenter::getPeers() const {
    std::lock_guard<std::mutex> lock(m_peersMutex);
    return m_peers;
}

void DiscoveryPresenter::refreshView() {
    if (!m_view) {
        return;
    }
    std::vector<DeviceViewModel> viewModels;
    bool isScanning = false;
    {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        isScanning = m_isScanning;
        viewModels.reserve(m_peers.size());
        for (const auto& peer : m_peers) {
            DeviceViewModel vm;
            vm.id = peer->getId();
            vm.name = peer->getDisplayName();
            vm.endpoint = peer->getEndpointInfo();
            vm.isOnline = peer->isOnline();
            if (auto local = std::dynamic_pointer_cast<LocalPeer>(peer)) {
                vm.type = local->getOsName();
                vm.signalRssi = local->getSignalStrengthRssi();
            } else {
                vm.type = peer->getPeerType();
                vm.signalRssi = -30;
            }
            viewModels.push_back(std::move(vm));
        }
    }
    m_view->updateDeviceList(viewModels);
    m_view->setScanningStatus(isScanning);
}

void DiscoveryPresenter::onRemovePeerClicked(const std::string& peerId) {
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        auto it = std::remove_if(m_peers.begin(), m_peers.end(), [&peerId](const std::shared_ptr<NetworkPeer>& p) {
            return p->getId() == peerId;
        });
        if (it != m_peers.end()) {
            if (m_selectedPeer && m_selectedPeer->getId() == peerId) {
                m_selectedPeer.reset();
            }
            m_peers.erase(it, m_peers.end());
            removed = true;
        }
    }
    if (removed) {
        refreshView();
        if (m_view) {
            m_view->showNotification("Device removed from radar");
        }
    }
}

void DiscoveryPresenter::onPeerDiscovered(const std::string& name, const std::string& uuid, const std::string& ip, uint16_t port) {
    std::string targetEndpoint = ip + ":" + std::to_string(port);
    bool isNew = false;
    {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        for (auto& peer : m_peers) {
            if (peer->getId() == uuid || peer->getEndpointInfo() == targetEndpoint) {
                peer->setOnline(true);
                goto finish_check;
            }
        }

        {
            std::string osType = "LAN Node";
            if (name.find("MacBook") != std::string::npos || name.find("Mac") != std::string::npos) osType = "macOS";
            else if (name.find("ThinkPad") != std::string::npos || name.find("Windows") != std::string::npos) osType = "Windows";
            else if (name.find("Linux") != std::string::npos || name.find("Ubuntu") != std::string::npos) osType = "Linux";
            else if (name.find("Android") != std::string::npos || name.find("Galaxy") != std::string::npos) osType = "Android";
            else if (name.find("iPhone") != std::string::npos || name.find("iPad") != std::string::npos) osType = "iOS";

            auto newPeer = std::make_shared<LocalPeer>(
                uuid,
                name,
                ip,
                port,
                osType,
                "Discovered via LAN Broadcast",
                "255.255.255.0",
                false
            );
            newPeer->initialize();
            m_peers.push_back(newPeer);
            isNew = true;
        }
    }

finish_check:
    refreshView();
    if (isNew && m_view) {
        m_view->showNotification("Discovered LAN device: " + name + " (" + ip + ")");
    }
}

std::string DiscoveryPresenter::getLocalDeviceName() const {
    QString name = QHostInfo::localHostName();
    if (!name.isEmpty()) {
        return name.toStdString();
    }
    return "This Device";
}

std::string DiscoveryPresenter::getLocalDeviceIp() const {
    const auto addresses = QNetworkInterface::allAddresses();
    for (const auto& address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()) {
            return address.toString().toStdString();
        }
    }
    return "127.0.0.1";
}

uint16_t DiscoveryPresenter::getLocalDevicePort() const {
    return m_beacon.getListenPort();
}

void DiscoveryPresenter::setLocalDevicePort(uint16_t port) {
    m_beacon.setListenPort(port);
}

}
