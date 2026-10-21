#pragma once

#include <memory>
#include <vector>
#include <string>
#include "landrop/ui/IView.hpp"
#include "landrop/peers/NetworkPeer.hpp"
#include "landrop/core/MulticastBeacon.hpp"

namespace landrop::ui {

class DiscoveryPresenter {
public:
    explicit DiscoveryPresenter(IDiscoveryView* view);
    ~DiscoveryPresenter() = default;

    void setView(IDiscoveryView* view);
    void initialize();

    void onRefreshDevicesClicked();
    void onSelectDeviceClicked(const std::string& peerId);
    void onNfcPairTriggered(const std::string& nfcPayload);
    void onToggleScanClicked(bool enable);

    void registerPeer(const std::shared_ptr<NetworkPeer>& peer);
    std::shared_ptr<NetworkPeer> getSelectedPeer() const;
    const std::vector<std::shared_ptr<NetworkPeer>>& getPeers() const;

private:
    IDiscoveryView* m_view{nullptr};
    std::vector<std::shared_ptr<NetworkPeer>> m_peers;
    std::shared_ptr<NetworkPeer> m_selectedPeer;
    MulticastBeacon m_beacon;
    bool m_isScanning{false};

    void refreshView();
};

}
