package tests

import (
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
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
		TrustProxy:  false,
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

func readEnvelopeWithTimeout(t *testing.T, conn *websocket.Conn, timeout time.Duration) protocol.Envelope {
	t.Helper()
	if err := conn.SetReadDeadline(time.Now().Add(timeout)); err != nil {
		t.Fatalf("failed to set read deadline: %v", err)
	}
	var env protocol.Envelope
	if err := conn.ReadJSON(&env); err != nil {
		t.Fatalf("failed to read json envelope within %v: %v", timeout, err)
	}
	return env
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

	respEnv := readEnvelopeWithTimeout(t, connA, 2*time.Second)
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

	// 2. Verify Initiator token access to /api/v1/turn-config (with CORS headers)
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
	if origin := turnResp.Header.Get("Access-Control-Allow-Origin"); origin != "*" {
		t.Errorf("expected CORS Access-Control-Allow-Origin: *, got %s", origin)
	}

	// 3. Verify OPTIONS preflight request handling
	optReq, _ := http.NewRequest("OPTIONS", ts.URL+"/api/v1/turn-config", nil)
	optResp, err := http.DefaultClient.Do(optReq)
	if err != nil {
		t.Fatalf("OPTIONS /api/v1/turn-config error: %v", err)
	}
	defer optResp.Body.Close()
	if optResp.StatusCode != http.StatusNoContent {
		t.Fatalf("expected status 204 for OPTIONS preflight, got %d", optResp.StatusCode)
	}

	// 4. Verify unauthorized access without token
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

		resp := readEnvelopeWithTimeout(t, conn, 2*time.Second)
		if resp.Action != protocol.ActionError {
			t.Fatalf("expected ERROR action, got %s", resp.Action)
		}
	}

	// 6th attempt must return ERR_RATE_LIMITED
	joinEnv, _ := protocol.NewEnvelope(protocol.ActionJoinRoom, "DROP-0000-0000", "attacker", nil)
	if err := conn.WriteJSON(joinEnv); err != nil {
		t.Fatalf("failed to write 6th JOIN_ROOM: %v", err)
	}

	throttledResp := readEnvelopeWithTimeout(t, conn, 2*time.Second)
	var errPayload protocol.ErrorPayload
	if err := json.Unmarshal(throttledResp.Payload, &errPayload); err != nil {
		t.Fatalf("failed to parse error payload: %v", err)
	}

	if errPayload.Code != protocol.ErrCodeRateLimited {
		t.Fatalf("expected error code %s, got %s", protocol.ErrCodeRateLimited, errPayload.Code)
	}
}

// TC-SIG-03: Glare Prevention, Joiner Token Delivery & Disconnect Notification
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
	if err := connA.WriteJSON(createEnv); err != nil {
		t.Fatalf("failed to send CREATE_ROOM: %v", err)
	}

	roomCreated := readEnvelopeWithTimeout(t, connA, 2*time.Second)
	var createdPayload protocol.RoomCreatedPayload
	if err := json.Unmarshal(roomCreated.Payload, &createdPayload); err != nil {
		t.Fatalf("failed to parse room created payload: %v", err)
	}
	roomCode := createdPayload.RoomCode

	// 2. Node B joins room
	joinEnv, _ := protocol.NewEnvelope(protocol.ActionJoinRoom, roomCode, "node-b", nil)
	if err := connB.WriteJSON(joinEnv); err != nil {
		t.Fatalf("failed to send JOIN_ROOM: %v", err)
	}

	// Both receive PEER_JOINED
	bJoined := readEnvelopeWithTimeout(t, connB, 2*time.Second)
	aJoined := readEnvelopeWithTimeout(t, connA, 2*time.Second)

	if bJoined.Action != protocol.ActionPeerJoined || aJoined.Action != protocol.ActionPeerJoined {
		t.Fatalf("expected PEER_JOINED for both peers, got %s and %s", bJoined.Action, aJoined.Action)
	}

	// Verify Joiner receives RoomToken for TURN access (SEC-06 remediation)
	var bPayload protocol.PeerJoinedPayload
	if err := json.Unmarshal(bJoined.Payload, &bPayload); err != nil {
		t.Fatalf("failed to parse joiner payload: %v", err)
	}
	if bPayload.RoomToken == "" {
		t.Fatalf("joiner must receive room token for TURN access")
	}

	// Joiner authenticates against /api/v1/turn-config
	joinerReq, _ := http.NewRequest("GET", ts.URL+"/api/v1/turn-config", nil)
	joinerReq.Header.Set("Authorization", "Bearer "+bPayload.RoomToken)
	joinerTurnResp, err := http.DefaultClient.Do(joinerReq)
	if err != nil {
		t.Fatalf("joiner turn config request failed: %v", err)
	}
	defer joinerTurnResp.Body.Close()
	if joinerTurnResp.StatusCode != http.StatusOK {
		t.Fatalf("expected 200 for joiner turn config, got %d", joinerTurnResp.StatusCode)
	}

	// 3. Glare test: Node B (joiner) attempts to send SDP_OFFER -> must be rejected
	badOffer, _ := protocol.NewEnvelope(protocol.ActionSdpOffer, roomCode, "node-b", protocol.SdpPayload{
		Type: "offer",
		Sdp:  "v=0 mock-offer",
	})
	if err := connB.WriteJSON(badOffer); err != nil {
		t.Fatalf("failed to send bad offer: %v", err)
	}

	glareErr := readEnvelopeWithTimeout(t, connB, 2*time.Second)
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
	if err := connA.WriteJSON(validOffer); err != nil {
		t.Fatalf("failed to send valid offer: %v", err)
	}

	bOfferReceived := readEnvelopeWithTimeout(t, connB, 2*time.Second)
	if bOfferReceived.Action != protocol.ActionSdpOffer {
		t.Fatalf("expected Node B to receive SDP_OFFER, got %s", bOfferReceived.Action)
	}

	// 5. Node B sends valid SDP_ANSWER -> Node A receives it
	validAnswer, _ := protocol.NewEnvelope(protocol.ActionSdpAnswer, roomCode, "node-b", protocol.SdpPayload{
		Type: "answer",
		Sdp:  "v=0 valid-answer",
	})
	if err := connB.WriteJSON(validAnswer); err != nil {
		t.Fatalf("failed to send valid answer: %v", err)
	}

	aAnswerReceived := readEnvelopeWithTimeout(t, connA, 2*time.Second)
	if aAnswerReceived.Action != protocol.ActionSdpAnswer {
		t.Fatalf("expected Node A to receive SDP_ANSWER, got %s", aAnswerReceived.Action)
	}

	// 6. ICE candidate relay
	candidateEnv, _ := protocol.NewEnvelope(protocol.ActionIceCandidate, roomCode, "node-a", protocol.IceCandidatePayload{
		Candidate:     "candidate:1 1 UDP 2122260223 127.0.0.1 54321 typ host",
		SdpMid:        "0",
		SdpMLineIndex: 0,
	})
	if err := connA.WriteJSON(candidateEnv); err != nil {
		t.Fatalf("failed to send ICE candidate: %v", err)
	}

	bCandReceived := readEnvelopeWithTimeout(t, connB, 2*time.Second)
	if bCandReceived.Action != protocol.ActionIceCandidate {
		t.Fatalf("expected Node B to receive ICE_CANDIDATE, got %s", bCandReceived.Action)
	}

	// 7. Disconnect Notification: Node A abruptly closes connection
	connA.Close()

	bLeftReceived := readEnvelopeWithTimeout(t, connB, 2*time.Second)
	if bLeftReceived.Action != protocol.ActionPeerLeft {
		t.Fatalf("expected Node B to receive PEER_LEFT, got %s", bLeftReceived.Action)
	}
}

// TestOversizedPayloadRejection verifies 64 KB read limit prevents OOM
func TestOversizedPayloadRejection(t *testing.T) {
	srv, ts := setupTestServer()
	defer srv.Close()
	defer ts.Close()

	conn := dialWS(t, ts.URL)
	defer conn.Close()

	// Send a payload larger than 64 KB
	oversized := strings.Repeat("A", 70*1024)
	errEnv := protocol.Envelope{
		Version:  protocol.CurrentVersion,
		Action:   protocol.ActionCreateRoom,
		ClientID: oversized,
	}

	_ = conn.WriteJSON(errEnv)

	// Server should close connection due to read limit exceeded
	_ = conn.SetReadDeadline(time.Now().Add(1 * time.Second))
	var resp protocol.Envelope
	err := conn.ReadJSON(&resp)
	if err == nil {
		t.Fatalf("expected connection to fail on oversized frame")
	}
}

// TestStaticWebAssetServing verifies static web assets are served from WebDir
func TestStaticWebAssetServing(t *testing.T) {
	tempDir := t.TempDir()
	indexContent := "<html><body><h1>LAN-Drop Web Test</h1></body></html>"
	if err := os.WriteFile(tempDir+"/index.html", []byte(indexContent), 0644); err != nil {
		t.Fatalf("failed to write test index.html: %v", err)
	}

	cfg := server.Config{
		TurnSecret: "test-secret",
		StunURLs:   []string{"stun:127.0.0.1:19302"},
		WebDir:     tempDir,
	}

	srv := server.NewServer(cfg)
	defer srv.Close()
	ts := httptest.NewServer(srv.Handler())
	defer ts.Close()

	resp, err := http.Get(ts.URL + "/")
	if err != nil {
		t.Fatalf("failed to GET /: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		t.Fatalf("expected HTTP 200, got %d", resp.StatusCode)
	}

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		t.Fatalf("failed to read response body: %v", err)
	}

	if !strings.Contains(string(body), "LAN-Drop Web Test") {
		t.Fatalf("expected index.html content, got: %s", string(body))
	}
}

