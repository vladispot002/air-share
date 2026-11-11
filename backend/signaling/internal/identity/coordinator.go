package identity

import (
	"crypto/rand"
	"errors"
	"sync"
	"time"
)

// EventType enumerates real-time event topics dispatched across WebSocket connections.
type EventType string

const (
	EventDeviceOnline     EventType = "device.online"
	EventDeviceOffline    EventType = "device.offline"
	EventDeviceRevoked    EventType = "device.revoked"
	EventContactRequest   EventType = "contact.request"
	EventContactAccepted  EventType = "contact.accepted"
	EventTransferRequest  EventType = "transfer.requested"
	EventTransferAccepted EventType = "transfer.accepted"
	EventTransferClaimed  EventType = "transfer.claimed"
	EventTransferDeclined EventType = "transfer.declined"
	EventTransferComplete EventType = "transfer.completed"
)

// EventMessage represents a notification envelope delivered to connected clients.
type EventMessage struct {
	Type           EventType   `json:"type"`
	Timestamp      int64       `json:"timestamp"`
	TargetUserID   string      `json:"target_user_id,omitempty"`
	TargetDeviceID string      `json:"target_device_id,omitempty"`
	Payload        interface{} `json:"payload"`
}

// EventBroadcaster delivers real-time notifications to connected clients.
type EventBroadcaster interface {
	BroadcastToUser(userID string, event *EventMessage)
	BroadcastToDevice(deviceID string, event *EventMessage)
}

// DeviceDisconnecter terminates a device's active WebSocket connection.
type DeviceDisconnecter interface {
	DisconnectDevice(deviceID string)
}

// NoopBroadcaster satisfies EventBroadcaster when no active websocket hub is bound.
type NoopBroadcaster struct{}

func (n *NoopBroadcaster) BroadcastToUser(string, *EventMessage)   {}
func (n *NoopBroadcaster) BroadcastToDevice(string, *EventMessage) {}

// Coordinator manages transfer offers, atomic device claims (Mode B), and metadata events.
type Coordinator struct {
	service     *Service
	broadcaster EventBroadcaster
	mu          sync.RWMutex
}

// NewCoordinator constructs a new Transfer Coordinator.
func NewCoordinator(service *Service, broadcaster EventBroadcaster) *Coordinator {
	if broadcaster == nil {
		broadcaster = &NoopBroadcaster{}
	}
	return &Coordinator{
		service:     service,
		broadcaster: broadcaster,
	}
}

// SetBroadcaster updates the event broadcasting engine.
func (c *Coordinator) SetBroadcaster(b EventBroadcaster) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if b == nil {
		b = &NoopBroadcaster{}
	}
	c.broadcaster = b
}

// BroadcastToUser delegates to the underlying broadcaster.
func (c *Coordinator) BroadcastToUser(userID string, event *EventMessage) {
	c.mu.RLock()
	defer c.mu.RUnlock()
	if c.broadcaster != nil {
		c.broadcaster.BroadcastToUser(userID, event)
	}
}

// BroadcastToDevice delegates to the underlying broadcaster.
func (c *Coordinator) BroadcastToDevice(deviceID string, event *EventMessage) {
	c.mu.RLock()
	defer c.mu.RUnlock()
	if c.broadcaster != nil {
		c.broadcaster.BroadcastToDevice(deviceID, event)
	}
}

// DisconnectDevice disconnects the active WebSocket connection of a revoked device.
func (c *Coordinator) DisconnectDevice(deviceID string) {
	c.mu.RLock()
	defer c.mu.RUnlock()
	if dd, ok := c.broadcaster.(DeviceDisconnecter); ok {
		dd.DisconnectDevice(deviceID)
	}
}

func generateRoomCode() string {
	charset := "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
	b := make([]byte, 6)
	_, _ = rand.Read(b)
	for i := 0; i < 6; i++ {
		b[i] = charset[int(b[i])%len(charset)]
	}
	return string(b)
}

// OfferTransfer initiates a transfer request from sender to recipient (Mode A or Mode B).
func (c *Coordinator) OfferTransfer(req *TransferRequest) (*TransferRequest, error) {
	if req.RecipientUserID == "" {
		return nil, errors.New("recipient_user_id is required")
	}
	if len(req.Files) == 0 {
		return nil, errors.New("at least one file is required")
	}

	// Verify recipient exists
	recip, err := c.service.Store().GetUserByID(req.RecipientUserID)
	if err != nil {
		return nil, err
	}

	if req.RequestID == "" {
		req.RequestID = GenerateUUID()
	}
	if req.RoomCode == "" {
		req.RoomCode = generateRoomCode()
	}
	if req.DeliveryMode == "" {
		if req.TargetDeviceID != "" {
			req.DeliveryMode = DeliveryModeSpecificDevice
		} else {
			req.DeliveryMode = DeliveryModeAnyAvailableDevice
		}
	}

	var total uint64
	for _, f := range req.Files {
		total += f.FileSize
	}
	req.TotalBytes = total
	req.State = TransferStateAwaiting
	now := time.Now().UTC()
	req.CreatedAt = now
	req.ExpiresAt = now.Add(5 * time.Minute)

	if err := c.service.Store().CreateTransferRequest(req); err != nil {
		return nil, err
	}

	// Record interaction in recent users
	if req.SenderUserID != "" {
		_ = c.service.RecordInteraction(&RecentUserEntry{
			UserID:             req.SenderUserID,
			TargetUserID:       recip.UserID,
			LandropID:          recip.LandropID,
			DisplayName:        recip.DisplayName,
			AvatarURL:          recip.AvatarURL,
			LastTransferStatus: string(TransferStateAwaiting),
		})
		senderUser, sErr := c.service.Store().GetUserByID(req.SenderUserID)
		if sErr == nil {
			_ = c.service.RecordInteraction(&RecentUserEntry{
				UserID:             recip.UserID,
				TargetUserID:       senderUser.UserID,
				LandropID:          senderUser.LandropID,
				DisplayName:        senderUser.DisplayName,
				AvatarURL:          senderUser.AvatarURL,
				LastTransferStatus: string(TransferStateAwaiting),
			})
		}
	}

	event := &EventMessage{
		Type:      EventTransferRequest,
		Timestamp: now.Unix(),
		Payload:   req,
	}

	c.mu.RLock()
	broadcaster := c.broadcaster
	c.mu.RUnlock()

	if req.DeliveryMode == DeliveryModeSpecificDevice && req.TargetDeviceID != "" {
		event.TargetDeviceID = req.TargetDeviceID
		broadcaster.BroadcastToDevice(req.TargetDeviceID, event)
	} else {
		event.TargetUserID = req.RecipientUserID
		broadcaster.BroadcastToUser(req.RecipientUserID, event)
	}

	return req, nil
}

// ClaimTransfer performs an atomic claim on an offered transfer (especially in Mode B).
func (c *Coordinator) ClaimTransfer(requestID, deviceID, userID string) (*TransferRequest, error) {
	req, err := c.service.Store().ClaimTransferRequest(requestID, deviceID)
	if err != nil {
		return nil, err
	}

	now := time.Now().UTC()

	c.mu.RLock()
	broadcaster := c.broadcaster
	c.mu.RUnlock()

	// 1. Notify all other devices of recipient that the transfer was claimed
	claimedMsg := &EventMessage{
		Type:         EventTransferClaimed,
		Timestamp:    now.Unix(),
		TargetUserID: req.RecipientUserID,
		Payload: map[string]interface{}{
			"request_id":           req.RequestID,
			"claimed_by_device_id": deviceID,
			"state":                TransferStateAccepted,
		},
	}
	broadcaster.BroadcastToUser(req.RecipientUserID, claimedMsg)

	// 2. Notify sender that transfer was accepted and provide WebRTC RoomCode
	acceptedMsg := &EventMessage{
		Type:           EventTransferAccepted,
		Timestamp:      now.Unix(),
		TargetDeviceID: req.SenderDeviceID,
		TargetUserID:   req.SenderUserID,
		Payload: map[string]interface{}{
			"request_id":           req.RequestID,
			"claimed_by_device_id": deviceID,
			"room_code":            req.RoomCode,
			"state":                TransferStateAccepted,
		},
	}
	if req.SenderDeviceID != "" {
		broadcaster.BroadcastToDevice(req.SenderDeviceID, acceptedMsg)
	} else if req.SenderUserID != "" {
		broadcaster.BroadcastToUser(req.SenderUserID, acceptedMsg)
	}

	return req, nil
}

// DeclineTransfer records rejection and dismisses notifications on recipient devices.
func (c *Coordinator) DeclineTransfer(requestID, deviceID string) (*TransferRequest, error) {
	req, err := c.service.Store().GetTransferRequest(requestID)
	if err != nil {
		return nil, err
	}

	req.State = TransferStateRejected
	if err := c.service.Store().UpdateTransferRequest(req); err != nil {
		return nil, err
	}

	now := time.Now().UTC()

	c.mu.RLock()
	broadcaster := c.broadcaster
	c.mu.RUnlock()

	declineMsg := &EventMessage{
		Type:         EventTransferDeclined,
		Timestamp:    now.Unix(),
		TargetUserID: req.RecipientUserID,
		Payload: map[string]interface{}{
			"request_id": req.RequestID,
			"device_id":  deviceID,
			"state":      TransferStateRejected,
		},
	}
	// Broadcast to recipient devices to dismiss modal
	broadcaster.BroadcastToUser(req.RecipientUserID, declineMsg)

	// Broadcast to sender
	if req.SenderDeviceID != "" {
		broadcaster.BroadcastToDevice(req.SenderDeviceID, declineMsg)
	} else if req.SenderUserID != "" {
		broadcaster.BroadcastToUser(req.SenderUserID, declineMsg)
	}

	return req, nil
}

// CompleteTransfer marks transfer success or failure and writes metadata-only history.
func (c *Coordinator) CompleteTransfer(requestID string, finalState TransferState, transportType string) (*TransferRequest, error) {
	req, err := c.service.Store().GetTransferRequest(requestID)
	if err != nil {
		return nil, err
	}

	req.State = finalState
	_ = c.service.Store().UpdateTransferRequest(req)

	now := time.Now().UTC()

	// Write metadata history for recipient
	recipHist := &TransferHistoryRecord{
		TransferID:     req.RequestID,
		UserID:         req.RecipientUserID,
		Direction:      "download",
		PeerUserID:     req.SenderUserID,
		PeerLandropID:  req.SenderLandropID,
		PeerDeviceName: req.SenderDeviceName,
		Files:          req.Files,
		TotalBytes:     req.TotalBytes,
		Status:         finalState,
		TransportType:  transportType,
		StartedAt:      req.CreatedAt,
		CompletedAt:    now,
	}
	_ = c.service.RecordHistory(recipHist)

	// If sender is authenticated, write history for sender too
	if req.SenderUserID != "" {
		senderHist := &TransferHistoryRecord{
			TransferID:     req.RequestID,
			UserID:         req.SenderUserID,
			Direction:      "upload",
			PeerUserID:     req.RecipientUserID,
			Files:          req.Files,
			TotalBytes:     req.TotalBytes,
			Status:         finalState,
			TransportType:  transportType,
			StartedAt:      req.CreatedAt,
			CompletedAt:    now,
		}
		_ = c.service.RecordHistory(senderHist)
	}

	c.mu.RLock()
	broadcaster := c.broadcaster
	c.mu.RUnlock()

	compMsg := &EventMessage{
		Type:      EventTransferComplete,
		Timestamp: now.Unix(),
		Payload: map[string]interface{}{
			"request_id": req.RequestID,
			"state":      finalState,
		},
	}
	broadcaster.BroadcastToUser(req.RecipientUserID, compMsg)
	if req.SenderUserID != "" {
		broadcaster.BroadcastToUser(req.SenderUserID, compMsg)
	}

	return req, nil
}
