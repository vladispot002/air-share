#include "landrop/ui/AccountPresenter.hpp"

namespace landrop {

AccountPresenter::AccountPresenter(std::shared_ptr<IdentityClient> client)
    : m_client(client ? client : std::make_shared<IdentityClient>())
{
}

bool AccountPresenter::isGuest() const {
    return m_client->isGuest();
}

std::string AccountPresenter::getGuestId() const {
    return m_client->getGuestId();
}

UserProfile AccountPresenter::getCurrentUser() const {
    return m_client->getCurrentUser();
}

std::vector<DeviceInfo> AccountPresenter::getDevices() const {
    return m_cachedDevices;
}

std::vector<ContactInfo> AccountPresenter::getContacts() const {
    return m_cachedContacts;
}

std::vector<RecentUserInfo> AccountPresenter::getRecentUsers() const {
    return m_cachedRecents;
}

std::vector<TransferHistoryItem> AccountPresenter::getHistory() const {
    return m_cachedHistory;
}

void AccountPresenter::setContacts(const std::vector<ContactInfo>& contacts) {
    m_cachedContacts = contacts;
    notifyStateChanged();
}

void AccountPresenter::setDevices(const std::vector<DeviceInfo>& devices) {
    m_cachedDevices = devices;
    notifyStateChanged();
}

void AccountPresenter::setHistory(const std::vector<TransferHistoryItem>& history) {
    m_cachedHistory = history;
    notifyStateChanged();
}

bool AccountPresenter::loginWithEmail(const std::string& email, const std::string& displayName, std::string* outError) {
    return loginWithProvider("email", email, email, displayName, outError);
}

bool AccountPresenter::registerAccount(const std::string& email, const std::string& password, const std::string& displayName, std::string* outError) {
    if (!m_client) return false;
    bool ok = m_client->registerWithPassword(email, password, displayName, "LAN-Drop Desktop", "linux", outError);
    if (ok) {
        refreshDevices();
        refreshContacts();
        refreshHistory();
        notifyStateChanged();
    }
    return ok;
}

bool AccountPresenter::loginWithPassword(const std::string& email, const std::string& password, std::string* outError) {
    if (!m_client) return false;
    bool ok = m_client->loginWithPassword(email, password, "LAN-Drop Desktop", "linux", outError);
    if (ok) {
        refreshDevices();
        refreshContacts();
        refreshHistory();
        notifyStateChanged();
    }
    return ok;
}

bool AccountPresenter::loginWithProvider(
    const std::string& provider,
    const std::string& providerUserId,
    const std::string& email,
    const std::string& displayName,
    std::string* outError
) {
    bool ok = m_client->authenticateWithProvider(
        provider,
        providerUserId,
        email,
        displayName,
        "LAN-Drop Desktop",
        "linux",
        outError
    );
    if (ok) {
        refreshDevices();
        refreshContacts();
        refreshHistory();
        notifyStateChanged();
    }
    return ok;
}

bool AccountPresenter::requestMagicLink(const std::string& email, std::string* outToken, std::string* outError) {
    return m_client->requestMagicLink(email, outToken, outError);
}

bool AccountPresenter::verifyMagicLink(const std::string& token, std::string* outError) {
    bool ok = m_client->verifyMagicLink(token, "LAN-Drop Desktop", "linux", outError);
    if (ok) {
        refreshDevices();
        refreshContacts();
        refreshHistory();
        notifyStateChanged();
    }
    return ok;
}

void AccountPresenter::logout() {
    m_client->logout();
    m_cachedDevices.clear();
    m_cachedContacts.clear();
    m_cachedRecents.clear();
    m_cachedHistory.clear();
    notifyStateChanged();
}

bool AccountPresenter::refreshContacts(std::string* outError) {
    if (m_client->isGuest()) {
        m_cachedContacts.clear();
        m_cachedRecents.clear();
        return true;
    }
    std::vector<ContactInfo> contacts;
    if (m_client->listContacts(contacts, outError)) {
        m_cachedContacts = contacts;
        std::vector<RecentUserInfo> recents;
        if (m_client->getRecentUsers(recents)) {
            m_cachedRecents = recents;
        }
        notifyStateChanged();
        return true;
    }
    return false;
}

bool AccountPresenter::addContact(const std::string& targetLandropId, std::string* outError) {
    ContactInfo entry;
    if (m_client->sendContactRequest(targetLandropId, entry, outError)) {
        refreshContacts();
        return true;
    }
    return false;
}

bool AccountPresenter::acceptContact(const std::string& targetUserId, std::string* outError) {
    if (m_client->acceptContactRequest(targetUserId, outError)) {
        refreshContacts();
        return true;
    }
    return false;
}

bool AccountPresenter::declineContact(const std::string& targetUserId, std::string* outError) {
    if (m_client->declineContactRequest(targetUserId, outError)) {
        refreshContacts();
        return true;
    }
    return false;
}

bool AccountPresenter::toggleFavorite(const std::string& targetUserId, bool isFavorite, std::string* outError) {
    if (m_client->setFavorite(targetUserId, isFavorite, outError)) {
        refreshContacts();
        return true;
    }
    return false;
}

bool AccountPresenter::refreshDevices(std::string* outError) {
    if (m_client->isGuest()) {
        m_cachedDevices.clear();
        return true;
    }
    std::vector<DeviceInfo> devices;
    if (m_client->listDevices(devices, outError)) {
        m_cachedDevices = devices;
        notifyStateChanged();
        return true;
    }
    return false;
}

bool AccountPresenter::revokeDevice(const std::string& deviceId, std::string* outError) {
    if (m_client->revokeDevice(deviceId, outError)) {
        refreshDevices();
        return true;
    }
    return false;
}

bool AccountPresenter::generatePairingSession(DevicePairingInfo& outSession, std::string* outError) {
    return m_client->createPairingSession(outSession, outError);
}

bool AccountPresenter::refreshHistory(int limit, std::string* outError) {
    if (m_client->isGuest()) {
        m_cachedHistory.clear();
        return true;
    }
    std::vector<TransferHistoryItem> history;
    if (m_client->getHistory(history, limit, outError)) {
        m_cachedHistory = history;
        notifyStateChanged();
        return true;
    }
    return false;
}

bool AccountPresenter::recordTransfer(const TransferHistoryItem& item, std::string* outError) {
    if (m_client->isGuest()) return false;
    bool ok = m_client->recordHistory(item, outError);
    if (ok) {
        refreshHistory();
    }
    return ok;
}

bool AccountPresenter::clearHistory(std::string* outError) {
    if (m_client->isGuest()) {
        m_cachedHistory.clear();
        return true;
    }
    bool ok = m_client->clearHistory(outError);
    if (ok) {
        m_cachedHistory.clear();
        notifyStateChanged();
    }
    return ok;
}

void AccountPresenter::setOnStateChanged(std::function<void()> callback) {
    m_onStateChanged = callback;
}

void AccountPresenter::notifyStateChanged() {
    if (m_onStateChanged) {
        m_onStateChanged();
    }
}

} // namespace landrop
