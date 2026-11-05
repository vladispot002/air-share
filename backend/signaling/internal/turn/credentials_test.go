package turn

import (
	"testing"
	"time"
)

func TestCredentialsGenerationAndVerification(t *testing.T) {
	secret := "super-secure-coturn-shared-secret"
	clientID := "peer-node-alpha-123"

	username, password := GenerateCredentials(secret, clientID, 1*time.Hour)
	if username == "" || password == "" {
		t.Fatalf("expected non-empty username and password")
	}

	if !VerifyCredentials(secret, username, password) {
		t.Fatalf("generated credentials should be valid")
	}

	// Tampered secret
	if VerifyCredentials("wrong-secret", username, password) {
		t.Fatalf("verification should fail with incorrect secret")
	}

	// Tampered password
	if VerifyCredentials(secret, username, "corrupted-password") {
		t.Fatalf("verification should fail with corrupted password")
	}

	// Expired credentials
	expiredUser, expiredPass := GenerateCredentials(secret, clientID, -10*time.Second)
	if VerifyCredentials(secret, expiredUser, expiredPass) {
		t.Fatalf("expired credentials should be rejected")
	}
}

func TestGenerateIceConfig(t *testing.T) {
	secret := "turn-secret-key"
	stunList := []string{"stun:stun.l.google.com:19302"}
	turnList := []string{"turn:relay.landrop.net:3478?transport=udp"}

	servers := GenerateIceConfig(secret, "client-x", stunList, turnList, 30*time.Minute)
	if len(servers) != 2 {
		t.Fatalf("expected 2 ICE server configurations, got %d", len(servers))
	}

	if len(servers[0].URLs) != 1 || servers[0].URLs[0] != stunList[0] {
		t.Errorf("unexpected STUN url: %v", servers[0].URLs)
	}
	if servers[0].Username != "" {
		t.Errorf("STUN server should not have credentials")
	}

	if servers[1].Username == "" || servers[1].Credential == "" {
		t.Errorf("TURN server must have ephemeral credentials")
	}
}
