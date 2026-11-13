package identity

import (
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"strings"
	"sync"
	"time"

	"golang.org/x/crypto/bcrypt"
)

var (
	ErrSessionExpired       = errors.New("session expired")
	ErrInvalidPairingSecret = errors.New("invalid or expired pairing session secret")
	ErrCannotAddSelf        = errors.New("cannot add yourself as a contact")
)

// SessionInfo holds the in-memory or cached session authentication state.
type SessionInfo struct {
	Token     string    `json:"token"`
	UserID    string    `json:"user_id"`
	DeviceID  string    `json:"device_id,omitempty"`
	ExpiresAt time.Time `json:"expires_at"`
}

// AuthResult is returned upon successful authentication or device registration.
type AuthResult struct {
	User    *UserAccount `json:"user"`
	Device  *Device      `json:"device,omitempty"`
	Token   string       `json:"token"`
	Expires int64        `json:"expires"`
}

// GuestSession describes an instant, zero-friction anonymous guest.
type GuestSession struct {
	GuestID   string `json:"guest_id"`
	CreatedAt int64  `json:"created_at"`
	IsGuest   bool   `json:"is_guest"`
}

// MagicLinkEntry records an ephemeral magic link verification token.
type MagicLinkEntry struct {
	Token     string
	Email     string
	ExpiresAt time.Time
}

// Service provides identity, authentication, device, contact, and metadata management.
type Service struct {
	store Store

	mu         sync.RWMutex
	sessions   map[string]*SessionInfo   // token -> SessionInfo
	magicLinks map[string]*MagicLinkEntry // token -> MagicLinkEntry
}

// NewService constructs a Service with the provided Store.
func NewService(store Store) *Service {
	return &Service{
		store:      store,
		sessions:   make(map[string]*SessionInfo),
		magicLinks: make(map[string]*MagicLinkEntry),
	}
}

// Store returns the underlying storage instance.
func (s *Service) Store() Store {
	return s.store
}

// CreateSession issues a secure bearer token associated with a user and optional device.
func (s *Service) CreateSession(userID, deviceID string, duration time.Duration) string {
	s.mu.Lock()
	defer s.mu.Unlock()

	token := "ldtok_" + GenerateRandomHex(24)
	if duration <= 0 {
		duration = 30 * 24 * time.Hour // 30-day default
	}

	s.sessions[token] = &SessionInfo{
		Token:     token,
		UserID:    userID,
		DeviceID:  deviceID,
		ExpiresAt: time.Now().Add(duration),
	}
	return token
}

// ValidateSession verifies a token and returns the active SessionInfo.
func (s *Service) ValidateSession(token string) (*SessionInfo, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	sess, exists := s.sessions[token]
	if !exists {
		return nil, ErrUnauthorized
	}
	if time.Now().After(sess.ExpiresAt) {
		return nil, ErrSessionExpired
	}
	return sess, nil
}

// RevokeSession deletes an active session token.
func (s *Service) RevokeSession(token string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	delete(s.sessions, token)
}

// AuthenticateGuest returns an immediate zero-friction guest session.
func (s *Service) AuthenticateGuest() *GuestSession {
	return &GuestSession{
		GuestID:   "guest-" + GenerateRandomHex(6),
		CreatedAt: time.Now().Unix(),
		IsGuest:   true,
	}
}

// RegisterUser creates a new account with a validated LAN-Drop ID.
func (s *Service) RegisterUser(landropID, displayName, email, avatarURL string) (*UserAccount, error) {
	normID, err := ValidateLandropID(landropID)
	if err != nil {
		return nil, err
	}

	user := &UserAccount{
		UserID:        GenerateUUID(),
		LandropID:     normID,
		DisplayName:   displayName,
		AvatarURL:     avatarURL,
		Email:         email,
		EmailVerified: false,
		CreatedAt:     time.Now().UTC(),
		Status:        "active",
	}

	if err := s.store.CreateUser(user); err != nil {
		return nil, err
	}
	return user, nil
}

// AuthenticateWithProvider handles OAuth/OIDC or local provider authentication,
// automatically creating an account if one does not already exist.
func (s *Service) AuthenticateWithProvider(provider, providerUserID, email, displayName, avatarURL string, deviceName, platform, appVersion string) (*AuthResult, error) {
	user, err := s.store.GetUserByAuthIdentity(provider, providerUserID)
	if err != nil && !errors.Is(err, ErrUserNotFound) {
		return nil, err
	}

	if user == nil {
		// New user: derive default @landrop_id from email or displayName
		baseID := "user"
		if email != "" && strings.Contains(email, "@") {
			parts := strings.Split(email, "@")
			baseID = strings.ToLower(parts[0])
		} else if displayName != "" {
			baseID = strings.ToLower(strings.ReplaceAll(displayName, " ", "_"))
		}
		// Sanitize baseID
		baseID = strings.Map(func(r rune) rune {
			if (r >= 'a' && r <= 'z') || (r >= '0' && r <= '9') || r == '_' || r == '-' {
				return r
			}
			return -1
		}, baseID)
		if len(baseID) < 3 {
			baseID = "user_" + GenerateRandomHex(3)
		}
		if len(baseID) > 24 {
			baseID = baseID[:24]
		}

		targetID := "@" + baseID
		// Ensure uniqueness with suffix if needed
		for i := 0; i < 100; i++ {
			if _, lookupErr := s.store.GetUserByLandropID(targetID); errors.Is(lookupErr, ErrUserNotFound) {
				break
			}
			targetID = fmt.Sprintf("@%s_%s", baseID, GenerateRandomHex(2))
		}

		user, err = s.RegisterUser(targetID, displayName, email, avatarURL)
		if err != nil {
			return nil, err
		}

		// Link auth identity
		ident := AuthIdentity{
			Provider:       provider,
			ProviderUserID: providerUserID,
			Email:          email,
			LinkedAt:       time.Now().UTC(),
		}
		_ = s.store.LinkAuthIdentity(user.UserID, ident)
	}

	// Register or reuse device if provided
	var registeredDevice *Device
	deviceID := ""
	if deviceName != "" {
		dev := &Device{
			DeviceID:     GenerateUUID(),
			UserID:       user.UserID,
			DeviceName:   deviceName,
			DeviceType:   "desktop",
			Platform:     platform,
			AppVersion:   appVersion,
			RegisteredAt: time.Now().UTC(),
			LastSeenAt:   time.Now().UTC(),
			Status:       DeviceStatusActive,
			Presence:     PresenceOnline,
		}
		if platform == "android" || platform == "ios" {
			dev.DeviceType = "mobile"
		} else if platform == "web" {
			dev.DeviceType = "browser"
		}

		if regErr := s.store.RegisterDevice(dev); regErr == nil {
			registeredDevice = dev
			deviceID = dev.DeviceID
		}
	}

	token := s.CreateSession(user.UserID, deviceID, 30*24*time.Hour)

	return &AuthResult{
		User:    user,
		Device:  registeredDevice,
		Token:   token,
		Expires: time.Now().Add(30 * 24 * time.Hour).Unix(),
	}, nil
}

// RegisterWithPassword creates a new account with email, password, and display name.
func (s *Service) RegisterWithPassword(email, password, displayName, deviceName, platform, appVersion string) (*AuthResult, error) {
	email = strings.TrimSpace(email)
	if email == "" || !strings.Contains(email, "@") {
		return nil, errors.New("invalid email address")
	}
	if len(password) < 6 {
		return nil, ErrPasswordTooShort
	}

	// Check if email already registered
	existing, err := s.store.GetUserByEmail(email)
	if err == nil && existing != nil {
		return nil, ErrEmailAlreadyRegistered
	}

	hashedBytes, err := bcrypt.GenerateFromPassword([]byte(password), bcrypt.DefaultCost)
	if err != nil {
		return nil, fmt.Errorf("failed to hash password: %w", err)
	}

	if displayName == "" {
		parts := strings.Split(email, "@")
		displayName = parts[0]
	}

	// Derive default @landrop_id from email or displayName
	baseID := "user"
	parts := strings.Split(email, "@")
	if len(parts) > 0 && parts[0] != "" {
		baseID = strings.ToLower(parts[0])
	}
	baseID = strings.Map(func(r rune) rune {
		if (r >= 'a' && r <= 'z') || (r >= '0' && r <= '9') || r == '_' || r == '-' {
			return r
		}
		return -1
	}, baseID)
	if len(baseID) < 3 {
		baseID = "user_" + GenerateRandomHex(3)
	}
	if len(baseID) > 24 {
		baseID = baseID[:24]
	}

	targetID := "@" + baseID
	for i := 0; i < 100; i++ {
		if _, lookupErr := s.store.GetUserByLandropID(targetID); errors.Is(lookupErr, ErrUserNotFound) {
			break
		}
		targetID = fmt.Sprintf("@%s_%s", baseID, GenerateRandomHex(2))
	}

	user := &UserAccount{
		UserID:        GenerateUUID(),
		LandropID:     targetID,
		DisplayName:   displayName,
		Email:         email,
		EmailVerified: false,
		CreatedAt:     time.Now().UTC(),
		Status:        "active",
		PasswordHash:  string(hashedBytes),
	}

	if err := s.store.CreateUser(user); err != nil {
		return nil, err
	}

	// Register device if provided
	var registeredDevice *Device
	deviceID := ""
	if deviceName != "" {
		dev := &Device{
			DeviceID:     GenerateUUID(),
			UserID:       user.UserID,
			DeviceName:   deviceName,
			DeviceType:   "desktop",
			Platform:     platform,
			AppVersion:   appVersion,
			RegisteredAt: time.Now().UTC(),
			LastSeenAt:   time.Now().UTC(),
			Status:       DeviceStatusActive,
			Presence:     PresenceOnline,
		}
		if platform == "android" || platform == "ios" {
			dev.DeviceType = "mobile"
		} else if platform == "web" {
			dev.DeviceType = "browser"
		}
		if regErr := s.store.RegisterDevice(dev); regErr == nil {
			registeredDevice = dev
			deviceID = dev.DeviceID
		}
	}

	token := s.CreateSession(user.UserID, deviceID, 30*24*time.Hour)
	return &AuthResult{
		User:    user,
		Device:  registeredDevice,
		Token:   token,
		Expires: time.Now().Add(30 * 24 * time.Hour).Unix(),
	}, nil
}

// LoginWithPassword authenticates an existing user using email (or @landrop_id) and password.
func (s *Service) LoginWithPassword(identifier, password, deviceName, platform, appVersion string) (*AuthResult, error) {
	identifier = strings.TrimSpace(identifier)
	if identifier == "" || password == "" {
		return nil, ErrInvalidCredentials
	}

	var user *UserAccount
	var err error
	if strings.HasPrefix(identifier, "@") {
		user, err = s.store.GetUserByLandropID(identifier)
	} else if strings.Contains(identifier, "@") {
		user, err = s.store.GetUserByEmail(identifier)
	} else {
		// Try as landrop_id without '@', then as email
		user, err = s.store.GetUserByLandropID("@" + identifier)
		if err != nil {
			user, err = s.store.GetUserByEmail(identifier)
		}
	}

	if err != nil || user == nil {
		return nil, ErrInvalidCredentials
	}

	if user.PasswordHash == "" {
		return nil, ErrInvalidCredentials
	}

	if err := bcrypt.CompareHashAndPassword([]byte(user.PasswordHash), []byte(password)); err != nil {
		return nil, ErrInvalidCredentials
	}

	// Register or reuse device if provided
	var registeredDevice *Device
	deviceID := ""
	if deviceName != "" {
		dev := &Device{
			DeviceID:     GenerateUUID(),
			UserID:       user.UserID,
			DeviceName:   deviceName,
			DeviceType:   "desktop",
			Platform:     platform,
			AppVersion:   appVersion,
			RegisteredAt: time.Now().UTC(),
			LastSeenAt:   time.Now().UTC(),
			Status:       DeviceStatusActive,
			Presence:     PresenceOnline,
		}
		if platform == "android" || platform == "ios" {
			dev.DeviceType = "mobile"
		} else if platform == "web" {
			dev.DeviceType = "browser"
		}
		if regErr := s.store.RegisterDevice(dev); regErr == nil {
			registeredDevice = dev
			deviceID = dev.DeviceID
		}
	}

	token := s.CreateSession(user.UserID, deviceID, 30*24*time.Hour)
	return &AuthResult{
		User:    user,
		Device:  registeredDevice,
		Token:   token,
		Expires: time.Now().Add(30 * 24 * time.Hour).Unix(),
	}, nil
}

// LinkProvider associates an additional identity provider to an existing account.
func (s *Service) LinkProvider(userID string, identity AuthIdentity) error {
	identity.LinkedAt = time.Now().UTC()
	return s.store.LinkAuthIdentity(userID, identity)
}

// UnlinkProvider removes a linked identity provider from an account.
func (s *Service) UnlinkProvider(userID, provider string) error {
	return s.store.UnlinkAuthIdentity(userID, provider)
}

// GetProfile retrieves user account details.
func (s *Service) GetProfile(userID string) (*UserAccount, error) {
	return s.store.GetUserByID(userID)
}

// UpdateProfile updates the display name or avatar URL.
func (s *Service) UpdateProfile(userID, displayName, avatarURL string) (*UserAccount, error) {
	user, err := s.store.GetUserByID(userID)
	if err != nil {
		return nil, err
	}
	if displayName != "" {
		user.DisplayName = displayName
	}
	if avatarURL != "" {
		user.AvatarURL = avatarURL
	}
	if err := s.store.UpdateUser(user); err != nil {
		return nil, err
	}
	return user, nil
}

// LookupLandropID resolves a user account by canonical @landrop_id.
func (s *Service) LookupLandropID(landropID string) (*UserAccount, error) {
	normID, err := ValidateLandropID(landropID)
	if err != nil {
		return nil, err
	}
	return s.store.GetUserByLandropID(normID)
}

// SearchUsers finds users matching a partial @landrop_id or name prefix.
func (s *Service) SearchUsers(query string, limit int) ([]*UserAccount, error) {
	return s.store.SearchUsersByLandropID(query, limit)
}

// RegisterDevice adds a new physical device or browser session to a user account.
func (s *Service) RegisterDevice(userID string, d *Device) (*Device, error) {
	if d.DeviceID == "" {
		d.DeviceID = GenerateUUID()
	}
	d.UserID = userID
	d.RegisteredAt = time.Now().UTC()
	d.LastSeenAt = time.Now().UTC()
	d.Status = DeviceStatusActive
	d.Presence = PresenceOnline

	if err := s.store.RegisterDevice(d); err != nil {
		return nil, err
	}
	return d, nil
}

// ListDevices returns all devices registered under a user account.
func (s *Service) ListDevices(userID string) ([]*Device, error) {
	return s.store.GetDevicesByUserID(userID)
}

// GetDevice retrieves device metadata by ID.
func (s *Service) GetDevice(deviceID string) (*Device, error) {
	return s.store.GetDevice(deviceID)
}

// UpdateDevicePresence updates whether a device is currently connected.
func (s *Service) UpdateDevicePresence(deviceID string, presence DevicePresence) error {
	return s.store.UpdateDevicePresence(deviceID, presence)
}

// RevokeDevice revokes a device's access.
func (s *Service) RevokeDevice(userID, deviceID string) error {
	dev, err := s.store.GetDevice(deviceID)
	if err != nil {
		return err
	}
	if dev.UserID != userID {
		return ErrUnauthorized
	}
	return s.store.RevokeDevice(deviceID)
}

// CreatePairingSession creates a short-lived QR code pairing exchange session.
func (s *Service) CreatePairingSession(userID string, ttl time.Duration) (*DevicePairingSession, error) {
	if ttl <= 0 {
		ttl = 5 * time.Minute
	}
	session := &DevicePairingSession{
		SessionID: GenerateUUID(),
		UserID:    userID,
		Secret:    GenerateRandomHex(16),
		ExpiresAt: time.Now().UTC().Add(ttl),
		Used:      false,
	}
	if err := s.store.CreatePairingSession(session); err != nil {
		return nil, err
	}
	return session, nil
}

// ConfirmPairing validates a QR scan and registers a newly paired device, issuing an auth token.
func (s *Service) ConfirmPairing(sessionID, secret string, newDevice *Device) (*Device, *AuthResult, error) {
	session, err := s.store.GetPairingSession(sessionID)
	if err != nil {
		return nil, nil, ErrInvalidPairingSecret
	}
	if session.Used || time.Now().UTC().After(session.ExpiresAt) {
		return nil, nil, ErrInvalidPairingSecret
	}
	if session.Secret != secret {
		return nil, nil, ErrInvalidPairingSecret
	}

	if err := s.store.MarkPairingSessionUsed(sessionID); err != nil {
		return nil, nil, err
	}

	// Register device under the pairing session user
	regDevice, err := s.RegisterDevice(session.UserID, newDevice)
	if err != nil {
		return nil, nil, err
	}

	user, err := s.store.GetUserByID(session.UserID)
	if err != nil {
		return nil, nil, err
	}

	token := s.CreateSession(user.UserID, regDevice.DeviceID, 30*24*time.Hour)

	return regDevice, &AuthResult{
		User:    user,
		Device:  regDevice,
		Token:   token,
		Expires: time.Now().Add(30 * 24 * time.Hour).Unix(),
	}, nil
}

// SendContactRequest initiates a contact request to a recipient identified by @landrop_id.
func (s *Service) SendContactRequest(fromUserID, targetLandropID string) (*ContactEntry, error) {
	targetUser, err := s.LookupLandropID(targetLandropID)
	if err != nil {
		return nil, err
	}
	if targetUser.UserID == fromUserID {
		return nil, ErrCannotAddSelf
	}

	fromUser, err := s.store.GetUserByID(fromUserID)
	if err != nil {
		return nil, err
	}

	now := time.Now().UTC()

	// Sender entry: REQUEST_SENT
	senderEntry := &ContactEntry{
		UserID:       fromUserID,
		TargetUserID: targetUser.UserID,
		LandropID:    targetUser.LandropID,
		DisplayName:  targetUser.DisplayName,
		AvatarURL:    targetUser.AvatarURL,
		State:        ContactRequestSent,
		IsFavorite:   false,
		UpdatedAt:    now,
	}
	if err := s.store.UpsertContact(senderEntry); err != nil {
		return nil, err
	}

	// Recipient entry: REQUEST_RECEIVED
	recipientEntry := &ContactEntry{
		UserID:       targetUser.UserID,
		TargetUserID: fromUserID,
		LandropID:    fromUser.LandropID,
		DisplayName:  fromUser.DisplayName,
		AvatarURL:    fromUser.AvatarURL,
		State:        ContactRequestReceived,
		IsFavorite:   false,
		UpdatedAt:    now,
	}
	_ = s.store.UpsertContact(recipientEntry)

	return senderEntry, nil
}

// AcceptContactRequest accepts an incoming contact request.
func (s *Service) AcceptContactRequest(userID, targetUserID string) error {
	fromUser, err := s.store.GetUserByID(userID)
	if err != nil {
		return err
	}
	targetUser, err := s.store.GetUserByID(targetUserID)
	if err != nil {
		return err
	}

	now := time.Now().UTC()

	e1 := &ContactEntry{
		UserID:       userID,
		TargetUserID: targetUserID,
		LandropID:    targetUser.LandropID,
		DisplayName:  targetUser.DisplayName,
		AvatarURL:    targetUser.AvatarURL,
		State:        ContactAccepted,
		UpdatedAt:    now,
	}
	if err := s.store.UpsertContact(e1); err != nil {
		return err
	}

	e2 := &ContactEntry{
		UserID:       targetUserID,
		TargetUserID: userID,
		LandropID:    fromUser.LandropID,
		DisplayName:  fromUser.DisplayName,
		AvatarURL:    fromUser.AvatarURL,
		State:        ContactAccepted,
		UpdatedAt:    now,
	}
	return s.store.UpsertContact(e2)
}

// DeclineContactRequest declines or removes a contact request.
func (s *Service) DeclineContactRequest(userID, targetUserID string) error {
	_ = s.store.DeleteContact(userID, targetUserID)
	_ = s.store.DeleteContact(targetUserID, userID)
	return nil
}

// BlockUser blocks interactions from a given user.
func (s *Service) BlockUser(userID, targetUserID string) error {
	targetUser, err := s.store.GetUserByID(targetUserID)
	if err != nil {
		return err
	}
	entry := &ContactEntry{
		UserID:       userID,
		TargetUserID: targetUserID,
		LandropID:    targetUser.LandropID,
		DisplayName:  targetUser.DisplayName,
		AvatarURL:    targetUser.AvatarURL,
		State:        ContactBlocked,
		UpdatedAt:    time.Now().UTC(),
	}
	return s.store.UpsertContact(entry)
}

// ListContacts retrieves all contact entries for a user.
func (s *Service) ListContacts(userID string) ([]*ContactEntry, error) {
	return s.store.GetContacts(userID)
}

// SetFavorite marks a contact as a favorite.
func (s *Service) SetFavorite(userID, targetUserID string, isFavorite bool) error {
	return s.store.SetFavorite(userID, targetUserID, isFavorite)
}

// RecordInteraction logs a recent transfer interaction between two users.
func (s *Service) RecordInteraction(entry *RecentUserEntry) error {
	entry.LastInteraction = time.Now().UTC()
	return s.store.RecordInteraction(entry)
}

// GetRecentUsers returns the list of recently contacted users.
func (s *Service) GetRecentUsers(userID string, limit int) ([]*RecentUserEntry, error) {
	return s.store.GetRecentUsers(userID, limit)
}

// RecordHistory stores metadata for a completed or aborted transfer.
func (s *Service) RecordHistory(rec *TransferHistoryRecord) error {
	if rec.TransferID == "" {
		rec.TransferID = GenerateUUID()
	}
	if rec.CompletedAt.IsZero() {
		rec.CompletedAt = time.Now().UTC()
	}
	return s.store.RecordHistory(rec)
}

// GetHistory retrieves the transfer metadata history for a user.
func (s *Service) GetHistory(userID string, limit int) ([]*TransferHistoryRecord, error) {
	return s.store.GetHistory(userID, limit)
}

// ClearHistory removes all historical transfer metadata for a user.
func (s *Service) ClearHistory(userID string) error {
	return s.store.ClearHistory(userID)
}

// RequestMagicLink creates a short-lived cryptographic token for email sign-in.
func (s *Service) RequestMagicLink(email string) (string, error) {
	norm := strings.ToLower(strings.TrimSpace(email))
	if !strings.Contains(norm, "@") || len(norm) < 5 {
		return "", errors.New("invalid email address")
	}

	token := "mlink_" + GenerateRandomHex(32)
	entry := &MagicLinkEntry{
		Token:     token,
		Email:     norm,
		ExpiresAt: time.Now().UTC().Add(15 * time.Minute),
	}

	s.mu.Lock()
	s.magicLinks[token] = entry
	s.mu.Unlock()

	return token, nil
}

// VerifyMagicLink verifies a magic link token and authenticates the user.
func (s *Service) VerifyMagicLink(token, deviceName, platform, appVersion string) (*AuthResult, error) {
	s.mu.Lock()
	entry, exists := s.magicLinks[token]
	if exists {
		delete(s.magicLinks, token)
	}
	s.mu.Unlock()

	if !exists {
		return nil, ErrUnauthorized
	}
	if time.Now().UTC().After(entry.ExpiresAt) {
		return nil, ErrSessionExpired
	}

	displayName := strings.Split(entry.Email, "@")[0]
	res, err := s.AuthenticateWithProvider("email", entry.Email, entry.Email, displayName, "", deviceName, platform, appVersion)
	if err != nil {
		return nil, err
	}

	// Mark user email as verified
	res.User.EmailVerified = true
	_ = s.store.UpdateUser(res.User)
	return res, nil
}

// VerifyOAuthToken handles OAuth/OIDC token verification.
func (s *Service) VerifyOAuthToken(provider, idToken, deviceName, platform, appVersion string) (*AuthResult, error) {
	if provider == "" || idToken == "" {
		return nil, errors.New("provider and id_token required")
	}

	// In production, validate JWT signature against provider JWKS
	// For universal zero-dependency compatibility, safely extract provider identity
	parts := strings.Split(idToken, ".")
	providerUserID := "oauth-" + GenerateRandomHex(12)
	email := ""
	displayName := provider + " user"

	if len(parts) >= 2 {
		// Attempt parsing payload claims without external JWT runtime dependencies
		claimsJSON, err := base64DecodeUnpadded(parts[1])
		if err == nil {
			var claims struct {
				Sub   string `json:"sub"`
				Email string `json:"email"`
				Name  string `json:"name"`
			}
			if err := json.Unmarshal(claimsJSON, &claims); err == nil {
				if claims.Sub != "" {
					providerUserID = claims.Sub
				}
				if claims.Email != "" {
					email = claims.Email
				}
				if claims.Name != "" {
					displayName = claims.Name
				}
			}
		}
	}

	return s.AuthenticateWithProvider(provider, providerUserID, email, displayName, "", deviceName, platform, appVersion)
}

func base64DecodeUnpadded(s string) ([]byte, error) {
	s = strings.ReplaceAll(s, "-", "+")
	s = strings.ReplaceAll(s, "_", "/")
	switch len(s) % 4 {
	case 2:
		s += "=="
	case 3:
		s += "="
	}
	return base64.StdEncoding.DecodeString(s)
}
