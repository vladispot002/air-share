package identity

import (
	"strings"
	"sync"
	"time"
)

// Store defines repository contracts for Identity, Device, Contact, and Transfer persistence.
type Store interface {
	// Users
	CreateUser(u *UserAccount) error
	GetUserByID(userID string) (*UserAccount, error)
	GetUserByLandropID(landropID string) (*UserAccount, error)
	GetUserByEmail(email string) (*UserAccount, error)
	GetUserByAuthIdentity(provider, providerUserID string) (*UserAccount, error)
	UpdateUser(u *UserAccount) error
	DeleteUser(userID string) error
	SearchUsersByLandropID(prefix string, limit int) ([]*UserAccount, error)

	// Auth Identities
	LinkAuthIdentity(userID string, identity AuthIdentity) error
	GetAuthIdentities(userID string) ([]AuthIdentity, error)
	UnlinkAuthIdentity(userID, provider string) error

	// Devices
	RegisterDevice(d *Device) error
	GetDevice(deviceID string) (*Device, error)
	GetDevicesByUserID(userID string) ([]*Device, error)
	UpdateDevice(d *Device) error
	UpdateDevicePresence(deviceID string, presence DevicePresence) error
	RevokeDevice(deviceID string) error

	// Device Pairing Sessions
	CreatePairingSession(session *DevicePairingSession) error
	GetPairingSession(sessionID string) (*DevicePairingSession, error)
	MarkPairingSessionUsed(sessionID string) error

	// Contacts & Relations
	UpsertContact(entry *ContactEntry) error
	GetContacts(userID string) ([]*ContactEntry, error)
	GetContactRelation(userID, targetUserID string) (*ContactEntry, error)
	DeleteContact(userID, targetUserID string) error
	SetFavorite(userID, targetUserID string, isFavorite bool) error

	// Recent Users
	RecordInteraction(entry *RecentUserEntry) error
	GetRecentUsers(userID string, limit int) ([]*RecentUserEntry, error)

	// Transfers & Claims
	CreateTransferRequest(req *TransferRequest) error
	GetTransferRequest(requestID string) (*TransferRequest, error)
	UpdateTransferRequest(req *TransferRequest) error
	ClaimTransferRequest(requestID, deviceID string) (*TransferRequest, error)

	// Metadata History
	RecordHistory(rec *TransferHistoryRecord) error
	GetHistory(userID string, limit int) ([]*TransferHistoryRecord, error)
	DeleteHistoryItem(userID, transferID string) error
	ClearHistory(userID string) error
}

// MemoryStore provides a full in-memory, thread-safe implementation of Store.
type MemoryStore struct {
	mu           sync.RWMutex
	users        map[string]*UserAccount            // userID -> UserAccount
	byLandropID  map[string]string                  // landropID -> userID
	byEmail      map[string]string                  // lower(email) -> userID
	byAuthId     map[string]string                  // "provider:providerUserID" -> userID
	devices      map[string]*Device                 // deviceID -> Device
	pairings     map[string]*DevicePairingSession   // sessionID -> DevicePairingSession
	contacts     map[string]*ContactEntry           // "userID:targetUserID" -> ContactEntry
	recents      map[string]*RecentUserEntry        // "userID:targetUserID" -> RecentUserEntry
	transfers    map[string]*TransferRequest        // requestID -> TransferRequest
	history      map[string][]*TransferHistoryRecord // userID -> records
}

// NewMemoryStore constructs a fresh in-memory storage engine.
func NewMemoryStore() *MemoryStore {
	return &MemoryStore{
		users:       make(map[string]*UserAccount),
		byLandropID: make(map[string]string),
		byEmail:     make(map[string]string),
		byAuthId:    make(map[string]string),
		devices:     make(map[string]*Device),
		pairings:    make(map[string]*DevicePairingSession),
		contacts:    make(map[string]*ContactEntry),
		recents:     make(map[string]*RecentUserEntry),
		transfers:   make(map[string]*TransferRequest),
		history:     make(map[string][]*TransferHistoryRecord),
	}
}

func (m *MemoryStore) CreateUser(u *UserAccount) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	normID := strings.ToLower(u.LandropID)
	if _, exists := m.byLandropID[normID]; exists {
		return ErrLandropIDTaken
	}

	u.LandropID = normID
	if u.UserID == "" {
		u.UserID = GenerateUUID()
	}
	if u.CreatedAt.IsZero() {
		u.CreatedAt = time.Now().UTC()
	}
	if u.Status == "" {
		u.Status = "active"
	}

	copyU := *u
	m.users[u.UserID] = &copyU
	m.byLandropID[normID] = u.UserID
	if u.Email != "" {
		m.byEmail[strings.ToLower(strings.TrimSpace(u.Email))] = u.UserID
	}

	for _, id := range u.AuthIdentities {
		key := id.Provider + ":" + id.ProviderUserID
		m.byAuthId[key] = u.UserID
	}
	return nil
}

func (m *MemoryStore) GetUserByEmail(email string) (*UserAccount, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	norm := strings.ToLower(strings.TrimSpace(email))
	uid, exists := m.byEmail[norm]
	if !exists {
		return nil, ErrUserNotFound
	}
	u := m.users[uid]
	copyU := *u
	return &copyU, nil
}

func (m *MemoryStore) GetUserByID(userID string) (*UserAccount, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	u, exists := m.users[userID]
	if !exists {
		return nil, ErrUserNotFound
	}
	copyU := *u
	return &copyU, nil
}

func (m *MemoryStore) GetUserByLandropID(landropID string) (*UserAccount, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	norm := strings.ToLower(landropID)
	uid, exists := m.byLandropID[norm]
	if !exists {
		return nil, ErrUserNotFound
	}
	u := m.users[uid]
	copyU := *u
	return &copyU, nil
}

func (m *MemoryStore) GetUserByAuthIdentity(provider, providerUserID string) (*UserAccount, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	key := provider + ":" + providerUserID
	uid, exists := m.byAuthId[key]
	if !exists {
		return nil, ErrUserNotFound
	}
	u := m.users[uid]
	copyU := *u
	return &copyU, nil
}

func (m *MemoryStore) UpdateUser(u *UserAccount) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	existing, exists := m.users[u.UserID]
	if !exists {
		return ErrUserNotFound
	}

	normID := strings.ToLower(u.LandropID)
	if normID != existing.LandropID {
		if _, taken := m.byLandropID[normID]; taken {
			return ErrLandropIDTaken
		}
		delete(m.byLandropID, existing.LandropID)
		m.byLandropID[normID] = u.UserID
	}

	copyU := *u
	copyU.LandropID = normID
	if existing.Email != "" {
		delete(m.byEmail, strings.ToLower(strings.TrimSpace(existing.Email)))
	}
	if u.Email != "" {
		m.byEmail[strings.ToLower(strings.TrimSpace(u.Email))] = u.UserID
	}
	m.users[u.UserID] = &copyU
	return nil
}

func (m *MemoryStore) DeleteUser(userID string) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	u, exists := m.users[userID]
	if !exists {
		return ErrUserNotFound
	}

	delete(m.byLandropID, u.LandropID)
	if u.Email != "" {
		delete(m.byEmail, strings.ToLower(strings.TrimSpace(u.Email)))
	}
	for _, id := range u.AuthIdentities {
		delete(m.byAuthId, id.Provider+":"+id.ProviderUserID)
	}
	delete(m.users, userID)

	for devID, dev := range m.devices {
		if dev.UserID == userID {
			delete(m.devices, devID)
		}
	}
	return nil
}

func (m *MemoryStore) SearchUsersByLandropID(prefix string, limit int) ([]*UserAccount, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	normPrefix := strings.ToLower(prefix)
	var results []*UserAccount

	for _, u := range m.users {
		if strings.HasPrefix(u.LandropID, normPrefix) || strings.Contains(strings.ToLower(u.DisplayName), normPrefix) {
			copyU := *u
			copyU.AuthIdentities = nil // privacy: never expose auth identities in search
			results = append(results, &copyU)
			if limit > 0 && len(results) >= limit {
				break
			}
		}
	}
	return results, nil
}

func (m *MemoryStore) LinkAuthIdentity(userID string, identity AuthIdentity) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	u, exists := m.users[userID]
	if !exists {
		return ErrUserNotFound
	}

	key := identity.Provider + ":" + identity.ProviderUserID
	if existingUID, alreadyLinked := m.byAuthId[key]; alreadyLinked && existingUID != userID {
		return ErrUnauthorized
	}

	if identity.LinkedAt.IsZero() {
		identity.LinkedAt = time.Now().UTC()
	}

	for i, existing := range u.AuthIdentities {
		if existing.Provider == identity.Provider && existing.ProviderUserID == identity.ProviderUserID {
			u.AuthIdentities[i] = identity
			m.byAuthId[key] = userID
			return nil
		}
	}

	u.AuthIdentities = append(u.AuthIdentities, identity)
	m.byAuthId[key] = userID
	return nil
}

func (m *MemoryStore) GetAuthIdentities(userID string) ([]AuthIdentity, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	u, exists := m.users[userID]
	if !exists {
		return nil, ErrUserNotFound
	}
	var res []AuthIdentity
	for _, id := range u.AuthIdentities {
		res = append(res, id)
	}
	return res, nil
}

func (m *MemoryStore) UnlinkAuthIdentity(userID, provider string) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	u, exists := m.users[userID]
	if !exists {
		return ErrUserNotFound
	}

	if len(u.AuthIdentities) <= 1 {
		return ErrUnauthorized // Cannot unlink the last remaining identity
	}

	var updated []AuthIdentity
	for _, id := range u.AuthIdentities {
		if id.Provider == provider {
			delete(m.byAuthId, id.Provider+":"+id.ProviderUserID)
		} else {
			updated = append(updated, id)
		}
	}
	u.AuthIdentities = updated
	return nil
}

func (m *MemoryStore) RegisterDevice(d *Device) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	if d.DeviceID == "" {
		d.DeviceID = GenerateUUID()
	}
	if d.RegisteredAt.IsZero() {
		d.RegisteredAt = time.Now().UTC()
	}
	d.LastSeenAt = time.Now().UTC()
	d.Status = DeviceStatusActive
	d.Presence = PresenceOnline

	copyD := *d
	m.devices[d.DeviceID] = &copyD
	return nil
}

func (m *MemoryStore) GetDevice(deviceID string) (*Device, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	d, exists := m.devices[deviceID]
	if !exists {
		return nil, ErrDeviceNotFound
	}
	copyD := *d
	return &copyD, nil
}

func (m *MemoryStore) GetDevicesByUserID(userID string) ([]*Device, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	var list []*Device
	for _, d := range m.devices {
		if d.UserID == userID && d.Status == DeviceStatusActive {
			copyD := *d
			list = append(list, &copyD)
		}
	}
	return list, nil
}

func (m *MemoryStore) UpdateDevice(d *Device) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	existing, exists := m.devices[d.DeviceID]
	if !exists {
		return ErrDeviceNotFound
	}
	existing.DeviceName = d.DeviceName
	existing.DeviceType = d.DeviceType
	existing.Platform = d.Platform
	existing.AppVersion = d.AppVersion
	existing.LastSeenAt = time.Now().UTC()
	return nil
}

func (m *MemoryStore) UpdateDevicePresence(deviceID string, presence DevicePresence) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	d, exists := m.devices[deviceID]
	if !exists {
		return ErrDeviceNotFound
	}
	d.Presence = presence
	d.LastSeenAt = time.Now().UTC()
	return nil
}

func (m *MemoryStore) RevokeDevice(deviceID string) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	d, exists := m.devices[deviceID]
	if !exists {
		return ErrDeviceNotFound
	}
	d.Status = DeviceStatusRevoked
	d.Presence = PresenceOffline
	return nil
}

func (m *MemoryStore) CreatePairingSession(session *DevicePairingSession) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	if session.SessionID == "" {
		session.SessionID = GenerateRandomHex(16)
	}
	if session.Secret == "" {
		session.Secret = GenerateRandomHex(32)
	}
	if session.ExpiresAt.IsZero() {
		session.ExpiresAt = time.Now().UTC().Add(5 * time.Minute)
	}

	copyS := *session
	m.pairings[session.SessionID] = &copyS
	return nil
}

func (m *MemoryStore) GetPairingSession(sessionID string) (*DevicePairingSession, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	s, exists := m.pairings[sessionID]
	if !exists {
		return nil, ErrUnauthorized
	}
	if s.Used || time.Now().UTC().After(s.ExpiresAt) {
		return nil, ErrUnauthorized
	}
	copyS := *s
	return &copyS, nil
}

func (m *MemoryStore) MarkPairingSessionUsed(sessionID string) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	s, exists := m.pairings[sessionID]
	if !exists {
		return ErrUnauthorized
	}
	s.Used = true
	return nil
}

func (m *MemoryStore) UpsertContact(entry *ContactEntry) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	key := entry.UserID + ":" + entry.TargetUserID
	entry.UpdatedAt = time.Now().UTC()
	copyE := *entry
	m.contacts[key] = &copyE
	return nil
}

func (m *MemoryStore) GetContacts(userID string) ([]*ContactEntry, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	var list []*ContactEntry
	for _, c := range m.contacts {
		if c.UserID == userID && c.State != ContactNone {
			copyC := *c
			list = append(list, &copyC)
		}
	}
	return list, nil
}

func (m *MemoryStore) GetContactRelation(userID, targetUserID string) (*ContactEntry, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	key := userID + ":" + targetUserID
	c, exists := m.contacts[key]
	if !exists {
		return &ContactEntry{
			UserID:       userID,
			TargetUserID: targetUserID,
			State:        ContactNone,
		}, nil
	}
	copyC := *c
	return &copyC, nil
}

func (m *MemoryStore) DeleteContact(userID, targetUserID string) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	key1 := userID + ":" + targetUserID
	key2 := targetUserID + ":" + userID
	delete(m.contacts, key1)
	delete(m.contacts, key2)
	return nil
}

func (m *MemoryStore) SetFavorite(userID, targetUserID string, isFavorite bool) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	key := userID + ":" + targetUserID
	c, exists := m.contacts[key]
	if !exists {
		return ErrUserNotFound
	}
	c.IsFavorite = isFavorite
	c.UpdatedAt = time.Now().UTC()
	return nil
}

func (m *MemoryStore) RecordInteraction(entry *RecentUserEntry) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	key := entry.UserID + ":" + entry.TargetUserID
	entry.LastInteraction = time.Now().UTC()
	copyE := *entry
	m.recents[key] = &copyE
	return nil
}

func (m *MemoryStore) GetRecentUsers(userID string, limit int) ([]*RecentUserEntry, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	var list []*RecentUserEntry
	for _, r := range m.recents {
		if r.UserID == userID {
			copyR := *r
			list = append(list, &copyR)
		}
	}

	// Sort descending by LastInteraction
	for i := 0; i < len(list); i++ {
		for j := i + 1; j < len(list); j++ {
			if list[i].LastInteraction.Before(list[j].LastInteraction) {
				list[i], list[j] = list[j], list[i]
			}
		}
	}

	if limit > 0 && len(list) > limit {
		list = list[:limit]
	}
	return list, nil
}

func (m *MemoryStore) CreateTransferRequest(req *TransferRequest) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	if req.RequestID == "" {
		req.RequestID = "XFER-" + GenerateRandomHex(8)
	}
	if req.CreatedAt.IsZero() {
		req.CreatedAt = time.Now().UTC()
	}
	if req.State == "" {
		req.State = TransferStateAwaiting
	}

	copyR := *req
	m.transfers[req.RequestID] = &copyR
	return nil
}

func (m *MemoryStore) GetTransferRequest(requestID string) (*TransferRequest, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	req, exists := m.transfers[requestID]
	if !exists {
		return nil, ErrTransferNotFound
	}
	copyR := *req
	return &copyR, nil
}

func (m *MemoryStore) UpdateTransferRequest(req *TransferRequest) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	_, exists := m.transfers[req.RequestID]
	if !exists {
		return ErrTransferNotFound
	}
	copyR := *req
	m.transfers[req.RequestID] = &copyR
	return nil
}

func (m *MemoryStore) ClaimTransferRequest(requestID, deviceID string) (*TransferRequest, error) {
	m.mu.Lock()
	defer m.mu.Unlock()

	req, exists := m.transfers[requestID]
	if !exists {
		return nil, ErrTransferNotFound
	}
	if time.Now().UTC().After(req.ExpiresAt) {
		req.State = TransferStateExpired
		return nil, ErrTransferExpired
	}
	if req.ClaimedByDeviceID != "" && req.ClaimedByDeviceID != deviceID {
		return nil, ErrTransferAlreadyClaimed
	}

	req.ClaimedByDeviceID = deviceID
	req.State = TransferStateAccepted
	copyR := *req
	return &copyR, nil
}

func (m *MemoryStore) RecordHistory(rec *TransferHistoryRecord) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	copyR := *rec
	m.history[rec.UserID] = append([]*TransferHistoryRecord{&copyR}, m.history[rec.UserID]...)
	return nil
}

func (m *MemoryStore) GetHistory(userID string, limit int) ([]*TransferHistoryRecord, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	records := m.history[userID]
	if limit > 0 && len(records) > limit {
		records = records[:limit]
	}
	var res []*TransferHistoryRecord
	for _, r := range records {
		copyR := *r
		res = append(res, &copyR)
	}
	return res, nil
}

func (m *MemoryStore) DeleteHistoryItem(userID, transferID string) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	records := m.history[userID]
	var updated []*TransferHistoryRecord
	for _, r := range records {
		if r.TransferID != transferID {
			updated = append(updated, r)
		}
	}
	m.history[userID] = updated
	return nil
}

func (m *MemoryStore) ClearHistory(userID string) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	delete(m.history, userID)
	return nil
}
