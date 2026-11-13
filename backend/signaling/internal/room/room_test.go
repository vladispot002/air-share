package room

import (
	"regexp"
	"strings"
	"testing"
	"time"

	"landrop/signaling/internal/protocol"
)

type mockSender struct {
	sent []*protocol.Envelope
}

func (m *mockSender) SendEnvelope(env *protocol.Envelope) error {
	m.sent = append(m.sent, env)
	return nil
}

func (m *mockSender) Close() error {
	return nil
}

func TestCrockfordBase32CodeGeneration(t *testing.T) {
	codeRegex := regexp.MustCompile(`^DROP-[0-9A-HJKMNP-Z]{4}-[0-9A-HJKMNP-Z]{4}$`)

	for i := 0; i < 50; i++ {
		code, err := GenerateRoomCode()
		if err != nil {
			t.Fatalf("GenerateRoomCode error: %v", err)
		}
		if !codeRegex.MatchString(code) {
			t.Fatalf("code %s does not match expected Crockford format", code)
		}
		// Confirm characters I, L, O, U are strictly excluded from generated payload
		if strings.ContainsAny(strings.TrimPrefix(code, "DROP-"), "ILOU") {
			t.Fatalf("code %s contains ambiguous characters (ILOU)", code)
		}
	}
}

func TestNormalizeRoomCode(t *testing.T) {
	cases := []struct {
		input    string
		expected string
		valid    bool
	}{
		{"DROP-K8M4-P9X2", "DROP-K8M4-P9X2", true},
		{"drop-k8m4-p9x2", "DROP-K8M4-P9X2", true},
		{"k8m4p9x2", "DROP-K8M4-P9X2", true},
		{"DROP K8M4 P9X2", "DROP-K8M4-P9X2", true},
		{"DROP-K8M4-P9XO", "DROP-K8M4-P9X0", true}, // 'O' corrected to '0'
		{"DROP-K8M4-P9XL", "DROP-K8M4-P9X1", true}, // 'L' corrected to '1'
		{"DROP-K8M4-P9XI", "DROP-K8M4-P9X1", true}, // 'I' corrected to '1'
		{"DROP-DROP-K8M4", "DROP-DR0P-K8M4", true}, // 'O' in code suffix converted to '0'
		{"https://landrop-server.duckdns.org/#DROP-K8M4-P9X2", "DROP-K8M4-P9X2", true},
		{"https://landrop-server.duckdns.org/#k8m4p9x2", "DROP-K8M4-P9X2", true},
		{"#DROP-K8M4-P9X2", "DROP-K8M4-P9X2", true},
		{"https://landrop-server.duckdns.org/DROP-K8M4-P9X2", "DROP-K8M4-P9X2", true},
		{"  \nDROP-K8M4-P9X2\t\r", "DROP-K8M4-P9X2", true},
		{"DROP-SHORT", "", false},                  // Invalid length
		{"DROP-INVALID-CODE-TOO-LONG", "", false},  // Invalid length
	}

	for _, c := range cases {
		normalized, err := NormalizeRoomCode(c.input)
		if c.valid {
			if err != nil {
				t.Errorf("NormalizeRoomCode(%q) unexpected error: %v", c.input, err)
			}
			if normalized != c.expected {
				t.Errorf("NormalizeRoomCode(%q) = %q, expected %q", c.input, normalized, c.expected)
			}
		} else {
			if err == nil {
				t.Errorf("NormalizeRoomCode(%q) expected error, got %q", c.input, normalized)
			}
		}
	}
}

func TestRoomLifecycleAndGlarePrevention(t *testing.T) {
	mgr := NewManager(10 * time.Minute)
	defer mgr.Close()

	initiatorSender := &mockSender{}
	joinerSender := &mockSender{}
	thirdSender := &mockSender{}

	initiator := NewClient("init-1", "127.0.0.1", initiatorSender)
	joiner := NewClient("join-2", "127.0.0.2", joinerSender)
	third := NewClient("third-3", "127.0.0.3", thirdSender)

	room, err := mgr.CreateRoom(initiator)
	if err != nil {
		t.Fatalf("failed to create room: %v", err)
	}

	if room.State != StateWaiting {
		t.Errorf("expected StateWaiting, got %s", room.State)
	}

	// Self-join must be rejected
	if err := room.AddJoiner(initiator); err != ErrCannotJoinOwn {
		t.Fatalf("expected ErrCannotJoinOwn when initiator tries to join own room, got: %v", err)
	}

	// Token lookup should find the room
	lookedUp, exists := mgr.GetRoomByToken(room.Token)
	if !exists || lookedUp.Code != room.Code {
		t.Errorf("room token lookup failed")
	}

	// Joiner joins
	joinedRoom, err := mgr.JoinRoom(room.Code, joiner)
	if err != nil {
		t.Fatalf("failed to join room: %v", err)
	}
	if joinedRoom.State != StateConnected {
		t.Errorf("expected StateConnected, got %s", joinedRoom.State)
	}

	// 3rd peer must be rejected with ErrRoomFull
	_, err = mgr.JoinRoom(room.Code, third)
	if err != ErrRoomFull {
		t.Fatalf("expected ErrRoomFull for 3rd peer, got: %v", err)
	}

	// Glare Prevention:
	// Initiator must send offer
	if err := room.ValidateSdpRole("init-1", "offer"); err != nil {
		t.Errorf("initiator offer should be permitted: %v", err)
	}
	if err := room.ValidateSdpRole("init-1", "answer"); err != ErrGlareViolation {
		t.Errorf("initiator answer should be rejected with ErrGlareViolation, got %v", err)
	}

	// Joiner must send answer
	if err := room.ValidateSdpRole("join-2", "answer"); err != nil {
		t.Errorf("joiner answer should be permitted: %v", err)
	}
	if err := room.ValidateSdpRole("join-2", "offer"); err != ErrGlareViolation {
		t.Errorf("joiner offer should be rejected with ErrGlareViolation, got %v", err)
	}

	// Non-participant
	if err := room.ValidateSdpRole("random-peer", "offer"); err != ErrNotParticipant {
		t.Errorf("unknown peer should be rejected with ErrNotParticipant, got %v", err)
	}

	// Transition to Established
	room.SetEstablished()
	if room.State != StateEstablished {
		t.Errorf("expected StateEstablished, got %s", room.State)
	}
}

func TestManagerCleanup(t *testing.T) {
	// 50ms TTL, 20ms cleanup interval
	mgr := NewManagerWithInterval(50*time.Millisecond, 20*time.Millisecond)
	defer mgr.Close()

	initiator := NewClient("init-1", "127.0.0.1", &mockSender{})
	room, err := mgr.CreateRoom(initiator)
	if err != nil {
		t.Fatalf("create room error: %v", err)
	}

	if mgr.ActiveCount() != 1 {
		t.Fatalf("expected active count 1, got %d", mgr.ActiveCount())
	}

	// Wait for TTL expiration and cleanup loop ticker to evict
	time.Sleep(100 * time.Millisecond)

	if mgr.ActiveCount() != 0 {
		t.Fatalf("expected active count 0 after cleanup, got %d", mgr.ActiveCount())
	}

	// Querying by code or token should return not found
	if _, exists := mgr.GetRoom(room.Code); exists {
		t.Errorf("expired room should not be retrievable")
	}
	if _, exists := mgr.GetRoomByToken(room.Token); exists {
		t.Errorf("expired room token should not be valid")
	}
}
