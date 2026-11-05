package room

import (
	"errors"
	"sync"
	"time"

	"landrop/signaling/internal/protocol"
)

var (
	ErrRoomNotFound = errors.New(protocol.ErrCodeRoomNotFound)
)

// Manager coordinates in-memory ephemeral rooms with concurrency and automatic cleanup.
type Manager struct {
	mu        sync.RWMutex
	rooms     map[string]*Room
	tokens    map[string]string // token -> roomCode
	ttl       time.Duration
	stopClean chan struct{}
}

// NewManager creates a room manager with default 30-second cleanup ticker.
func NewManager(ttl time.Duration) *Manager {
	return NewManagerWithInterval(ttl, 30*time.Second)
}

// NewManagerWithInterval creates a room manager with a custom cleanup interval.
func NewManagerWithInterval(ttl, cleanupInterval time.Duration) *Manager {
	m := &Manager{
		rooms:     make(map[string]*Room),
		tokens:    make(map[string]string),
		ttl:       ttl,
		stopClean: make(chan struct{}),
	}

	go m.cleanupLoop(cleanupInterval)
	return m
}

// Close stops background cleanup goroutines.
func (m *Manager) Close() {
	select {
	case <-m.stopClean:
	default:
		close(m.stopClean)
	}
}

// CreateRoom generates a unique room code, initializes the room, and registers it.
func (m *Manager) CreateRoom(initiator *Client) (*Room, error) {
	m.mu.Lock()
	defer m.mu.Unlock()

	var code string
	for i := 0; i < 10; i++ {
		candidate, err := GenerateRoomCode()
		if err != nil {
			return nil, err
		}
		if _, exists := m.rooms[candidate]; !exists {
			code = candidate
			break
		}
	}

	if code == "" {
		return nil, errors.New("failed to generate unique room code")
	}

	token, err := GenerateRoomToken()
	if err != nil {
		return nil, err
	}

	r := NewRoom(code, token, initiator, m.ttl)
	m.rooms[code] = r
	m.tokens[token] = code

	return r, nil
}

// JoinRoom normalizes the code, looks up the room, and registers the joiner.
func (m *Manager) JoinRoom(rawCode string, joiner *Client) (*Room, error) {
	canonicalCode, err := NormalizeRoomCode(rawCode)
	if err != nil {
		return nil, ErrRoomNotFound
	}

	m.mu.Lock()
	r, exists := m.rooms[canonicalCode]
	m.mu.Unlock()

	if !exists || r.IsClosed() {
		return nil, ErrRoomNotFound
	}

	if err := r.AddJoiner(joiner); err != nil {
		return nil, err
	}

	return r, nil
}

// GetRoom retrieves an active room by canonical code.
func (m *Manager) GetRoom(rawCode string) (*Room, bool) {
	canonicalCode, err := NormalizeRoomCode(rawCode)
	if err != nil {
		return nil, false
	}

	m.mu.RLock()
	defer m.mu.RUnlock()

	r, exists := m.rooms[canonicalCode]
	if !exists || r.IsClosed() {
		return nil, false
	}
	return r, true
}

// GetRoomByToken retrieves an active, non-expired room using its authorization token.
func (m *Manager) GetRoomByToken(token string) (*Room, bool) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	code, exists := m.tokens[token]
	if !exists {
		return nil, false
	}

	r, exists := m.rooms[code]
	if !exists || r.IsClosed() || r.IsExpired(time.Now()) {
		return nil, false
	}

	return r, true
}

// RemoveRoom deletes a room and its associated token.
func (m *Manager) RemoveRoom(rawCode string) {
	canonicalCode, err := NormalizeRoomCode(rawCode)
	if err != nil {
		return
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	if r, exists := m.rooms[canonicalCode]; exists {
		delete(m.tokens, r.Token)
		delete(m.rooms, canonicalCode)
		r.Close()
	}
}

// ActiveCount returns current active rooms count.
func (m *Manager) ActiveCount() int {
	m.mu.RLock()
	defer m.mu.RUnlock()

	return len(m.rooms)
}

func (m *Manager) cleanupLoop(interval time.Duration) {
	ticker := time.NewTicker(interval)
	defer ticker.Stop()

	for {
		select {
		case <-m.stopClean:
			return
		case now := <-ticker.C:
			m.mu.Lock()
			for code, r := range m.rooms {
				if r.IsExpired(now) || r.IsClosed() {
					delete(m.tokens, r.Token)
					delete(m.rooms, code)
					r.Close()
				}
			}
			m.mu.Unlock()
		}
	}
}
