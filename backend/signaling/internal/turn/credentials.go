package turn

import (
	"crypto/hmac"
	"crypto/sha1"
	"encoding/base64"
	"fmt"
	"time"
)

// IceServer matches the W3C WebRTC RTCIceServer dictionary.
type IceServer struct {
	URLs       []string `json:"urls"`
	Username   string   `json:"username,omitempty"`
	Credential string   `json:"credential,omitempty"`
}

// GenerateCredentials generates an ephemeral username and HMAC-SHA1 password for Coturn.
func GenerateCredentials(secret, clientID string, ttl time.Duration) (string, string) {
	expiry := time.Now().Add(ttl).Unix()
	username := fmt.Sprintf("%d:%s", expiry, clientID)

	mac := hmac.New(sha1.New, []byte(secret))
	mac.Write([]byte(username))
	password := base64.StdEncoding.EncodeToString(mac.Sum(nil))

	return username, password
}

// GenerateIceConfig builds the full list of STUN and TURN servers with generated credentials.
func GenerateIceConfig(secret, clientID string, stunURLs, turnURLs []string, ttl time.Duration) []IceServer {
	var servers []IceServer

	// STUN servers (unauthenticated)
	if len(stunURLs) > 0 {
		servers = append(servers, IceServer{
			URLs: stunURLs,
		})
	}

	// TURN servers (ephemeral HMAC credentials)
	if len(turnURLs) > 0 && secret != "" {
		username, password := GenerateCredentials(secret, clientID, ttl)
		servers = append(servers, IceServer{
			URLs:       turnURLs,
			Username:   username,
			Credential: password,
		})
	}

	return servers
}

// VerifyCredentials verifies an incoming Coturn username and password against the shared secret.
func VerifyCredentials(secret, username, password string) bool {
	var expiry int64
	var clientID string
	_, err := fmt.Sscanf(username, "%d:%s", &expiry, &clientID)
	if err != nil {
		return false
	}

	if time.Now().Unix() > expiry {
		return false
	}

	mac := hmac.New(sha1.New, []byte(secret))
	mac.Write([]byte(username))
	expectedPassword := base64.StdEncoding.EncodeToString(mac.Sum(nil))

	return hmac.Equal([]byte(password), []byte(expectedPassword))
}
