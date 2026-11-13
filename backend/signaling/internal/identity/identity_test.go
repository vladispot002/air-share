package identity

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

// MockBroadcaster captures dispatched events for test assertions.
type MockBroadcaster struct {
	UserEvents   map[string][]*EventMessage
	DeviceEvents map[string][]*EventMessage
}

func NewMockBroadcaster() *MockBroadcaster {
	return &MockBroadcaster{
		UserEvents:   make(map[string][]*EventMessage),
		DeviceEvents: make(map[string][]*EventMessage),
	}
}

func (m *MockBroadcaster) BroadcastToUser(userID string, event *EventMessage) {
	m.UserEvents[userID] = append(m.UserEvents[userID], event)
}

func (m *MockBroadcaster) BroadcastToDevice(deviceID string, event *EventMessage) {
	m.DeviceEvents[deviceID] = append(m.DeviceEvents[deviceID], event)
}

func TestLandropIDValidation(t *testing.T) {
	validIDs := []struct {
		input    string
		expected string
	}{
		{"@alice", "@alice"},
		{"alice", "@alice"},
		{"@Bob_123", "@bob_123"},
		{"USER-XYZ", "@user-xyz"},
		{"@developer_99", "@developer_99"},
	}

	for _, tc := range validIDs {
		norm, err := ValidateLandropID(tc.input)
		if err != nil {
			t.Fatalf("expected valid for %s, got error: %v", tc.input, err)
		}
		if norm != tc.expected {
			t.Errorf("expected %s, got %s", tc.expected, norm)
		}
	}

	invalidIDs := []string{
		"",
		"@",
		"ab",       // too short
		"@a",       // too short
		"@toolong_username_that_exceeds_thirty_characters_limit",
		"@invalid.char",
		"@user with space",
		"@hello!world",
	}

	for _, inv := range invalidIDs {
		_, err := ValidateLandropID(inv)
		if err == nil {
			t.Errorf("expected error for invalid ID %q, got nil", inv)
		}
	}
}

func TestUserServiceAndAuthIdentities(t *testing.T) {
	store := NewMemoryStore()
	svc := NewService(store)

	// 1. Guest session
	guest := svc.AuthenticateGuest()
	if !guest.IsGuest || guest.GuestID == "" {
		t.Fatalf("invalid guest session: %+v", guest)
	}

	// 2. Register user
	user1, err := svc.RegisterUser("@alice", "Alice Wonderland", "alice@example.com", "https://img/alice.png")
	if err != nil {
		t.Fatalf("failed to register user: %v", err)
	}
	if user1.LandropID != "@alice" {
		t.Errorf("unexpected landrop_id: %s", user1.LandropID)
	}

	// 3. Prevent duplicate registration
	_, err = svc.RegisterUser("@alice", "Alice Dupe", "alice2@example.com", "")
	if err != ErrLandropIDTaken {
		t.Fatalf("expected ErrLandropIDTaken, got: %v", err)
	}

	// 4. Authenticate with provider (creates user if new, or finds existing)
	authRes, err := svc.AuthenticateWithProvider("google", "goog-12345", "bob@gmail.com", "Bob Builder", "", "Bob-Laptop", "linux", "1.0.0")
	if err != nil {
		t.Fatalf("failed provider auth: %v", err)
	}
	if authRes.Token == "" || authRes.User == nil || authRes.Device == nil {
		t.Fatalf("invalid auth result: %+v", authRes)
	}

	// Verify token validation
	sess, err := svc.ValidateSession(authRes.Token)
	if err != nil {
		t.Fatalf("failed to validate session: %v", err)
	}
	if sess.UserID != authRes.User.UserID {
		t.Errorf("expected session user %s, got %s", authRes.User.UserID, sess.UserID)
	}

	// 5. Link additional provider (e.g. GitHub to Bob)
	err = svc.LinkProvider(authRes.User.UserID, AuthIdentity{
		Provider:       "github",
		ProviderUserID: "gh-9988",
		Email:          "bob@github.com",
	})
	if err != nil {
		t.Fatalf("failed to link provider: %v", err)
	}

	// Lookup by linked provider
	linkedUser, err := store.GetUserByAuthIdentity("github", "gh-9988")
	if err != nil || linkedUser.UserID != authRes.User.UserID {
		t.Fatalf("expected linked user lookup to succeed, got user: %+v, err: %v", linkedUser, err)
	}

	// 6. Update profile
	upUser, err := svc.UpdateProfile(user1.UserID, "Alice in Chains", "https://img/alice2.png")
	if err != nil || upUser.DisplayName != "Alice in Chains" {
		t.Fatalf("failed profile update: %v", err)
	}

	// 7. Search users
	matches, err := svc.SearchUsers("ali", 10)
	if err != nil || len(matches) == 0 {
		t.Fatalf("expected search match for 'ali', got %d matches, err: %v", len(matches), err)
	}
}

func TestDeviceManagementAndPairing(t *testing.T) {
	store := NewMemoryStore()
	svc := NewService(store)

	user, err := svc.RegisterUser("@charlie", "Charlie Brown", "charlie@peanuts.com", "")
	if err != nil {
		t.Fatalf("failed to create user: %v", err)
	}

	// 1. Register Device
	dev1, err := svc.RegisterDevice(user.UserID, &Device{
		DeviceName: "Charlie-Desktop",
		Platform:   "windows",
		AppVersion: "2.1.0",
	})
	if err != nil {
		t.Fatalf("failed to register device: %v", err)
	}
	if dev1.Presence != PresenceOnline {
		t.Errorf("expected online presence, got: %s", dev1.Presence)
	}

	// 2. List devices
	devs, err := svc.ListDevices(user.UserID)
	if err != nil || len(devs) != 1 {
		t.Fatalf("expected 1 device, got %d", len(devs))
	}

	// 3. QR Pairing Session
	session, err := svc.CreatePairingSession(user.UserID, 1*time.Minute)
	if err != nil {
		t.Fatalf("failed to create pairing session: %v", err)
	}

	// Attempt confirm with invalid secret
	_, _, err = svc.ConfirmPairing(session.SessionID, "wrong-secret", &Device{DeviceName: "Phone"})
	if err != ErrInvalidPairingSecret {
		t.Fatalf("expected ErrInvalidPairingSecret, got: %v", err)
	}

	// Confirm with correct secret
	pairedDev, authRes, err := svc.ConfirmPairing(session.SessionID, session.Secret, &Device{
		DeviceName: "Charlie-iPhone",
		DeviceType: "mobile",
		Platform:   "ios",
	})
	if err != nil {
		t.Fatalf("failed pairing confirmation: %v", err)
	}
	if pairedDev.DeviceID == "" || authRes.Token == "" {
		t.Fatalf("invalid paired device / token: %+v, %+v", pairedDev, authRes)
	}

	// Session should now be marked used
	_, _, err = svc.ConfirmPairing(session.SessionID, session.Secret, &Device{DeviceName: "Re-used"})
	if err != ErrInvalidPairingSecret {
		t.Fatalf("expected ErrInvalidPairingSecret on re-use, got: %v", err)
	}

	// 4. Update presence & Revoke
	_ = svc.UpdateDevicePresence(dev1.DeviceID, PresenceOffline)
	d1Updated, _ := svc.GetDevice(dev1.DeviceID)
	if d1Updated.Presence != PresenceOffline {
		t.Errorf("expected offline, got: %s", d1Updated.Presence)
	}

	err = svc.RevokeDevice(user.UserID, dev1.DeviceID)
	if err != nil {
		t.Fatalf("failed to revoke device: %v", err)
	}
	d1Revoked, _ := svc.GetDevice(dev1.DeviceID)
	if d1Revoked.Status != DeviceStatusRevoked {
		t.Errorf("expected revoked status, got %s", d1Revoked.Status)
	}
}

func TestContactWorkflow(t *testing.T) {
	store := NewMemoryStore()
	svc := NewService(store)

	u1, _ := svc.RegisterUser("@alice", "Alice", "alice@example.com", "")
	u2, _ := svc.RegisterUser("@bob", "Bob", "bob@example.com", "")

	// 1. Send contact request
	entry, err := svc.SendContactRequest(u1.UserID, "@bob")
	if err != nil {
		t.Fatalf("failed to send contact request: %v", err)
	}
	if entry.State != ContactRequestSent {
		t.Errorf("expected REQUEST_SENT, got %s", entry.State)
	}

	// Check Bob's side
	bobContacts, _ := svc.ListContacts(u2.UserID)
	if len(bobContacts) != 1 || bobContacts[0].State != ContactRequestReceived {
		t.Fatalf("expected REQUEST_RECEIVED for bob, got: %+v", bobContacts)
	}

	// 2. Accept contact request
	err = svc.AcceptContactRequest(u2.UserID, u1.UserID)
	if err != nil {
		t.Fatalf("failed to accept request: %v", err)
	}

	aliceContacts, _ := svc.ListContacts(u1.UserID)
	if len(aliceContacts) != 1 || aliceContacts[0].State != ContactAccepted {
		t.Fatalf("expected ACCEPTED for alice, got: %+v", aliceContacts)
	}

	// 3. Favorite
	_ = svc.SetFavorite(u1.UserID, u2.UserID, true)
	aliceContacts, _ = svc.ListContacts(u1.UserID)
	if !aliceContacts[0].IsFavorite {
		t.Errorf("expected favorite=true")
	}

	// 4. Block
	_ = svc.BlockUser(u1.UserID, u2.UserID)
	rel, _ := store.GetContactRelation(u1.UserID, u2.UserID)
	if rel.State != ContactBlocked {
		t.Errorf("expected BLOCKED, got %s", rel.State)
	}
}

func TestTransferCoordinationModeAandModeB(t *testing.T) {
	store := NewMemoryStore()
	svc := NewService(store)
	broadcaster := NewMockBroadcaster()
	coord := NewCoordinator(svc, broadcaster)

	sender, _ := svc.RegisterUser("@sender", "Sender", "sender@example.com", "")
	recipient, _ := svc.RegisterUser("@recipient", "Recipient", "recip@example.com", "")

	// Recipient has 2 active devices
	dev1, _ := svc.RegisterDevice(recipient.UserID, &Device{DeviceName: "Recip-Laptop", Platform: "linux"})
	dev2, _ := svc.RegisterDevice(recipient.UserID, &Device{DeviceName: "Recip-Phone", Platform: "android"})

	// --- TEST MODE B: Any Available Device (Atomic Claim) ---
	reqB := &TransferRequest{
		SenderUserID:    sender.UserID,
		SenderLandropID: sender.LandropID,
		RecipientUserID: recipient.UserID,
		DeliveryMode:    DeliveryModeAnyAvailableDevice,
		Files: []TransferFileItem{
			{FileName: "doc.pdf", FileSize: 1024 * 1024, Sha256Hex: "abcdef123456"},
		},
	}

	offeredB, err := coord.OfferTransfer(reqB)
	if err != nil {
		t.Fatalf("failed to offer transfer in Mode B: %v", err)
	}
	if offeredB.State != TransferStateAwaiting || offeredB.RoomCode == "" {
		t.Fatalf("invalid offered transfer: %+v", offeredB)
	}

	// Verify broadcast event sent to recipient user
	userEvents := broadcaster.UserEvents[recipient.UserID]
	if len(userEvents) == 0 || userEvents[len(userEvents)-1].Type != EventTransferRequest {
		t.Fatalf("expected EventTransferRequest dispatched to user %s", recipient.UserID)
	}

	// Device 1 claims the transfer
	claimed, err := coord.ClaimTransfer(offeredB.RequestID, dev1.DeviceID, recipient.UserID)
	if err != nil {
		t.Fatalf("expected device 1 claim to succeed, got: %v", err)
	}
	if claimed.ClaimedByDeviceID != dev1.DeviceID || claimed.State != TransferStateAccepted {
		t.Errorf("unexpected claimed state: %+v", claimed)
	}

	// Device 2 attempts to claim same transfer -> MUST FAIL with ErrTransferAlreadyClaimed
	_, err = coord.ClaimTransfer(offeredB.RequestID, dev2.DeviceID, recipient.UserID)
	if err != ErrTransferAlreadyClaimed {
		t.Fatalf("expected ErrTransferAlreadyClaimed for second device, got: %v", err)
	}

	// Complete transfer
	_, err = coord.CompleteTransfer(offeredB.RequestID, TransferStateCompleted, "WebRtcDataChannel")
	if err != nil {
		t.Fatalf("failed to complete transfer: %v", err)
	}

	// Verify metadata history was recorded
	recipHist, _ := svc.GetHistory(recipient.UserID, 10)
	if len(recipHist) != 1 || recipHist[0].Status != TransferStateCompleted {
		t.Fatalf("expected 1 completed history record for recipient, got: %+v", recipHist)
	}
	senderHist, _ := svc.GetHistory(sender.UserID, 10)
	if len(senderHist) != 1 || senderHist[0].Status != TransferStateCompleted {
		t.Fatalf("expected 1 completed history record for sender, got: %+v", senderHist)
	}

	// --- TEST MODE A: Specific Device ---
	reqA := &TransferRequest{
		SenderUserID:    sender.UserID,
		RecipientUserID: recipient.UserID,
		TargetDeviceID:  dev2.DeviceID,
		DeliveryMode:    DeliveryModeSpecificDevice,
		Files: []TransferFileItem{
			{FileName: "photo.jpg", FileSize: 5000},
		},
	}
	offeredA, err := coord.OfferTransfer(reqA)
	if err != nil {
		t.Fatalf("failed to offer transfer in Mode A: %v", err)
	}
	if len(broadcaster.DeviceEvents[dev2.DeviceID]) == 0 {
		t.Fatalf("expected transfer request event sent directly to device %s in Mode A", dev2.DeviceID)
	}

	// Decline transfer in Mode A
	declined, err := coord.DeclineTransfer(offeredA.RequestID, dev2.DeviceID)
	if err != nil || declined.State != TransferStateRejected {
		t.Fatalf("failed to decline transfer: %v", err)
	}
}

func TestRESTHandlersIntegration(t *testing.T) {
	store := NewMemoryStore()
	svc := NewService(store)
	coord := NewCoordinator(svc, nil)
	handlers := NewHandlers(svc, coord)

	mux := http.NewServeMux()
	handlers.RegisterRoutes(mux)

	// 1. POST /api/v1/auth/guest
	req := httptest.NewRequest(http.MethodPost, "/api/v1/auth/guest", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)
	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200 OK from guest auth, got: %d", rec.Code)
	}
	var guestResp GuestSession
	_ = json.NewDecoder(rec.Body).Decode(&guestResp)
	if !guestResp.IsGuest {
		t.Errorf("expected is_guest=true")
	}

	// 2. POST /api/v1/auth/login
	loginPayload := map[string]string{
		"provider":         "email",
		"provider_user_id": "test@landrop.app",
		"email":            "test@landrop.app",
		"display_name":     "Tester",
		"device_name":      "MacBook Pro",
		"platform":         "macos",
	}
	bodyBytes, _ := json.Marshal(loginPayload)
	req = httptest.NewRequest(http.MethodPost, "/api/v1/auth/login", bytes.NewReader(bodyBytes))
	rec = httptest.NewRecorder()
	mux.ServeHTTP(rec, req)
	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200 OK from login, got %d: %s", rec.Code, rec.Body.String())
	}
	var authRes AuthResult
	_ = json.NewDecoder(rec.Body).Decode(&authRes)
	token := authRes.Token
	if token == "" {
		t.Fatalf("missing auth token in login response")
	}

	// 3. GET /api/v1/users/me
	req = httptest.NewRequest(http.MethodGet, "/api/v1/users/me", nil)
	req.Header.Set("Authorization", "Bearer "+token)
	rec = httptest.NewRecorder()
	mux.ServeHTTP(rec, req)
	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200 OK for /api/v1/users/me, got: %d", rec.Code)
	}

	// 4. GET /api/v1/devices
	req = httptest.NewRequest(http.MethodGet, "/api/v1/devices", nil)
	req.Header.Set("Authorization", "Bearer "+token)
	rec = httptest.NewRecorder()
	mux.ServeHTTP(rec, req)
	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200 OK for /api/v1/devices, got: %d", rec.Code)
	}

	// 5. POST /api/v1/devices/pairing
	req = httptest.NewRequest(http.MethodPost, "/api/v1/devices/pairing", nil)
	req.Header.Set("Authorization", "Bearer "+token)
	rec = httptest.NewRecorder()
	mux.ServeHTTP(rec, req)
	if rec.Code != http.StatusCreated {
		t.Fatalf("expected 201 Created for pairing session, got: %d", rec.Code)
	}
	var pairSession DevicePairingSession
	_ = json.NewDecoder(rec.Body).Decode(&pairSession)

	// 6. POST /api/v1/devices/pairing/confirm
	confirmPayload := map[string]string{
		"session_id":  pairSession.SessionID,
		"secret":      pairSession.Secret,
		"device_name": "Test iPad",
		"device_type": "tablet",
		"platform":    "ios",
	}
	bodyBytes, _ = json.Marshal(confirmPayload)
	req = httptest.NewRequest(http.MethodPost, "/api/v1/devices/pairing/confirm", bytes.NewReader(bodyBytes))
	rec = httptest.NewRecorder()
	mux.ServeHTTP(rec, req)
	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200 OK for pairing confirm, got %d: %s", rec.Code, rec.Body.String())
	}
}

func TestPasswordRegistrationAndLogin(t *testing.T) {
	store := NewMemoryStore()
	svc := NewService(store)
	coord := NewCoordinator(svc, nil)
	handlers := NewHandlers(svc, coord)

	mux := http.NewServeMux()
	handlers.RegisterRoutes(mux)

	// 1. Register new user
	regPayload := map[string]string{
		"email":            "testuser@example.com",
		"password":         "secret123",
		"confirm_password": "secret123",
		"display_name":     "Test User",
		"device_name":      "My Linux Rig",
		"platform":         "linux",
	}
	bodyBytes, _ := json.Marshal(regPayload)
	req := httptest.NewRequest(http.MethodPost, "/api/v1/auth/register", bytes.NewReader(bodyBytes))
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if rec.Code != http.StatusCreated {
		t.Fatalf("expected 201 Created on register, got %d: %s", rec.Code, rec.Body.String())
	}

	var res AuthResult
	if err := json.NewDecoder(rec.Body).Decode(&res); err != nil {
		t.Fatalf("failed to decode register response: %v", err)
	}
	if res.User == nil || res.User.Email != "testuser@example.com" {
		t.Fatalf("expected user with email, got %+v", res.User)
	}
	if res.Token == "" {
		t.Fatalf("expected non-empty token")
	}

	// 2. Register duplicate email -> Expect 409 Conflict
	req = httptest.NewRequest(http.MethodPost, "/api/v1/auth/register", bytes.NewReader(bodyBytes))
	rec = httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if rec.Code != http.StatusConflict {
		t.Fatalf("expected 409 Conflict on duplicate email, got %d: %s", rec.Code, rec.Body.String())
	}

	// 3. Login with correct password
	loginPayload := map[string]string{
		"email":    "testuser@example.com",
		"password": "secret123",
	}
	bodyBytes, _ = json.Marshal(loginPayload)
	req = httptest.NewRequest(http.MethodPost, "/api/v1/auth/login", bytes.NewReader(bodyBytes))
	rec = httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200 OK on login, got %d: %s", rec.Code, rec.Body.String())
	}
	var loginRes AuthResult
	_ = json.NewDecoder(rec.Body).Decode(&loginRes)
	if loginRes.Token == "" {
		t.Fatalf("expected token on login")
	}

	// 4. Login with wrong password -> Expect 401 Unauthorized
	loginPayload["password"] = "wrongpassword"
	bodyBytes, _ = json.Marshal(loginPayload)
	req = httptest.NewRequest(http.MethodPost, "/api/v1/auth/login", bytes.NewReader(bodyBytes))
	rec = httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if rec.Code != http.StatusUnauthorized {
		t.Fatalf("expected 401 Unauthorized on wrong password, got %d", rec.Code)
	}
}

