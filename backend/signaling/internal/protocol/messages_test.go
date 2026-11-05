package protocol

import (
	"encoding/json"
	"testing"
)

func TestEnvelopeSerialization(t *testing.T) {
	payload := RoomCreatedPayload{
		RoomCode:  "DROP-K8M4-P9X2",
		RoomToken: "token-secret-12345",
		Role:      RoleInitiator,
		ExpiresAt: 1730822400,
	}

	env, err := NewEnvelope(ActionRoomCreated, "DROP-K8M4-P9X2", "client-1", payload)
	if err != nil {
		t.Fatalf("failed to create envelope: %v", err)
	}

	if env.Version != CurrentVersion {
		t.Errorf("expected version %s, got %s", CurrentVersion, env.Version)
	}
	if env.Action != ActionRoomCreated {
		t.Errorf("expected action %s, got %s", ActionRoomCreated, env.Action)
	}

	data, err := json.Marshal(env)
	if err != nil {
		t.Fatalf("failed to marshal envelope: %v", err)
	}

	var parsed Envelope
	if err := json.Unmarshal(data, &parsed); err != nil {
		t.Fatalf("failed to unmarshal envelope: %v", err)
	}

	var parsedPayload RoomCreatedPayload
	if err := json.Unmarshal(parsed.Payload, &parsedPayload); err != nil {
		t.Fatalf("failed to unmarshal payload: %v", err)
	}

	if parsedPayload.RoomCode != payload.RoomCode {
		t.Errorf("expected room code %s, got %s", payload.RoomCode, parsedPayload.RoomCode)
	}
	if parsedPayload.Role != RoleInitiator {
		t.Errorf("expected role %s, got %s", RoleInitiator, parsedPayload.Role)
	}
}

func TestErrorEnvelope(t *testing.T) {
	env := NewErrorEnvelope(ErrCodeRoomNotFound, "room does not exist", "DROP-XXXX-YYYY", "c-1")
	if env.Action != ActionError {
		t.Fatalf("expected action ERROR, got %s", env.Action)
	}

	var errPayload ErrorPayload
	if err := json.Unmarshal(env.Payload, &errPayload); err != nil {
		t.Fatalf("failed to unmarshal error payload: %v", err)
	}

	if errPayload.Code != ErrCodeRoomNotFound {
		t.Errorf("expected error code %s, got %s", ErrCodeRoomNotFound, errPayload.Code)
	}
}
