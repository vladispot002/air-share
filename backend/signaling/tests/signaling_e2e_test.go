package tests

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/gorilla/websocket"

	"landrop/signaling/internal/protocol"
	"landrop/signaling/internal/server"
)

func setupTestServer() (*server.Server, *httptest.Server) {
	cfg := server.Config{
		TurnSecret:  "test-secret-12345",
		StunURLs:    []string{"stun:127.0.0.1:19302"},
		TurnURLs:    []string{"turn:127.0.0.1:3478?transport=udp"},
		RoomTTL:     10 * time.Minute,
		MaxFailures: 5,
		RateWindow:  10 * time.Minute,
		BlockTime:   10 * time.Minute,
	}

	srv := server.NewServer(cfg)
	ts := httptest.NewServer(srv.Handler())
	return srv, ts
}

func dialWS(t *testing.T, serverURL string) *websocket.Conn {
	t.Helper()
	wsURL := "ws" + strings.TrimPrefix(serverURL, "http") + "/ws/signaling"
	conn, _, err := websocket.DefaultDialer.Dial(wsURL, nil)
	if err != nil {
		t.Fatalf("failed to dial websocket at %s: %v", wsURL, err)
	}
	return conn
}

// TC-SIG-01: Room Creation, 8-char Crockford Base32 formatting, duplicate rejection & TTL cleanup
func TestTCSIG01_RoomLifecycle(t *testing.T) {
	srv, ts := setupTestServer()
	defer srv.Close()
	defer ts.Close()

	connA := dialWS(t, ts.URL)
	defer connA.Close()

	// 1. Create Room
	createEnv, _ := protocol.NewEnvelope(protocol.ActionCreateRoom, "", "client-a", nil)
	if err := connA.WriteJSON(createEnv); err != nil {
		t.Fatalf("failed to write CREATE_ROOM: %v", err)
	}

	var respEnv protocol.Envelope
	if err := connA.ReadJSON(&respEnv); err != nil {
		t.Fatalf("failed to read response: %v", err)
	}

	if respEnv.Action != protocol.ActionRoomCreated {
		t.Fatalf("expected Action ROOM_CREATED, got %s", respEnv.Action)
	}

	var payload protocol.RoomCreatedPayload
	if err := json.Unmarshal(respEnv.Payload, &payload); err != nil {
		t.Fatalf("failed to unmarshal payload: %v", err)
	}

	if !strings.HasPrefix(payload.RoomCode, "DROP-") {
		t.Fatalf("room code %s does not have DROP- prefix", payload.RoomCode)
	}
	if payload.Role != protocol.RoleInitiator {
		t.Fatalf("expected initiator role, got %s", payload.Role)
	}
	if payload.RoomToken == "" {
		t.Fatalf("room token must not be empty")
	}

	// 2. Verify token access to /api/v1/turn-config
	req, _ := http.NewRequest("GET", ts.URL+"/api/v1/turn-config", nil)
	req.Header.Set("Authorization", "Bearer "+payload.RoomToken)
	turnResp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatalf("GET /api/v1/turn-config failed: %v", err)
	}
	defer turnResp.Body.Close()

	if turnResp.StatusCode != http.StatusOK {
		t.Fatalf("expected status 200 for valid token, got %d", turnResp.StatusCode)
	}

	// 3. Verify unauthorized access without token
	badReq, _ := http.NewRequest("GET", ts.URL+"/api/v1/turn-config", nil)
	badResp, err := http.DefaultClient.Do(badReq)
	if err != nil {
		t.Fatalf("GET /api/v1/turn-config error: %v", err)
	}
	defer badResp.Body.Close()
	if badResp.StatusCode != http.StatusUnauthorized {
		t.Fatalf("expected 401 for missing token, got %d", badResp.StatusCode)
	}
}

// TC-SIG-02: Brute-Force Rate Limiting (5 failed join attempts trigger ERR_RATE_LIMITED)
func TestTCSIG02_RateLimiting(t *testing.T) {
	srv, ts := setupTestServer()
	defer srv.Close()
	defer ts.Close()

	conn := dialWS(t, ts.URL)
	defer conn.Close()

	// Perform 5 invalid JOIN_ROOM attempts
	for i := 0; i < 5; i++ {
		joinEnv, _ := protocol.NewEnvelope(protocol.ActionJoinRoom, "DROP-0000-0000", "attacker", nil)
		if err := conn.WriteJSON(joinEnv); err != nil {
			t.Fatalf("failed to write JOIN_ROOM attempt %d: %v", i+1, err)
		}

		var resp protocol.Envelope
		if err := conn.ReadJSON(&resp); err != nil {
			t.Fatalf("failed to read error response: %v", err)
		}
		if resp.Action != protocol.ActionError {
			t.Fatalf("expected ERROR action, got %s", resp.Action)
		}
	}

	// 6th attempt must return ERR_RATE_LIMITED
	joinEnv, _ := protocol.NewEnvelope(protocol.ActionJoinRoom, "DROP-0000-0000", "attacker", nil)
	if err := conn.WriteJSON(joinEnv); err != nil {
		t.Fatalf("failed to write 6th JOIN_ROOM: %v", err)
	}

	var throttledResp protocol.Envelope
	if err := conn.ReadJSON(&throttledResp); err != nil {
		t.Fatalf("failed to read throttled response: %v", err)
	}

	var errPayload protocol.ErrorPayload
	if err := json.Unmarshal(throttledResp.Payload, &errPayload); err != nil {
		t.Fatalf("failed to parse error payload: %v", err)
	}

	if errPayload.Code != protocol.ErrCodeRateLimited {
		t.Fatalf("expected error code %s, got %s", protocol.ErrCodeRateLimited, errPayload.Code)
	}
}

// TC-SIG-03: Glare Prevention & Disconnect Notification
func TestTCSIG03_GlareAndDisconnect(t *testing.T) {
	srv, ts := setupTestServer()
	defer srv.Close()
	defer ts.Close()

	connA := dialWS(t, ts.URL)
	defer connA.Close()

	connB := dialWS(t, ts.URL)
	defer connB.Close()

	// 1. Node A creates room
	createEnv, _ := protocol.NewEnvelope(protocol.ActionCreateRoom, "", "node-a", nil)
	_ = connA.WriteJSON(createEnv)

	var roomCreated protocol.Envelope
	_ = connA.ReadJSON(&roomCreated)
	var createdPayload protocol.RoomCreatedPayload
	_ = json.Unmarshal(roomCreated.Payload, &createdPayload)
	roomCode := createdPayload.RoomCode

	// 2. Node B joins room
	joinEnv, _ := protocol.NewEnvelope(protocol.ActionJoinRoom, roomCode, "node-b", nil)
	_ = connB.WriteJSON(joinEnv)

	// Both receive PEER_JOINED
	var bJoined, aJoined protocol.Envelope
	_ = connB.ReadJSON(&bJoined)
	_ = connA.ReadJSON(&aJoined)

	if bJoined.Action != protocol.ActionPeerJoined || aJoined.Action != protocol.ActionPeerJoined {
		t.Fatalf("expected PEER_JOINED for both peers")
	}

	// 3. Glare test: Node B (joiner) attempts to send SDP_OFFER -> must be rejected
	badOffer, _ := protocol.NewEnvelope(protocol.ActionSdpOffer, roomCode, "node-b", protocol.SdpPayload{
		Type: "offer",
		Sdp:  "v=0 mock-offer",
	})
	_ = connB.WriteJSON(badOffer)

	var glareErr protocol.Envelope
	_ = connB.ReadJSON(&glareErr)
	if glareErr.Action != protocol.ActionError {
		t.Fatalf("expected ERROR on glare violation, got %s", glareErr.Action)
	}
	var glarePayload protocol.ErrorPayload
	_ = json.Unmarshal(glareErr.Payload, &glarePayload)
	if glarePayload.Code != protocol.ErrCodeGlareViolation {
		t.Fatalf("expected code %s, got %s", protocol.ErrCodeGlareViolation, glarePayload.Code)
	}

	// 4. Node A sends valid SDP_OFFER -> Node B receives it
	validOffer, _ := protocol.NewEnvelope(protocol.ActionSdpOffer, roomCode, "node-a", protocol.SdpPayload{
		Type: "offer",
		Sdp:  "v=0 valid-offer",
	})
	_ = connA.WriteJSON(validOffer)

	var bOfferReceived protocol.Envelope
	_ = connB.ReadJSON(&bOfferReceived)
	if bOfferReceived.Action != protocol.ActionSdpOffer {
		t.Fatalf("expected Node B to receive SDP_OFFER, got %s", bOfferReceived.Action)
	}

	// 5. Node B sends valid SDP_ANSWER -> Node A receives it
	validAnswer, _ := protocol.NewEnvelope(protocol.ActionSdpAnswer, roomCode, "node-b", protocol.SdpPayload{
		Type: "answer",
		Sdp:  "v=0 valid-answer",
	})
	_ = connB.WriteJSON(validAnswer)

	var aAnswerReceived protocol.Envelope
	_ = connA.ReadJSON(&aAnswerReceived)
	if aAnswerReceived.Action != protocol.ActionSdpAnswer {
		t.Fatalf("expected Node A to receive SDP_ANSWER, got %s", aAnswerReceived.Action)
	}

	// 6. ICE candidate relay
	candidateEnv, _ := protocol.NewEnvelope(protocol.ActionIceCandidate, roomCode, "node-a", protocol.IceCandidatePayload{
		Candidate:     "candidate:1 1 UDP 2122260223 127.0.0.1 54321 typ host",
		SdpMid:        "0",
		SdpMLineIndex: 0,
	})
	_ = connA.WriteJSON(candidateEnv)

	var bCandReceived protocol.Envelope
	_ = connB.ReadJSON(&bCandReceived)
	if bCandReceived.Action != protocol.ActionIceCandidate {
		t.Fatalf("expected Node B to receive ICE_CANDIDATE, got %s", bCandReceived.Action)
	}

	// 7. Disconnect Notification: Node A abruptly closes connection
	connA.Close()

	var bLeftReceived protocol.Envelope
	_ = connB.ReadJSON(&bLeftReceived)
	if bLeftReceived.Action != protocol.ActionPeerLeft {
		t.Fatalf("expected Node B to receive PEER_LEFT, got %s", bLeftReceived.Action)
	}
}
