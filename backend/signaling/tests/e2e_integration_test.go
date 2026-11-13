package tests

import (
	"bytes"
	"encoding/json"
	"net/http"
	"strings"
	"testing"
	"time"

	"landrop/signaling/internal/identity"
	"landrop/signaling/internal/protocol"
)

// TestE2E_GuestWebSignalingRelay verifies full Guest ↔ Guest peer exchange over WebSockets
func TestE2E_GuestWebSignalingRelay(t *testing.T) {
	srv, ts := setupTestServer()
	defer srv.Close()
	defer ts.Close()

	// 1. Client A creates room
	connA := dialWS(t, ts.URL)
	defer connA.Close()

	createEnv, _ := protocol.NewEnvelope(protocol.ActionCreateRoom, "", "guest-initiator", nil)
	if err := connA.WriteJSON(createEnv); err != nil {
		t.Fatalf("failed to send CREATE_ROOM: %v", err)
	}

	createdResp := readEnvelopeWithTimeout(t, connA, 2*time.Second)
	if createdResp.Action != protocol.ActionRoomCreated {
		t.Fatalf("expected ROOM_CREATED, got %s", createdResp.Action)
	}

	var createdPayload protocol.RoomCreatedPayload
	if err := json.Unmarshal(createdPayloadBytes(createdResp), &createdPayload); err != nil {
		t.Fatalf("failed to unmarshal room created payload: %v", err)
	}

	roomCode := createdPayload.RoomCode
	if !strings.HasPrefix(roomCode, "DROP-") {
		t.Fatalf("unexpected room code format: %s", roomCode)
	}

	// 2. Client B joins room
	connB := dialWS(t, ts.URL)
	defer connB.Close()

	joinEnv, _ := protocol.NewEnvelope(protocol.ActionJoinRoom, roomCode, "guest-joiner", protocol.JoinRoomPayload{
		RoomCode: roomCode,
	})
	if err := connB.WriteJSON(joinEnv); err != nil {
		t.Fatalf("failed to send JOIN_ROOM: %v", err)
	}

	// Joiner receives PEER_JOINED
	joinedResp := readEnvelopeWithTimeout(t, connB, 2*time.Second)
	if joinedResp.Action != protocol.ActionPeerJoined {
		t.Fatalf("expected PEER_JOINED on joiner, got %s", joinedResp.Action)
	}

	// Initiator receives PEER_JOINED
	peerJoined := readEnvelopeWithTimeout(t, connA, 2*time.Second)
	if peerJoined.Action != protocol.ActionPeerJoined {
		t.Fatalf("expected PEER_JOINED on initiator, got %s", peerJoined.Action)
	}

	// 3. Client A sends SDP Offer
	offerEnv, _ := protocol.NewEnvelope(protocol.ActionSdpOffer, roomCode, "guest-initiator", protocol.SdpPayload{
		Type: "offer",
		Sdp:  "v=0\r\no=initiator 12345 2 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n",
	})
	if err := connA.WriteJSON(offerEnv); err != nil {
		t.Fatalf("failed to send SDP offer: %v", err)
	}

	relayedOffer := readEnvelopeWithTimeout(t, connB, 2*time.Second)
	if relayedOffer.Action != protocol.ActionSdpOffer {
		t.Fatalf("expected relayed SDP_OFFER, got %s", relayedOffer.Action)
	}

	// 4. Client B sends SDP Answer
	answerEnv, _ := protocol.NewEnvelope(protocol.ActionSdpAnswer, roomCode, "guest-joiner", protocol.SdpPayload{
		Type: "answer",
		Sdp:  "v=0\r\no=joiner 67890 2 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n",
	})
	if err := connB.WriteJSON(answerEnv); err != nil {
		t.Fatalf("failed to send SDP answer: %v", err)
	}

	relayedAnswer := readEnvelopeWithTimeout(t, connA, 2*time.Second)
	if relayedAnswer.Action != protocol.ActionSdpAnswer {
		t.Fatalf("expected relayed SDP_ANSWER, got %s", relayedAnswer.Action)
	}

	// 5. Client A sends ICE Candidate
	candEnv, _ := protocol.NewEnvelope(protocol.ActionIceCandidate, roomCode, "guest-initiator", protocol.IceCandidatePayload{
		Candidate:     "candidate:1 1 UDP 2130706431 192.168.1.100 50000 typ host",
		SdpMid:        "data",
		SdpMLineIndex: 0,
	})
	if err := connA.WriteJSON(candEnv); err != nil {
		t.Fatalf("failed to send ICE candidate: %v", err)
	}

	relayedCand := readEnvelopeWithTimeout(t, connB, 2*time.Second)
	if relayedCand.Action != protocol.ActionIceCandidate {
		t.Fatalf("expected relayed ICE_CANDIDATE, got %s", relayedCand.Action)
	}

	// 6. Graceful Leave Room
	leaveEnv, _ := protocol.NewEnvelope(protocol.ActionLeaveRoom, roomCode, "guest-joiner", nil)
	_ = connB.WriteJSON(leaveEnv)

	peerLeft := readEnvelopeWithTimeout(t, connA, 2*time.Second)
	if peerLeft.Action != protocol.ActionPeerLeft {
		t.Fatalf("expected PEER_LEFT notification, got %s", peerLeft.Action)
	}
}

// TestE2E_MagicLinkAndDeviceRevocationFlow tests magic link auth, WebSocket bind, and instant revocation teardown
func TestE2E_MagicLinkAndDeviceRevocationFlow(t *testing.T) {
	srv, ts := setupTestServer()
	defer srv.Close()
	defer ts.Close()

	// 1. Request magic link
	reqBody, _ := json.Marshal(map[string]string{"email": "alice@landrop.net"})
	resp, err := http.Post(ts.URL+"/api/v1/auth/magic-link/request", "application/json", bytes.NewReader(reqBody))
	if err != nil || resp.StatusCode != http.StatusOK {
		t.Fatalf("magic link request failed: code %d, err %v", resp.StatusCode, err)
	}
	var reqRes struct {
		Status string `json:"status"`
		Token  string `json:"token"`
	}
	_ = json.NewDecoder(resp.Body).Decode(&reqRes)
	resp.Body.Close()
	if reqRes.Status != "sent" || reqRes.Token == "" {
		t.Fatalf("invalid magic link request response: %+v", reqRes)
	}

	// 2. Verify magic link
	verifyBody, _ := json.Marshal(map[string]string{
		"token":       reqRes.Token,
		"device_name": "Alice's Workstation",
		"platform":    "Linux",
		"app_version": "2.0.0",
	})
	verifyResp, err := http.Post(ts.URL+"/api/v1/auth/magic-link/verify", "application/json", bytes.NewReader(verifyBody))
	if err != nil || verifyResp.StatusCode != http.StatusOK {
		t.Fatalf("magic link verify failed: code %d, err %v", verifyResp.StatusCode, err)
	}
	var authRes identity.AuthResult
	_ = json.NewDecoder(verifyResp.Body).Decode(&authRes)
	verifyResp.Body.Close()

	if authRes.Token == "" || authRes.Device.DeviceID == "" || !authRes.User.EmailVerified {
		t.Fatalf("invalid auth result: %+v", authRes)
	}

	// 3. Connect device via WebSocket and bind using session token
	conn := dialWS(t, ts.URL)
	defer conn.Close()

	bindEnv, _ := protocol.NewEnvelope(protocol.ActionBindClient, "", "alice-sock-1", map[string]string{
		"token":     authRes.Token,
		"device_id": authRes.Device.DeviceID,
	})
	if err := conn.WriteJSON(bindEnv); err != nil {
		t.Fatalf("failed to send BIND_CLIENT: %v", err)
	}

	boundResp := readEnvelopeWithTimeout(t, conn, 2*time.Second)
	if boundResp.Action != protocol.ActionClientBound {
		t.Fatalf("expected CLIENT_BOUND, got %s", boundResp.Action)
	}

	// 4. Revoke device
	revokeReq, _ := json.Marshal(map[string]string{
		"device_id": authRes.Device.DeviceID,
	})
	httpReq, _ := http.NewRequest("POST", ts.URL+"/api/v1/devices/revoke", bytes.NewReader(revokeReq))
	httpReq.Header.Set("Authorization", "Bearer "+authRes.Token)
	httpReq.Header.Set("Content-Type", "application/json")
	revokeResp, err := http.DefaultClient.Do(httpReq)
	if err != nil || revokeResp.StatusCode != http.StatusOK {
		t.Fatalf("device revoke failed: code %d, err %v", revokeResp.StatusCode, err)
	}
	revokeResp.Body.Close()

	// 5. Verify WebSocket is instantly disconnected / sends UNAUTHORIZED error
	_ = conn.SetReadDeadline(time.Now().Add(2 * time.Second))
	var errorEnv protocol.Envelope
	err = conn.ReadJSON(&errorEnv)
	if err == nil {
		if errorEnv.Action != protocol.ActionError {
			t.Fatalf("expected ERROR envelope upon revocation, got %s", errorEnv.Action)
		}
	}
	// Verify socket closes
	_, _, err = conn.ReadMessage()
	if err == nil {
		t.Fatalf("expected socket read to fail after revocation disconnect, but succeeded")
	}
}

// TestE2E_ModeBAtomicClaiming tests multi-device broadcast and atomic claim exclusivity
func TestE2E_ModeBAtomicClaiming(t *testing.T) {
	srv, ts := setupTestServer()
	defer srv.Close()
	defer ts.Close()

	// 1. Create Recipient User Bob via login
	bobAuthBody, _ := json.Marshal(map[string]string{
		"display_name": "Bob",
		"email":        "bob@landrop.net",
		"device_name":  "Bob's Laptop",
		"platform":     "Linux",
	})
	bobAuthResp, err := http.Post(ts.URL+"/api/v1/auth/login", "application/json", bytes.NewReader(bobAuthBody))
	if err != nil || bobAuthResp.StatusCode != http.StatusOK {
		t.Fatalf("Bob auth login failed: code %d, err %v", bobAuthResp.StatusCode, err)
	}
	var bobAuth1 identity.AuthResult
	_ = json.NewDecoder(bobAuthResp.Body).Decode(&bobAuth1)
	bobAuthResp.Body.Close()

	// 2. Register Device 2 for Bob
	bobDev2Body, _ := json.Marshal(map[string]string{
		"device_name": "Bob's Phone",
		"device_type": "mobile",
		"platform":    "Android",
	})
	dev2Req, _ := http.NewRequest("POST", ts.URL+"/api/v1/devices", bytes.NewReader(bobDev2Body))
	dev2Req.Header.Set("Authorization", "Bearer "+bobAuth1.Token)
	dev2Req.Header.Set("Content-Type", "application/json")
	dev2Resp, err := http.DefaultClient.Do(dev2Req)
	if err != nil || dev2Resp.StatusCode != http.StatusCreated {
		t.Fatalf("Bob dev2 register failed: code %d, err %v", dev2Resp.StatusCode, err)
	}
	var bobDev2 identity.Device
	_ = json.NewDecoder(dev2Resp.Body).Decode(&bobDev2)
	dev2Resp.Body.Close()

	// 3. Connect Bob Device 1 via WebSocket
	connDev1 := dialWS(t, ts.URL)
	defer connDev1.Close()
	bindEnv1, _ := protocol.NewEnvelope(protocol.ActionBindClient, "", "bob-d1", map[string]string{
		"token":     bobAuth1.Token,
		"device_id": bobAuth1.Device.DeviceID,
	})
	_ = connDev1.WriteJSON(bindEnv1)
	_ = readEnvelopeWithTimeout(t, connDev1, 2*time.Second) // CLIENT_BOUND

	// Connect Bob Device 2 via WebSocket
	connDev2 := dialWS(t, ts.URL)
	defer connDev2.Close()
	bindEnv2, _ := protocol.NewEnvelope(protocol.ActionBindClient, "", "bob-d2", map[string]string{
		"token":     bobAuth1.Token,
		"device_id": bobDev2.DeviceID,
	})
	_ = connDev2.WriteJSON(bindEnv2)
	_ = readEnvelopeWithTimeout(t, connDev2, 2*time.Second) // CLIENT_BOUND

	// 4. Create Sender Charlie and offer transfer in Mode B (ANY_AVAILABLE_DEVICE)
	charlieAuthBody, _ := json.Marshal(map[string]string{
		"display_name": "Charlie",
		"email":        "charlie@landrop.net",
		"device_name":  "Charlie's PC",
	})
	charlieResp, _ := http.Post(ts.URL+"/api/v1/auth/login", "application/json", bytes.NewReader(charlieAuthBody))
	var charlieAuth identity.AuthResult
	_ = json.NewDecoder(charlieResp.Body).Decode(&charlieAuth)
	charlieResp.Body.Close()

	transferOffer := identity.TransferRequest{
		RecipientUserID: bobAuth1.User.UserID,
		DeliveryMode:    identity.DeliveryModeAnyAvailableDevice,
		Files: []identity.TransferFileItem{
			{FileName: "shared_presentation.pdf", FileSize: 4500000},
			{FileName: "data.csv", FileSize: 120000},
		},
		TotalBytes: 4620000,
	}
	offerBody, _ := json.Marshal(transferOffer)
	offerReq, _ := http.NewRequest("POST", ts.URL+"/api/v1/transfers/requests", bytes.NewReader(offerBody))
	offerReq.Header.Set("Authorization", "Bearer "+charlieAuth.Token)
	offerReq.Header.Set("Content-Type", "application/json")

	offerResp, err := http.DefaultClient.Do(offerReq)
	if err != nil || offerResp.StatusCode != http.StatusCreated {
		t.Fatalf("transfer offer failed: %v", err)
	}
	var createdXfer identity.TransferRequest
	_ = json.NewDecoder(offerResp.Body).Decode(&createdXfer)
	offerResp.Body.Close()

	// 5. Verify both Device 1 and Device 2 receive transfer.requested event
	xferReq1 := readEnvelopeWithTimeout(t, connDev1, 2*time.Second)
	if xferReq1.Action != string(identity.EventTransferRequest) {
		t.Fatalf("expected transfer.requested on dev1, got %s", xferReq1.Action)
	}

	xferReq2 := readEnvelopeWithTimeout(t, connDev2, 2*time.Second)
	if xferReq2.Action != string(identity.EventTransferRequest) {
		t.Fatalf("expected transfer.requested on dev2, got %s", xferReq2.Action)
	}

	// 6. Device 1 claims the transfer
	claimBody, _ := json.Marshal(map[string]string{
		"request_id": createdXfer.RequestID,
		"device_id":  bobAuth1.Device.DeviceID,
	})
	claimReq, _ := http.NewRequest("POST", ts.URL+"/api/v1/transfers/requests/claim", bytes.NewReader(claimBody))
	claimReq.Header.Set("Authorization", "Bearer "+bobAuth1.Token)
	claimReq.Header.Set("Content-Type", "application/json")
	claimResp, err := http.DefaultClient.Do(claimReq)
	if err != nil || claimResp.StatusCode != http.StatusOK {
		t.Fatalf("Device 1 claim failed: code %d, err %v", claimResp.StatusCode, err)
	}
	var claimedResult identity.TransferRequest
	_ = json.NewDecoder(claimResp.Body).Decode(&claimedResult)
	claimResp.Body.Close()

	if claimedResult.State != identity.TransferStateAccepted || claimedResult.ClaimedByDeviceID != bobAuth1.Device.DeviceID {
		t.Fatalf("invalid claimed result state: %+v", claimedResult)
	}

	// 7. Verify Device 2 receives real-time transfer.claimed notification to dismiss popup
	dismissNotice := readEnvelopeWithTimeout(t, connDev2, 2*time.Second)
	if dismissNotice.Action != string(identity.EventTransferClaimed) {
		t.Fatalf("expected transfer.claimed on non-claiming dev2, got %s", dismissNotice.Action)
	}

	// 8. Device 2 tries to claim same transfer -> 409 Conflict (ErrTransferAlreadyClaimed)
	claim2Body, _ := json.Marshal(map[string]string{
		"request_id": createdXfer.RequestID,
		"device_id":  bobDev2.DeviceID,
	})
	claim2Req, _ := http.NewRequest("POST", ts.URL+"/api/v1/transfers/requests/claim", bytes.NewReader(claim2Body))
	claim2Req.Header.Set("Authorization", "Bearer "+bobAuth1.Token)
	claim2Req.Header.Set("Content-Type", "application/json")
	claim2Resp, err := http.DefaultClient.Do(claim2Req)
	if err != nil {
		t.Fatalf("claim 2 request error: %v", err)
	}
	claim2Resp.Body.Close()
	if claim2Resp.StatusCode != http.StatusConflict {
		t.Fatalf("expected 409 Conflict on competitor claim, got %d", claim2Resp.StatusCode)
	}
}

// TestE2E_HealthCheck verifies /healthz endpoint
func TestE2E_HealthCheck(t *testing.T) {
	srv, ts := setupTestServer()
	defer srv.Close()
	defer ts.Close()

	resp, err := http.Get(ts.URL + "/healthz")
	if err != nil || resp.StatusCode != http.StatusOK {
		t.Fatalf("GET /healthz failed: %v", err)
	}
	defer resp.Body.Close()

	var data map[string]interface{}
	if err := json.NewDecoder(resp.Body).Decode(&data); err != nil {
		t.Fatalf("failed to decode /healthz JSON: %v", err)
	}

	if data["status"] != "ok" {
		t.Fatalf("expected status 'ok', got %v", data["status"])
	}
	if data["database"] != "memory" {
		t.Fatalf("expected database 'memory', got %v", data["database"])
	}
}

func createdPayloadBytes(env protocol.Envelope) []byte {
	return env.Payload
}
