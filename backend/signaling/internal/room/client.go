package room

import (
	"errors"
	"sync"

	"landrop/signaling/internal/protocol"
)

// Sender abstracts message delivery to a connected peer.
type Sender interface {
	SendEnvelope(env *protocol.Envelope) error
	Close() error
}

// Client represents an active signaling session.
type Client struct {
	ID       string
	IP       string
	Role     string
	RoomCode string

	mu     sync.Mutex
	sender Sender
	closed bool
}

// NewClient creates a new Client tracking a connected peer.
func NewClient(id, ip string, sender Sender) *Client {
	return &Client{
		ID:     id,
		IP:     ip,
		sender: sender,
	}
}

// Send dispatches an envelope to the client.
func (c *Client) Send(env *protocol.Envelope) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.closed || c.sender == nil {
		return errors.New("client connection closed")
	}

	return c.sender.SendEnvelope(env)
}

// Close terminates client connection.
func (c *Client) Close() error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.closed {
		return nil
	}
	c.closed = true
	if c.sender != nil {
		return c.sender.Close()
	}
	return nil
}
