package protocol

import (
	"encoding/json"
	"time"
)

const (
	CurrentVersion = "2.0"

	// Actions
	ActionCreateRoom    = "CREATE_ROOM"
	ActionRoomCreated   = "ROOM_CREATED"
	ActionJoinRoom      = "JOIN_ROOM"
	ActionPeerJoined    = "PEER_JOINED"
	ActionSdpOffer      = "SDP_OFFER"
	ActionSdpAnswer     = "SDP_ANSWER"
	ActionIceCandidate  = "ICE_CANDIDATE"
	ActionPeerLeft      = "PEER_LEFT"
	ActionLeaveRoom     = "LEAVE_ROOM"
	ActionError         = "ERROR"

	// Roles
	RoleInitiator = "initiator"
	RoleJoiner    = "joiner"

	// Error Codes
	ErrCodeInvalidMessage = "ERR_INVALID_MESSAGE"
	ErrCodeRoomNotFound   = "ERR_ROOM_NOT_FOUND"
	ErrCodeRoomFull       = "ERR_ROOM_FULL"
	ErrCodeRateLimited    = "ERR_RATE_LIMITED"
	ErrCodeGlareViolation = "ERR_GLARE_VIOLATION"
	ErrCodeUnauthorized   = "ERR_UNAUTHORIZED"
	ErrCodeInternal       = "ERR_INTERNAL"
)

// Envelope is the standard JSON wrapper for all signaling WebSocket messages.
type Envelope struct {
	Version   string          `json:"version"`
	Action    string          `json:"action"`
	RoomCode  string          `json:"room_code,omitempty"`
	ClientID  string          `json:"client_id,omitempty"`
	Timestamp int64           `json:"timestamp"`
	Payload   json.RawMessage `json:"payload,omitempty"`
}

// RoomCreatedPayload is returned to the room initiator upon successful room creation.
type RoomCreatedPayload struct {
	RoomCode  string `json:"room_code"`
	RoomToken string `json:"room_token"`
	Role      string `json:"role"`
	ExpiresAt int64  `json:"expires_at"`
}

// JoinRoomPayload is sent by a peer requesting to join an existing room.
type JoinRoomPayload struct {
	RoomCode string `json:"room_code"`
}

// PeerJoinedPayload is broadcast to peers when two endpoints are connected.
type PeerJoinedPayload struct {
	RoomCode  string `json:"room_code"`
	PeerID    string `json:"peer_id"`
	Role      string `json:"role"`
	RoomToken string `json:"room_token,omitempty"`
}

// SdpPayload contains WebRTC SDP offer or answer string and optional E2EE public key.
type SdpPayload struct {
	Type          string `json:"type"` // "offer" or "answer"
	Sdp           string `json:"sdp"`
	E2EEPublicKey string `json:"e2ee_public_key,omitempty"`
}

// IceCandidatePayload encapsulates a trickle ICE candidate.
type IceCandidatePayload struct {
	Candidate     string `json:"candidate"`
	SdpMid        string `json:"sdp_mid,omitempty"`
	SdpMLineIndex int    `json:"sdp_mline_index"`
}

// PeerLeftPayload notifies a peer that the other party has left or disconnected.
type PeerLeftPayload struct {
	PeerID string `json:"peer_id,omitempty"`
	Reason string `json:"reason"`
}

// ErrorPayload describes a protocol or state error.
type ErrorPayload struct {
	Code    string `json:"code"`
	Message string `json:"message"`
}

// NewEnvelope creates a populated Envelope with current timestamp and JSON-marshaled payload.
func NewEnvelope(action, roomCode, clientID string, payload any) (*Envelope, error) {
	var rawPayload json.RawMessage
	if payload != nil {
		data, err := json.Marshal(payload)
		if err != nil {
			return nil, err
		}
		rawPayload = data
	}

	return &Envelope{
		Version:   CurrentVersion,
		Action:    action,
		RoomCode:  roomCode,
		ClientID:  clientID,
		Timestamp: time.Now().Unix(),
		Payload:   rawPayload,
	}, nil
}

// NewErrorEnvelope creates an Envelope carrying an ErrorPayload.
func NewErrorEnvelope(code, message, roomCode, clientID string) *Envelope {
	errPayload := ErrorPayload{
		Code:    code,
		Message: message,
	}
	raw, _ := json.Marshal(errPayload)

	return &Envelope{
		Version:   CurrentVersion,
		Action:    ActionError,
		RoomCode:  roomCode,
		ClientID:  clientID,
		Timestamp: time.Now().Unix(),
		Payload:   raw,
	}
}
