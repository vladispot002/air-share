package room

import (
	"errors"
	"sync"
	"time"

	"landrop/signaling/internal/protocol"
)

// State represents the lifecycle stage of an ephemeral transfer room.
type State string

const (
	StateWaiting     State = "WAITING"     // Room created, waiting for joiner
	StateConnected   State = "CONNECTED"   // Both peers connected, exchanging SDP/ICE
	StateEstablished State = "ESTABLISHED" // DataChannel connected; room on standby for ICE restart
	StateClosed      State = "CLOSED"      // Room terminated
)

var (
	ErrRoomFull       = errors.New(protocol.ErrCodeRoomFull)
	ErrGlareViolation = errors.New(protocol.ErrCodeGlareViolation)
	ErrNotParticipant = errors.New("client is not a participant in this room")
	ErrCannotJoinOwn  = errors.New("client cannot join their own room")
)

// Room manages point-to-point negotiation between two peers.
type Room struct {
	mu        sync.RWMutex
	Code      string
	Token     string
	State     State
	CreatedAt time.Time
	ExpiresAt time.Time

	Initiator *Client
	Joiner    *Client
}

// NewRoom constructs a new ephemeral room with an Initiator.
func NewRoom(code, token string, initiator *Client, ttl time.Duration) *Room {
	now := time.Now()
	initiator.Role = protocol.RoleInitiator
	initiator.RoomCode = code

	return &Room{
		Code:      code,
		Token:     token,
		State:     StateWaiting,
		CreatedAt: now,
		ExpiresAt: now.Add(ttl),
		Initiator: initiator,
	}
}

// AddJoiner joins a second peer into the room.
func (r *Room) AddJoiner(joiner *Client) error {
	r.mu.Lock()
	defer r.mu.Unlock()

	if r.Initiator != nil && r.Initiator.ID == joiner.ID {
		return ErrCannotJoinOwn
	}

	if r.State != StateWaiting || r.Joiner != nil {
		return ErrRoomFull
	}

	joiner.Role = protocol.RoleJoiner
	joiner.RoomCode = r.Code
	r.Joiner = joiner
	r.State = StateConnected

	return nil
}

// GetPeer returns the counterpart peer for the specified client.
func (r *Room) GetPeer(clientID string) (*Client, error) {
	r.mu.RLock()
	defer r.mu.RUnlock()

	if r.Initiator != nil && r.Initiator.ID == clientID {
		if r.Joiner == nil {
			return nil, errors.New("peer not joined yet")
		}
		return r.Joiner, nil
	}

	if r.Joiner != nil && r.Joiner.ID == clientID {
		if r.Initiator == nil {
			return nil, errors.New("initiator disconnected")
		}
		return r.Initiator, nil
	}

	return nil, ErrNotParticipant
}

// ValidateSdpRole enforces glare prevention: Initiator creates Offer, Joiner creates Answer.
func (r *Room) ValidateSdpRole(clientID, sdpType string) error {
	r.mu.RLock()
	defer r.mu.RUnlock()

	if r.Initiator != nil && r.Initiator.ID == clientID {
		if sdpType != "offer" {
			return ErrGlareViolation
		}
		return nil
	}

	if r.Joiner != nil && r.Joiner.ID == clientID {
		if sdpType != "answer" {
			return ErrGlareViolation
		}
		return nil
	}

	return ErrNotParticipant
}

// SetEstablished marks the room as having successfully established P2P connectivity.
func (r *Room) SetEstablished() {
	r.mu.Lock()
	defer r.mu.Unlock()

	if r.State == StateConnected {
		r.State = StateEstablished
	}
}

// IsExpired checks if the room TTL has passed.
func (r *Room) IsExpired(now time.Time) bool {
	r.mu.RLock()
	defer r.mu.RUnlock()

	return now.After(r.ExpiresAt)
}

// IsClosed thread-safely reports if the room state is closed.
func (r *Room) IsClosed() bool {
	r.mu.RLock()
	defer r.mu.RUnlock()

	return r.State == StateClosed
}

// Close marks room as closed.
func (r *Room) Close() {
	r.mu.Lock()
	defer r.mu.Unlock()

	r.State = StateClosed
}
