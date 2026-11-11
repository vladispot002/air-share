package identity

import (
	"encoding/json"
	"errors"
	"net/http"
	"strconv"
	"strings"
	"time"
)

// Handlers exposes the REST API routes for identity, devices, contacts, and transfers.
type Handlers struct {
	service     *Service
	coordinator *Coordinator
}

// NewHandlers creates a new Handlers instance.
func NewHandlers(service *Service, coordinator *Coordinator) *Handlers {
	return &Handlers{
		service:     service,
		coordinator: coordinator,
	}
}

// RegisterRoutes registers all identity endpoints onto the provided http.ServeMux.
func (h *Handlers) RegisterRoutes(mux *http.ServeMux) {
	// Auth
	mux.HandleFunc("/api/v1/auth/guest", h.handleAuthGuest)
	mux.HandleFunc("/api/v1/auth/login", h.handleAuthLogin)
	mux.HandleFunc("/api/v1/auth/magic-link/request", h.handleMagicLinkRequest)
	mux.HandleFunc("/api/v1/auth/magic-link/verify", h.handleMagicLinkVerify)
	mux.HandleFunc("/api/v1/auth/oauth/verify", h.handleOAuthVerify)

	// Users
	mux.HandleFunc("/api/v1/users/me", h.handleUsersMe)
	mux.HandleFunc("/api/v1/users/lookup", h.handleUsersLookup)
	mux.HandleFunc("/api/v1/users/search", h.handleUsersSearch)

	// Devices
	mux.HandleFunc("/api/v1/devices", h.handleDevices)
	mux.HandleFunc("/api/v1/devices/revoke", h.handleDeviceRevoke)
	mux.HandleFunc("/api/v1/devices/pairing", h.handleDevicePairing)
	mux.HandleFunc("/api/v1/devices/pairing/confirm", h.handleDevicePairingConfirm)

	// Contacts
	mux.HandleFunc("/api/v1/contacts", h.handleContacts)
	mux.HandleFunc("/api/v1/contacts/request", h.handleContactRequest)
	mux.HandleFunc("/api/v1/contacts/accept", h.handleContactAccept)
	mux.HandleFunc("/api/v1/contacts/decline", h.handleContactDecline)
	mux.HandleFunc("/api/v1/contacts/block", h.handleContactBlock)
	mux.HandleFunc("/api/v1/contacts/favorite", h.handleContactFavorite)
	mux.HandleFunc("/api/v1/contacts/recents", h.handleContactRecents)

	// Transfers
	mux.HandleFunc("/api/v1/transfers/requests", h.handleTransferRequests)
	mux.HandleFunc("/api/v1/transfers/requests/claim", h.handleTransferClaim)
	mux.HandleFunc("/api/v1/transfers/requests/decline", h.handleTransferDecline)
	mux.HandleFunc("/api/v1/transfers/requests/complete", h.handleTransferComplete)

	// History
	mux.HandleFunc("/api/v1/history", h.handleHistory)
}

// Helper: jsonResponse
func jsonResponse(w http.ResponseWriter, status int, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	if data != nil {
		_ = json.NewEncoder(w).Encode(data)
	}
}

// Helper: jsonError
func jsonError(w http.ResponseWriter, status int, msg string) {
	jsonResponse(w, status, map[string]string{"error": msg})
}

// Helper: extractToken
func (h *Handlers) extractSession(r *http.Request) (*SessionInfo, error) {
	authHeader := r.Header.Get("Authorization")
	if authHeader == "" {
		return nil, ErrUnauthorized
	}
	parts := strings.SplitN(authHeader, " ", 2)
	if len(parts) != 2 || !strings.EqualFold(parts[0], "bearer") {
		return nil, ErrUnauthorized
	}
	token := strings.TrimSpace(parts[1])
	return h.service.ValidateSession(token)
}

// /api/v1/auth/guest (POST)
func (h *Handlers) handleAuthGuest(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost && r.Method != http.MethodGet {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	sess := h.service.AuthenticateGuest()
	jsonResponse(w, http.StatusOK, sess)
}

// /api/v1/auth/login (POST)
func (h *Handlers) handleAuthLogin(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}

	var req struct {
		Provider       string `json:"provider"`
		ProviderUserID string `json:"provider_user_id"`
		Email          string `json:"email"`
		DisplayName    string `json:"display_name"`
		AvatarURL      string `json:"avatar_url"`
		DeviceName     string `json:"device_name"`
		Platform       string `json:"platform"`
		AppVersion     string `json:"app_version"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		jsonError(w, http.StatusBadRequest, "invalid request body")
		return
	}

	if req.Provider == "" {
		req.Provider = "local"
	}
	if req.ProviderUserID == "" {
		if req.Email != "" {
			req.ProviderUserID = req.Email
		} else {
			req.ProviderUserID = GenerateUUID()
		}
	}

	res, err := h.service.AuthenticateWithProvider(
		req.Provider,
		req.ProviderUserID,
		req.Email,
		req.DisplayName,
		req.AvatarURL,
		req.DeviceName,
		req.Platform,
		req.AppVersion,
	)
	if err != nil {
		jsonError(w, http.StatusInternalServerError, err.Error())
		return
	}

	jsonResponse(w, http.StatusOK, res)
}

// /api/v1/auth/magic-link/request (POST)
func (h *Handlers) handleMagicLinkRequest(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}

	var req struct {
		Email string `json:"email"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || req.Email == "" {
		jsonError(w, http.StatusBadRequest, "valid email required")
		return
	}

	token, err := h.service.RequestMagicLink(req.Email)
	if err != nil {
		jsonError(w, http.StatusBadRequest, err.Error())
		return
	}

	jsonResponse(w, http.StatusOK, map[string]interface{}{
		"status":     "sent",
		"token":      token, // returned for zero-friction client workflows and testing
		"expires_in": 900,
	})
}

// /api/v1/auth/magic-link/verify (POST)
func (h *Handlers) handleMagicLinkVerify(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}

	var req struct {
		Token      string `json:"token"`
		DeviceName string `json:"device_name"`
		Platform   string `json:"platform"`
		AppVersion string `json:"app_version"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || req.Token == "" {
		jsonError(w, http.StatusBadRequest, "token required")
		return
	}

	res, err := h.service.VerifyMagicLink(req.Token, req.DeviceName, req.Platform, req.AppVersion)
	if err != nil {
		if errors.Is(err, ErrSessionExpired) {
			jsonError(w, http.StatusGone, "magic link expired")
			return
		}
		jsonError(w, http.StatusUnauthorized, "invalid or already used magic link")
		return
	}

	jsonResponse(w, http.StatusOK, res)
}

// /api/v1/auth/oauth/verify (POST)
func (h *Handlers) handleOAuthVerify(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}

	var req struct {
		Provider   string `json:"provider"`
		IDToken    string `json:"id_token"`
		DeviceName string `json:"device_name"`
		Platform   string `json:"platform"`
		AppVersion string `json:"app_version"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || req.Provider == "" || req.IDToken == "" {
		jsonError(w, http.StatusBadRequest, "provider and id_token required")
		return
	}

	res, err := h.service.VerifyOAuthToken(req.Provider, req.IDToken, req.DeviceName, req.Platform, req.AppVersion)
	if err != nil {
		jsonError(w, http.StatusUnauthorized, err.Error())
		return
	}

	jsonResponse(w, http.StatusOK, res)
}

// /api/v1/users/me (GET, PATCH)
func (h *Handlers) handleUsersMe(w http.ResponseWriter, r *http.Request) {
	sess, err := h.extractSession(r)
	if err != nil {
		jsonError(w, http.StatusUnauthorized, "unauthorized")
		return
	}

	switch r.Method {
	case http.MethodGet:
		user, err := h.service.GetProfile(sess.UserID)
		if err != nil {
			jsonError(w, http.StatusNotFound, "user not found")
			return
		}
		jsonResponse(w, http.StatusOK, user)

	case http.MethodPatch:
		var body struct {
			DisplayName string `json:"display_name"`
			AvatarURL   string `json:"avatar_url"`
		}
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
			jsonError(w, http.StatusBadRequest, "invalid request payload")
			return
		}
		updated, err := h.service.UpdateProfile(sess.UserID, body.DisplayName, body.AvatarURL)
		if err != nil {
			jsonError(w, http.StatusInternalServerError, err.Error())
			return
		}
		jsonResponse(w, http.StatusOK, updated)

	default:
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
	}
}

// /api/v1/users/lookup?id=@username (GET)
func (h *Handlers) handleUsersLookup(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	landropID := r.URL.Query().Get("id")
	if landropID == "" {
		jsonError(w, http.StatusBadRequest, "query param 'id' is required")
		return
	}
	user, err := h.service.LookupLandropID(landropID)
	if err != nil {
		jsonError(w, http.StatusNotFound, "user not found")
		return
	}
	// Redact private info for public lookup
	sanitized := &UserAccount{
		UserID:      user.UserID,
		LandropID:   user.LandropID,
		DisplayName: user.DisplayName,
		AvatarURL:   user.AvatarURL,
		Status:      user.Status,
		CreatedAt:   user.CreatedAt,
	}
	jsonResponse(w, http.StatusOK, sanitized)
}

// /api/v1/users/search?q=query&limit=10 (GET)
func (h *Handlers) handleUsersSearch(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	query := r.URL.Query().Get("q")
	limitStr := r.URL.Query().Get("limit")
	limit := 10
	if n, err := strconv.Atoi(limitStr); err == nil && n > 0 {
		limit = n
	}

	results, err := h.service.SearchUsers(query, limit)
	if err != nil {
		jsonError(w, http.StatusInternalServerError, err.Error())
		return
	}
	jsonResponse(w, http.StatusOK, results)
}

// /api/v1/devices (GET, POST)
func (h *Handlers) handleDevices(w http.ResponseWriter, r *http.Request) {
	sess, err := h.extractSession(r)
	if err != nil {
		jsonError(w, http.StatusUnauthorized, "unauthorized")
		return
	}

	switch r.Method {
	case http.MethodGet:
		devices, err := h.service.ListDevices(sess.UserID)
		if err != nil {
			jsonError(w, http.StatusInternalServerError, err.Error())
			return
		}
		jsonResponse(w, http.StatusOK, devices)

	case http.MethodPost:
		var d Device
		if err := json.NewDecoder(r.Body).Decode(&d); err != nil {
			jsonError(w, http.StatusBadRequest, "invalid device payload")
			return
		}
		regDev, err := h.service.RegisterDevice(sess.UserID, &d)
		if err != nil {
			jsonError(w, http.StatusInternalServerError, err.Error())
			return
		}
		jsonResponse(w, http.StatusCreated, regDev)

	default:
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
	}
}

// /api/v1/devices/revoke (POST)
func (h *Handlers) handleDeviceRevoke(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	sess, err := h.extractSession(r)
	if err != nil {
		jsonError(w, http.StatusUnauthorized, "unauthorized")
		return
	}

	var body struct {
		DeviceID string `json:"device_id"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		jsonError(w, http.StatusBadRequest, "invalid request body")
		return
	}

	if err := h.service.RevokeDevice(sess.UserID, body.DeviceID); err != nil {
		jsonError(w, http.StatusInternalServerError, err.Error())
		return
	}

	if h.coordinator != nil {
		h.coordinator.DisconnectDevice(body.DeviceID)
		h.coordinator.BroadcastToUser(sess.UserID, &EventMessage{
			Type:         EventDeviceRevoked,
			Timestamp:    time.Now().Unix(),
			TargetUserID: sess.UserID,
			Payload: map[string]interface{}{
				"device_id": body.DeviceID,
				"revoked":   true,
			},
		})
	}

	jsonResponse(w, http.StatusOK, map[string]bool{"revoked": true})
}

// /api/v1/devices/pairing (POST)
func (h *Handlers) handleDevicePairing(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	sess, err := h.extractSession(r)
	if err != nil {
		jsonError(w, http.StatusUnauthorized, "unauthorized")
		return
	}

	session, err := h.service.CreatePairingSession(sess.UserID, 0)
	if err != nil {
		jsonError(w, http.StatusInternalServerError, err.Error())
		return
	}
	jsonResponse(w, http.StatusCreated, session)
}

// /api/v1/devices/pairing/confirm (POST)
func (h *Handlers) handleDevicePairingConfirm(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}

	var body struct {
		SessionID  string `json:"session_id"`
		Secret     string `json:"secret"`
		DeviceName string `json:"device_name"`
		DeviceType string `json:"device_type"`
		Platform   string `json:"platform"`
		AppVersion string `json:"app_version"`
		PublicKey  string `json:"public_key"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		jsonError(w, http.StatusBadRequest, "invalid request body")
		return
	}

	dev := &Device{
		DeviceName: body.DeviceName,
		DeviceType: body.DeviceType,
		Platform:   body.Platform,
		AppVersion: body.AppVersion,
		PublicKey:  body.PublicKey,
	}

	registeredDev, authRes, err := h.service.ConfirmPairing(body.SessionID, body.Secret, dev)
	if err != nil {
		jsonError(w, http.StatusBadRequest, err.Error())
		return
	}

	jsonResponse(w, http.StatusOK, map[string]interface{}{
		"device": registeredDev,
		"auth":   authRes,
	})
}

// /api/v1/contacts (GET)
func (h *Handlers) handleContacts(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	sess, err := h.extractSession(r)
	if err != nil {
		jsonError(w, http.StatusUnauthorized, "unauthorized")
		return
	}

	contacts, err := h.service.ListContacts(sess.UserID)
	if err != nil {
		jsonError(w, http.StatusInternalServerError, err.Error())
		return
	}
	jsonResponse(w, http.StatusOK, contacts)
}

// /api/v1/contacts/request (POST)
func (h *Handlers) handleContactRequest(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	sess, err := h.extractSession(r)
	if err != nil {
		jsonError(w, http.StatusUnauthorized, "unauthorized")
		return
	}

	var body struct {
		TargetLandropID string `json:"target_landrop_id"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		jsonError(w, http.StatusBadRequest, "invalid request body")
		return
	}

	entry, err := h.service.SendContactRequest(sess.UserID, body.TargetLandropID)
	if err != nil {
		jsonError(w, http.StatusBadRequest, err.Error())
		return
	}
	jsonResponse(w, http.StatusCreated, entry)
}

// /api/v1/contacts/accept (POST)
func (h *Handlers) handleContactAccept(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	sess, err := h.extractSession(r)
	if err != nil {
		jsonError(w, http.StatusUnauthorized, "unauthorized")
		return
	}

	var body struct {
		TargetUserID string `json:"target_user_id"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		jsonError(w, http.StatusBadRequest, "invalid request body")
		return
	}

	if err := h.service.AcceptContactRequest(sess.UserID, body.TargetUserID); err != nil {
		jsonError(w, http.StatusInternalServerError, err.Error())
		return
	}
	jsonResponse(w, http.StatusOK, map[string]bool{"accepted": true})
}

// /api/v1/contacts/decline (POST)
func (h *Handlers) handleContactDecline(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	sess, err := h.extractSession(r)
	if err != nil {
		jsonError(w, http.StatusUnauthorized, "unauthorized")
		return
	}

	var body struct {
		TargetUserID string `json:"target_user_id"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		jsonError(w, http.StatusBadRequest, "invalid request body")
		return
	}

	if err := h.service.DeclineContactRequest(sess.UserID, body.TargetUserID); err != nil {
		jsonError(w, http.StatusInternalServerError, err.Error())
		return
	}
	jsonResponse(w, http.StatusOK, map[string]bool{"declined": true})
}

// /api/v1/contacts/block (POST)
func (h *Handlers) handleContactBlock(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	sess, err := h.extractSession(r)
	if err != nil {
		jsonError(w, http.StatusUnauthorized, "unauthorized")
		return
	}

	var body struct {
		TargetUserID string `json:"target_user_id"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		jsonError(w, http.StatusBadRequest, "invalid request body")
		return
	}

	if err := h.service.BlockUser(sess.UserID, body.TargetUserID); err != nil {
		jsonError(w, http.StatusInternalServerError, err.Error())
		return
	}
	jsonResponse(w, http.StatusOK, map[string]bool{"blocked": true})
}

// /api/v1/contacts/favorite (POST, PUT)
func (h *Handlers) handleContactFavorite(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost && r.Method != http.MethodPut {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	sess, err := h.extractSession(r)
	if err != nil {
		jsonError(w, http.StatusUnauthorized, "unauthorized")
		return
	}

	var body struct {
		TargetUserID string `json:"target_user_id"`
		IsFavorite   bool   `json:"is_favorite"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		jsonError(w, http.StatusBadRequest, "invalid request body")
		return
	}

	if err := h.service.SetFavorite(sess.UserID, body.TargetUserID, body.IsFavorite); err != nil {
		jsonError(w, http.StatusInternalServerError, err.Error())
		return
	}
	jsonResponse(w, http.StatusOK, map[string]bool{"favorite": body.IsFavorite})
}

// /api/v1/contacts/recents (GET)
func (h *Handlers) handleContactRecents(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	sess, err := h.extractSession(r)
	if err != nil {
		jsonError(w, http.StatusUnauthorized, "unauthorized")
		return
	}

	limitStr := r.URL.Query().Get("limit")
	limit := 10
	if n, err := strconv.Atoi(limitStr); err == nil && n > 0 {
		limit = n
	}

	recents, err := h.service.GetRecentUsers(sess.UserID, limit)
	if err != nil {
		jsonError(w, http.StatusInternalServerError, err.Error())
		return
	}
	jsonResponse(w, http.StatusOK, recents)
}

// /api/v1/transfers/requests (POST, GET)
func (h *Handlers) handleTransferRequests(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodPost:
		var req TransferRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			jsonError(w, http.StatusBadRequest, "invalid request payload")
			return
		}

		// Allow optional session if sender is authenticated, or guest if not
		if sess, err := h.extractSession(r); err == nil {
			req.SenderUserID = sess.UserID
			if req.SenderDeviceID == "" {
				req.SenderDeviceID = sess.DeviceID
			}
		}

		offered, err := h.coordinator.OfferTransfer(&req)
		if err != nil {
			jsonError(w, http.StatusBadRequest, err.Error())
			return
		}
		jsonResponse(w, http.StatusCreated, offered)

	case http.MethodGet:
		reqID := r.URL.Query().Get("id")
		if reqID == "" {
			jsonError(w, http.StatusBadRequest, "query param 'id' is required")
			return
		}
		req, err := h.service.Store().GetTransferRequest(reqID)
		if err != nil {
			jsonError(w, http.StatusNotFound, "transfer request not found")
			return
		}
		jsonResponse(w, http.StatusOK, req)

	default:
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
	}
}

// /api/v1/transfers/requests/claim (POST)
func (h *Handlers) handleTransferClaim(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}

	var body struct {
		RequestID string `json:"request_id"`
		DeviceID  string `json:"device_id"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		jsonError(w, http.StatusBadRequest, "invalid request payload")
		return
	}

	userID := ""
	if sess, err := h.extractSession(r); err == nil {
		userID = sess.UserID
		if body.DeviceID == "" {
			body.DeviceID = sess.DeviceID
		}
	}

	claimed, err := h.coordinator.ClaimTransfer(body.RequestID, body.DeviceID, userID)
	if err != nil {
		if err == ErrTransferAlreadyClaimed {
			jsonError(w, http.StatusConflict, "transfer already claimed by another device")
			return
		}
		jsonError(w, http.StatusBadRequest, err.Error())
		return
	}
	jsonResponse(w, http.StatusOK, claimed)
}

// /api/v1/transfers/requests/decline (POST)
func (h *Handlers) handleTransferDecline(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}

	var body struct {
		RequestID string `json:"request_id"`
		DeviceID  string `json:"device_id"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		jsonError(w, http.StatusBadRequest, "invalid request payload")
		return
	}

	declined, err := h.coordinator.DeclineTransfer(body.RequestID, body.DeviceID)
	if err != nil {
		jsonError(w, http.StatusBadRequest, err.Error())
		return
	}
	jsonResponse(w, http.StatusOK, declined)
}

// /api/v1/transfers/requests/complete (POST)
func (h *Handlers) handleTransferComplete(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}

	var body struct {
		RequestID     string        `json:"request_id"`
		State         TransferState `json:"state"`
		TransportType string        `json:"transport_type"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		jsonError(w, http.StatusBadRequest, "invalid request payload")
		return
	}

	if body.State == "" {
		body.State = TransferStateCompleted
	}
	if body.TransportType == "" {
		body.TransportType = "WebRtcDataChannel"
	}

	completed, err := h.coordinator.CompleteTransfer(body.RequestID, body.State, body.TransportType)
	if err != nil {
		jsonError(w, http.StatusBadRequest, err.Error())
		return
	}
	jsonResponse(w, http.StatusOK, completed)
}

// /api/v1/history (GET, POST, DELETE)
func (h *Handlers) handleHistory(w http.ResponseWriter, r *http.Request) {
	sess, err := h.extractSession(r)
	if err != nil {
		jsonError(w, http.StatusUnauthorized, "unauthorized")
		return
	}

	switch r.Method {
	case http.MethodGet:
		limitStr := r.URL.Query().Get("limit")
		limit := 20
		if n, err := strconv.Atoi(limitStr); err == nil && n > 0 {
			limit = n
		}
		hist, err := h.service.GetHistory(sess.UserID, limit)
		if err != nil {
			jsonError(w, http.StatusInternalServerError, err.Error())
			return
		}
		jsonResponse(w, http.StatusOK, hist)

	case http.MethodPost:
		var rec TransferHistoryRecord
		if err := json.NewDecoder(r.Body).Decode(&rec); err != nil {
			jsonError(w, http.StatusBadRequest, "invalid history record")
			return
		}
		rec.UserID = sess.UserID
		if err := h.service.RecordHistory(&rec); err != nil {
			jsonError(w, http.StatusInternalServerError, err.Error())
			return
		}
		jsonResponse(w, http.StatusCreated, rec)

	case http.MethodDelete:
		if err := h.service.ClearHistory(sess.UserID); err != nil {
			jsonError(w, http.StatusInternalServerError, err.Error())
			return
		}
		jsonResponse(w, http.StatusOK, map[string]bool{"cleared": true})

	default:
		jsonError(w, http.StatusMethodNotAllowed, "method not allowed")
	}
}
