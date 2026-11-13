package identity

import (
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"regexp"
	"strings"
	"time"
)

var (
	ErrUserNotFound           = errors.New("user not found")
	ErrLandropIDTaken         = errors.New("landrop ID already in use")
	ErrInvalidLandropID       = errors.New("invalid landrop ID format: must be @[a-zA-Z0-9_-]{3,30}")
	ErrDeviceNotFound         = errors.New("device not found")
	ErrDeviceRevoked          = errors.New("device access revoked")
	ErrContactAlreadyExists   = errors.New("contact relation already exists")
	ErrTransferNotFound       = errors.New("transfer request not found")
	ErrTransferAlreadyClaimed = errors.New("transfer already claimed by another device")
	ErrTransferExpired        = errors.New("transfer request expired")
	ErrUnauthorized           = errors.New("unauthorized operation")
	ErrEmailAlreadyRegistered = errors.New("email is already registered")
	ErrInvalidCredentials     = errors.New("invalid email or password")
	ErrPasswordTooShort       = errors.New("password must be at least 6 characters")
)

var landropIDRegex = regexp.MustCompile(`^@[a-zA-Z0-9_-]{3,30}$`)

// ValidateLandropID verifies that a given ID matches the canonical format '@name'.
func ValidateLandropID(id string) (string, error) {
	trimmed := strings.TrimSpace(id)
	if !strings.HasPrefix(trimmed, "@") {
		trimmed = "@" + trimmed
	}
	if !landropIDRegex.MatchString(trimmed) {
		return "", ErrInvalidLandropID
	}
	return strings.ToLower(trimmed), nil
}

// UserAccount represents a persistent user identity.
type UserAccount struct {
	UserID         string         `json:"user_id"`
	LandropID      string         `json:"landrop_id"`
	DisplayName    string         `json:"display_name"`
	AvatarURL      string         `json:"avatar_url"`
	Email          string         `json:"email"`
	EmailVerified  bool           `json:"email_verified"`
	CreatedAt      time.Time      `json:"created_at"`
	Status         string         `json:"status"` // "active", "suspended"
	PasswordHash   string         `json:"-"`
	AuthIdentities []AuthIdentity `json:"auth_identities,omitempty"`
}

// AuthIdentity represents an external OAuth/OIDC or email credential linked to an account.
type AuthIdentity struct {
	Provider       string    `json:"provider"` // "google", "apple", "github", "microsoft", "email"
	ProviderUserID string    `json:"provider_user_id"`
	Email          string    `json:"email"`
	LinkedAt       time.Time `json:"linked_at"`
}

// DeviceStatus represents whether a device is active or revoked.
type DeviceStatus string

const (
	DeviceStatusActive  DeviceStatus = "active"
	DeviceStatusRevoked DeviceStatus = "revoked"
)

// DevicePresence represents live presence state.
type DevicePresence string

const (
	PresenceOnline  DevicePresence = "online"
	PresenceOffline DevicePresence = "offline"
)

// Device represents a registered physical device or browser session under an account.
type Device struct {
	DeviceID     string         `json:"device_id"`
	UserID       string         `json:"user_id"`
	DeviceName   string         `json:"device_name"`
	DeviceType   string         `json:"device_type"` // "desktop", "laptop", "mobile", "tablet", "browser"
	Platform     string         `json:"platform"`    // "linux", "windows", "macos", "android", "ios", "web"
	AppVersion   string         `json:"app_version"`
	PublicKey    string         `json:"public_key,omitempty"`
	RegisteredAt time.Time      `json:"registered_at"`
	LastSeenAt   time.Time      `json:"last_seen_at"`
	Status       DeviceStatus   `json:"status"`
	Presence     DevicePresence `json:"presence"`
}

// DevicePairingSession represents a short-lived QR code exchange session to register a new device.
type DevicePairingSession struct {
	SessionID string    `json:"session_id"`
	UserID    string    `json:"user_id"`
	Secret    string    `json:"secret"`
	ExpiresAt time.Time `json:"expires_at"`
	Used      bool      `json:"used"`
}

// ContactState models relationship status between two accounts.
type ContactState string

const (
	ContactNone            ContactState = "NONE"
	ContactRequestSent     ContactState = "REQUEST_SENT"
	ContactRequestReceived ContactState = "REQUEST_RECEIVED"
	ContactAccepted        ContactState = "ACCEPTED"
	ContactBlocked         ContactState = "BLOCKED"
)

// ContactEntry represents a contact or pending relationship.
type ContactEntry struct {
	UserID       string       `json:"user_id"`
	TargetUserID string       `json:"target_user_id"`
	LandropID    string       `json:"landrop_id"`
	DisplayName  string       `json:"display_name"`
	AvatarURL    string       `json:"avatar_url"`
	State        ContactState `json:"state"`
	IsFavorite   bool         `json:"is_favorite"`
	UpdatedAt    time.Time    `json:"updated_at"`
}

// RecentUserEntry captures the last interaction between two users.
type RecentUserEntry struct {
	UserID             string    `json:"user_id"`
	TargetUserID       string    `json:"target_user_id"`
	LandropID          string    `json:"landrop_id"`
	DisplayName        string    `json:"display_name"`
	AvatarURL          string    `json:"avatar_url"`
	LastInteraction    time.Time `json:"last_interaction"`
	LastTransferStatus string    `json:"last_transfer_status"`
	AvailableDevices   int       `json:"available_devices"`
}

// DeliveryMode specifies how a transfer should be routed to the recipient.
type DeliveryMode string

const (
	DeliveryModeSpecificDevice     DeliveryMode = "SPECIFIC_DEVICE"
	DeliveryModeAnyAvailableDevice DeliveryMode = "ANY_AVAILABLE_DEVICE"
)

// TransferState represents state transitions of a transfer request.
type TransferState string

const (
	TransferStateCreated      TransferState = "CREATED"
	TransferStateOffered      TransferState = "OFFERED"
	TransferStateAwaiting     TransferState = "AWAITING_APPROVAL"
	TransferStateAccepted     TransferState = "ACCEPTED"
	TransferStateRejected     TransferState = "REJECTED"
	TransferStateConnecting   TransferState = "CONNECTING"
	TransferStateTransferring TransferState = "TRANSFERRING"
	TransferStatePaused       TransferState = "PAUSED"
	TransferStateCompleted    TransferState = "COMPLETED"
	TransferStateFailed       TransferState = "FAILED"
	TransferStateCancelled    TransferState = "CANCELLED"
	TransferStateExpired      TransferState = "EXPIRED"
)

// TransferFileItem holds file metadata (NO file content).
type TransferFileItem struct {
	FileName  string `json:"file_name"`
	FileSize  uint64 `json:"file_size"`
	Sha256Hex string `json:"sha256_hex,omitempty"`
}

// TransferRequest tracks an active or pending transfer session.
type TransferRequest struct {
	RequestID         string             `json:"request_id"`
	SenderUserID      string             `json:"sender_user_id,omitempty"` // empty if Guest
	SenderLandropID   string             `json:"sender_landrop_id,omitempty"`
	SenderDeviceID    string             `json:"sender_device_id"`
	SenderDeviceName  string             `json:"sender_device_name"`
	RecipientUserID   string             `json:"recipient_user_id"`
	TargetDeviceID    string             `json:"target_device_id,omitempty"` // populated in Mode A
	DeliveryMode      DeliveryMode       `json:"delivery_mode"`
	Files             []TransferFileItem `json:"files"`
	TotalBytes        uint64             `json:"total_bytes"`
	State             TransferState      `json:"state"`
	ClaimedByDeviceID string             `json:"claimed_by_device_id,omitempty"`
	RoomCode          string             `json:"room_code,omitempty"`
	CreatedAt         time.Time          `json:"created_at"`
	ExpiresAt         time.Time          `json:"expires_at"`
}

// TransferHistoryRecord stores synchronized metadata only.
type TransferHistoryRecord struct {
	TransferID     string             `json:"transfer_id"`
	UserID         string             `json:"user_id"`
	Direction      string             `json:"direction"` // "upload", "download"
	PeerUserID     string             `json:"peer_user_id,omitempty"`
	PeerLandropID  string             `json:"peer_landrop_id,omitempty"`
	PeerDeviceName string             `json:"peer_device_name,omitempty"`
	Files          []TransferFileItem `json:"files"`
	TotalBytes     uint64             `json:"total_bytes"`
	Status         TransferState      `json:"status"`
	TransportType  string             `json:"transport_type"` // "WebRtcDataChannel", "TcpDirect", "TlsTcp"
	StartedAt      time.Time          `json:"started_at"`
	CompletedAt    time.Time          `json:"completed_at"`
}

// GenerateRandomHex produces secure hex strings for IDs and secrets.
func GenerateRandomHex(bytesCount int) string {
	b := make([]byte, bytesCount)
	_, _ = rand.Read(b)
	return hex.EncodeToString(b)
}

// GenerateUUID generates a random UUID v4 format string.
func GenerateUUID() string {
	b := make([]byte, 16)
	_, _ = rand.Read(b)
	b[6] = (b[6] & 0x0f) | 0x40 // Version 4
	b[8] = (b[8] & 0x3f) | 0x80 // RFC 4122 variant
	return fmt.Sprintf("%08x-%04x-%04x-%04x-%012x", b[0:4], b[4:6], b[6:8], b[8:10], b[10:16])
}
