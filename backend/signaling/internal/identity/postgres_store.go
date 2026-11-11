package identity

import (
	"database/sql"
	_ "embed"
	"encoding/json"
	"errors"
	"fmt"
	"strings"
	"time"

	_ "github.com/lib/pq"
)

//go:embed schema.sql
var DefaultSchemaSQL string

// PostgresStore implements the Store interface using PostgreSQL.
type PostgresStore struct {
	db *sql.DB
}

// NewPostgresStore creates a new PostgreSQL-backed store.
func NewPostgresStore(db *sql.DB) *PostgresStore {
	return &PostgresStore{db: db}
}

// AutoMigrate applies the given SQL schema idempotently.
func (s *PostgresStore) AutoMigrate(schemaSQL string) error {
	if strings.TrimSpace(schemaSQL) == "" {
		schemaSQL = DefaultSchemaSQL
	}
	_, err := s.db.Exec(schemaSQL)
	return err
}

// DB returns the underlying database handle.
func (s *PostgresStore) DB() *sql.DB {
	return s.db
}

// Ping checks database connectivity.
func (s *PostgresStore) Ping() error {
	return s.db.Ping()
}

// --- Users ---

func (s *PostgresStore) CreateUser(u *UserAccount) error {
	if u.CreatedAt.IsZero() {
		u.CreatedAt = time.Now().UTC()
	}
	if u.Status == "" {
		u.Status = "active"
	}

	query := `
		INSERT INTO users (user_id, landrop_id, display_name, avatar_url, email, email_verified, created_at, status)
		VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
	`
	_, err := s.db.Exec(query, u.UserID, u.LandropID, u.DisplayName, u.AvatarURL, u.Email, u.EmailVerified, u.CreatedAt, u.Status)
	if err != nil {
		if strings.Contains(err.Error(), "unique constraint") || strings.Contains(err.Error(), "duplicate key") {
			return ErrLandropIDTaken
		}
		return err
	}

	for _, idn := range u.AuthIdentities {
		_ = s.LinkAuthIdentity(u.UserID, idn)
	}
	return nil
}

func (s *PostgresStore) GetUserByID(userID string) (*UserAccount, error) {
	query := `
		SELECT user_id, landrop_id, display_name, avatar_url, email, email_verified, created_at, status
		FROM users
		WHERE user_id = $1
	`
	row := s.db.QueryRow(query, userID)
	u := &UserAccount{}
	var email sql.NullString
	var avatar sql.NullString
	err := row.Scan(&u.UserID, &u.LandropID, &u.DisplayName, &avatar, &email, &u.EmailVerified, &u.CreatedAt, &u.Status)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return nil, ErrUserNotFound
		}
		return nil, err
	}
	u.Email = email.String
	u.AvatarURL = avatar.String

	identities, _ := s.GetAuthIdentities(u.UserID)
	u.AuthIdentities = identities
	return u, nil
}

func (s *PostgresStore) GetUserByLandropID(landropID string) (*UserAccount, error) {
	norm := strings.ToLower(strings.TrimSpace(landropID))
	query := `
		SELECT user_id, landrop_id, display_name, avatar_url, email, email_verified, created_at, status
		FROM users
		WHERE LOWER(landrop_id) = $1
	`
	row := s.db.QueryRow(query, norm)
	u := &UserAccount{}
	var email sql.NullString
	var avatar sql.NullString
	err := row.Scan(&u.UserID, &u.LandropID, &u.DisplayName, &avatar, &email, &u.EmailVerified, &u.CreatedAt, &u.Status)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return nil, ErrUserNotFound
		}
		return nil, err
	}
	u.Email = email.String
	u.AvatarURL = avatar.String

	identities, _ := s.GetAuthIdentities(u.UserID)
	u.AuthIdentities = identities
	return u, nil
}

func (s *PostgresStore) GetUserByAuthIdentity(provider, providerUserID string) (*UserAccount, error) {
	query := `
		SELECT u.user_id, u.landrop_id, u.display_name, u.avatar_url, u.email, u.email_verified, u.created_at, u.status
		FROM users u
		JOIN auth_identities a ON u.user_id = a.user_id
		WHERE a.provider = $1 AND a.provider_user_id = $2
	`
	row := s.db.QueryRow(query, provider, providerUserID)
	u := &UserAccount{}
	var email sql.NullString
	var avatar sql.NullString
	err := row.Scan(&u.UserID, &u.LandropID, &u.DisplayName, &avatar, &email, &u.EmailVerified, &u.CreatedAt, &u.Status)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return nil, ErrUserNotFound
		}
		return nil, err
	}
	u.Email = email.String
	u.AvatarURL = avatar.String

	identities, _ := s.GetAuthIdentities(u.UserID)
	u.AuthIdentities = identities
	return u, nil
}

func (s *PostgresStore) UpdateUser(u *UserAccount) error {
	query := `
		UPDATE users
		SET display_name = $1, avatar_url = $2, email = $3, email_verified = $4, status = $5
		WHERE user_id = $6
	`
	res, err := s.db.Exec(query, u.DisplayName, u.AvatarURL, u.Email, u.EmailVerified, u.Status, u.UserID)
	if err != nil {
		return err
	}
	n, _ := res.RowsAffected()
	if n == 0 {
		return ErrUserNotFound
	}
	return nil
}

func (s *PostgresStore) DeleteUser(userID string) error {
	query := `DELETE FROM users WHERE user_id = $1`
	res, err := s.db.Exec(query, userID)
	if err != nil {
		return err
	}
	n, _ := res.RowsAffected()
	if n == 0 {
		return ErrUserNotFound
	}
	return nil
}

func (s *PostgresStore) SearchUsersByLandropID(prefix string, limit int) ([]*UserAccount, error) {
	if limit <= 0 {
		limit = 10
	}
	pattern := "%" + strings.ToLower(strings.TrimSpace(prefix)) + "%"
	query := `
		SELECT user_id, landrop_id, display_name, avatar_url, email, email_verified, created_at, status
		FROM users
		WHERE LOWER(landrop_id) LIKE $1 OR LOWER(display_name) LIKE $1
		ORDER BY landrop_id
		LIMIT $2
	`
	rows, err := s.db.Query(query, pattern, limit)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var users []*UserAccount
	for rows.Next() {
		u := &UserAccount{}
		var email sql.NullString
		var avatar sql.NullString
		if err := rows.Scan(&u.UserID, &u.LandropID, &u.DisplayName, &avatar, &email, &u.EmailVerified, &u.CreatedAt, &u.Status); err != nil {
			return nil, err
		}
		u.Email = email.String
		u.AvatarURL = avatar.String
		users = append(users, u)
	}
	return users, rows.Err()
}

// --- Auth Identities ---

func (s *PostgresStore) LinkAuthIdentity(userID string, identity AuthIdentity) error {
	if identity.LinkedAt.IsZero() {
		identity.LinkedAt = time.Now().UTC()
	}
	query := `
		INSERT INTO auth_identities (provider, provider_user_id, user_id, email, linked_at)
		VALUES ($1, $2, $3, $4, $5)
		ON CONFLICT (provider, provider_user_id) DO UPDATE
		SET user_id = EXCLUDED.user_id, email = EXCLUDED.email, linked_at = EXCLUDED.linked_at
	`
	_, err := s.db.Exec(query, identity.Provider, identity.ProviderUserID, userID, identity.Email, identity.LinkedAt)
	return err
}

func (s *PostgresStore) GetAuthIdentities(userID string) ([]AuthIdentity, error) {
	query := `
		SELECT provider, provider_user_id, email, linked_at
		FROM auth_identities
		WHERE user_id = $1
		ORDER BY linked_at
	`
	rows, err := s.db.Query(query, userID)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var list []AuthIdentity
	for rows.Next() {
		var idn AuthIdentity
		var email sql.NullString
		if err := rows.Scan(&idn.Provider, &idn.ProviderUserID, &email, &idn.LinkedAt); err != nil {
			return nil, err
		}
		idn.Email = email.String
		list = append(list, idn)
	}
	return list, rows.Err()
}

func (s *PostgresStore) UnlinkAuthIdentity(userID, provider string) error {
	query := `DELETE FROM auth_identities WHERE user_id = $1 AND provider = $2`
	_, err := s.db.Exec(query, userID, provider)
	return err
}

// --- Devices ---

func (s *PostgresStore) RegisterDevice(d *Device) error {
	if d.RegisteredAt.IsZero() {
		d.RegisteredAt = time.Now().UTC()
	}
	if d.LastSeenAt.IsZero() {
		d.LastSeenAt = d.RegisteredAt
	}
	if d.Status == "" {
		d.Status = DeviceStatusActive
	}
	if d.Presence == "" {
		d.Presence = PresenceOffline
	}

	query := `
		INSERT INTO devices (device_id, user_id, device_name, device_type, platform, app_version, public_key, registered_at, last_seen_at, status, presence)
		VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11)
		ON CONFLICT (device_id) DO UPDATE
		SET device_name = EXCLUDED.device_name,
		    platform = EXCLUDED.platform,
		    app_version = EXCLUDED.app_version,
		    last_seen_at = EXCLUDED.last_seen_at,
		    status = EXCLUDED.status,
		    presence = EXCLUDED.presence
	`
	_, err := s.db.Exec(query, d.DeviceID, d.UserID, d.DeviceName, d.DeviceType, d.Platform, d.AppVersion, d.PublicKey, d.RegisteredAt, d.LastSeenAt, string(d.Status), string(d.Presence))
	return err
}

func (s *PostgresStore) GetDevice(deviceID string) (*Device, error) {
	query := `
		SELECT device_id, user_id, device_name, device_type, platform, app_version, public_key, registered_at, last_seen_at, status, presence
		FROM devices
		WHERE device_id = $1
	`
	row := s.db.QueryRow(query, deviceID)
	d := &Device{}
	var appVer, pubKey sql.NullString
	var status, presence string
	err := row.Scan(&d.DeviceID, &d.UserID, &d.DeviceName, &d.DeviceType, &d.Platform, &appVer, &pubKey, &d.RegisteredAt, &d.LastSeenAt, &status, &presence)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return nil, ErrDeviceNotFound
		}
		return nil, err
	}
	d.AppVersion = appVer.String
	d.PublicKey = pubKey.String
	d.Status = DeviceStatus(status)
	d.Presence = DevicePresence(presence)
	return d, nil
}

func (s *PostgresStore) GetDevicesByUserID(userID string) ([]*Device, error) {
	query := `
		SELECT device_id, user_id, device_name, device_type, platform, app_version, public_key, registered_at, last_seen_at, status, presence
		FROM devices
		WHERE user_id = $1
		ORDER BY registered_at
	`
	rows, err := s.db.Query(query, userID)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var devices []*Device
	for rows.Next() {
		d := &Device{}
		var appVer, pubKey sql.NullString
		var status, presence string
		if err := rows.Scan(&d.DeviceID, &d.UserID, &d.DeviceName, &d.DeviceType, &d.Platform, &appVer, &pubKey, &d.RegisteredAt, &d.LastSeenAt, &status, &presence); err != nil {
			return nil, err
		}
		d.AppVersion = appVer.String
		d.PublicKey = pubKey.String
		d.Status = DeviceStatus(status)
		d.Presence = DevicePresence(presence)
		devices = append(devices, d)
	}
	return devices, rows.Err()
}

func (s *PostgresStore) UpdateDevice(d *Device) error {
	query := `
		UPDATE devices
		SET device_name = $1, platform = $2, app_version = $3, last_seen_at = $4, status = $5, presence = $6
		WHERE device_id = $7
	`
	res, err := s.db.Exec(query, d.DeviceName, d.Platform, d.AppVersion, d.LastSeenAt, string(d.Status), string(d.Presence), d.DeviceID)
	if err != nil {
		return err
	}
	n, _ := res.RowsAffected()
	if n == 0 {
		return ErrDeviceNotFound
	}
	return nil
}

func (s *PostgresStore) UpdateDevicePresence(deviceID string, presence DevicePresence) error {
	query := `
		UPDATE devices
		SET presence = $1, last_seen_at = $2
		WHERE device_id = $3
	`
	res, err := s.db.Exec(query, string(presence), time.Now().UTC(), deviceID)
	if err != nil {
		return err
	}
	n, _ := res.RowsAffected()
	if n == 0 {
		return ErrDeviceNotFound
	}
	return nil
}

func (s *PostgresStore) RevokeDevice(deviceID string) error {
	query := `
		UPDATE devices
		SET status = 'revoked', presence = 'offline', last_seen_at = $1
		WHERE device_id = $2
	`
	res, err := s.db.Exec(query, time.Now().UTC(), deviceID)
	if err != nil {
		return err
	}
	n, _ := res.RowsAffected()
	if n == 0 {
		return ErrDeviceNotFound
	}
	return nil
}

// --- Device Pairing Sessions ---

func (s *PostgresStore) CreatePairingSession(session *DevicePairingSession) error {
	query := `
		INSERT INTO device_pairing_sessions (session_id, user_id, secret, expires_at, used)
		VALUES ($1, $2, $3, $4, $5)
	`
	_, err := s.db.Exec(query, session.SessionID, session.UserID, session.Secret, session.ExpiresAt, session.Used)
	return err
}

func (s *PostgresStore) GetPairingSession(sessionID string) (*DevicePairingSession, error) {
	query := `
		SELECT session_id, user_id, secret, expires_at, used
		FROM device_pairing_sessions
		WHERE session_id = $1
	`
	row := s.db.QueryRow(query, sessionID)
	sess := &DevicePairingSession{}
	err := row.Scan(&sess.SessionID, &sess.UserID, &sess.Secret, &sess.ExpiresAt, &sess.Used)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return nil, errors.New("pairing session not found")
		}
		return nil, err
	}
	return sess, nil
}

func (s *PostgresStore) MarkPairingSessionUsed(sessionID string) error {
	query := `UPDATE device_pairing_sessions SET used = TRUE WHERE session_id = $1`
	_, err := s.db.Exec(query, sessionID)
	return err
}

// --- Contacts ---

func (s *PostgresStore) UpsertContact(entry *ContactEntry) error {
	if entry.UpdatedAt.IsZero() {
		entry.UpdatedAt = time.Now().UTC()
	}
	query := `
		INSERT INTO contacts (user_id, target_user_id, state, is_favorite, updated_at)
		VALUES ($1, $2, $3, $4, $5)
		ON CONFLICT (user_id, target_user_id) DO UPDATE
		SET state = EXCLUDED.state, is_favorite = EXCLUDED.is_favorite, updated_at = EXCLUDED.updated_at
	`
	_, err := s.db.Exec(query, entry.UserID, entry.TargetUserID, string(entry.State), entry.IsFavorite, entry.UpdatedAt)
	return err
}

func (s *PostgresStore) GetContacts(userID string) ([]*ContactEntry, error) {
	query := `
		SELECT c.user_id, c.target_user_id, u.landrop_id, u.display_name, u.avatar_url, c.state, c.is_favorite, c.updated_at
		FROM contacts c
		JOIN users u ON c.target_user_id = u.user_id
		WHERE c.user_id = $1 AND c.state != 'NONE'
		ORDER BY c.is_favorite DESC, u.display_name
	`
	rows, err := s.db.Query(query, userID)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var contacts []*ContactEntry
	for rows.Next() {
		c := &ContactEntry{}
		var avatar sql.NullString
		var state string
		if err := rows.Scan(&c.UserID, &c.TargetUserID, &c.LandropID, &c.DisplayName, &avatar, &state, &c.IsFavorite, &c.UpdatedAt); err != nil {
			return nil, err
		}
		c.AvatarURL = avatar.String
		c.State = ContactState(state)
		contacts = append(contacts, c)
	}
	return contacts, rows.Err()
}

func (s *PostgresStore) GetContactRelation(userID, targetUserID string) (*ContactEntry, error) {
	query := `
		SELECT c.user_id, c.target_user_id, u.landrop_id, u.display_name, u.avatar_url, c.state, c.is_favorite, c.updated_at
		FROM contacts c
		JOIN users u ON c.target_user_id = u.user_id
		WHERE c.user_id = $1 AND c.target_user_id = $2
	`
	row := s.db.QueryRow(query, userID, targetUserID)
	c := &ContactEntry{}
	var avatar sql.NullString
	var state string
	err := row.Scan(&c.UserID, &c.TargetUserID, &c.LandropID, &c.DisplayName, &avatar, &state, &c.IsFavorite, &c.UpdatedAt)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return &ContactEntry{UserID: userID, TargetUserID: targetUserID, State: ContactNone}, nil
		}
		return nil, err
	}
	c.AvatarURL = avatar.String
	c.State = ContactState(state)
	return c, nil
}

func (s *PostgresStore) DeleteContact(userID, targetUserID string) error {
	query := `DELETE FROM contacts WHERE user_id = $1 AND target_user_id = $2`
	_, err := s.db.Exec(query, userID, targetUserID)
	return err
}

func (s *PostgresStore) SetFavorite(userID, targetUserID string, isFavorite bool) error {
	query := `
		UPDATE contacts
		SET is_favorite = $1, updated_at = $2
		WHERE user_id = $3 AND target_user_id = $4
	`
	res, err := s.db.Exec(query, isFavorite, time.Now().UTC(), userID, targetUserID)
	if err != nil {
		return err
	}
	n, _ := res.RowsAffected()
	if n == 0 {
		return errors.New("contact relation not found")
	}
	return nil
}

// --- Recent Interactions ---

func (s *PostgresStore) RecordInteraction(entry *RecentUserEntry) error {
	if entry.LastInteraction.IsZero() {
		entry.LastInteraction = time.Now().UTC()
	}
	query := `
		INSERT INTO recent_interactions (user_id, target_user_id, last_interaction, last_transfer_status)
		VALUES ($1, $2, $3, $4)
		ON CONFLICT (user_id, target_user_id) DO UPDATE
		SET last_interaction = EXCLUDED.last_interaction, last_transfer_status = EXCLUDED.last_transfer_status
	`
	_, err := s.db.Exec(query, entry.UserID, entry.TargetUserID, entry.LastInteraction, entry.LastTransferStatus)
	return err
}

func (s *PostgresStore) GetRecentUsers(userID string, limit int) ([]*RecentUserEntry, error) {
	if limit <= 0 {
		limit = 10
	}
	query := `
		SELECT r.user_id, r.target_user_id, u.landrop_id, u.display_name, u.avatar_url, r.last_interaction, r.last_transfer_status,
		       (SELECT COUNT(*) FROM devices d WHERE d.user_id = r.target_user_id AND d.status = 'active' AND d.presence = 'online') AS available_devices
		FROM recent_interactions r
		JOIN users u ON r.target_user_id = u.user_id
		WHERE r.user_id = $1
		ORDER BY r.last_interaction DESC
		LIMIT $2
	`
	rows, err := s.db.Query(query, userID, limit)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var recents []*RecentUserEntry
	for rows.Next() {
		re := &RecentUserEntry{}
		var avatar, status sql.NullString
		if err := rows.Scan(&re.UserID, &re.TargetUserID, &re.LandropID, &re.DisplayName, &avatar, &re.LastInteraction, &status, &re.AvailableDevices); err != nil {
			return nil, err
		}
		re.AvatarURL = avatar.String
		re.LastTransferStatus = status.String
		recents = append(recents, re)
	}
	return recents, rows.Err()
}

// --- Transfers & Claims ---

func (s *PostgresStore) CreateTransferRequest(req *TransferRequest) error {
	if req.CreatedAt.IsZero() {
		req.CreatedAt = time.Now().UTC()
	}
	if req.ExpiresAt.IsZero() {
		req.ExpiresAt = req.CreatedAt.Add(5 * time.Minute)
	}
	if req.State == "" {
		req.State = TransferStateAwaiting
	}
	if req.DeliveryMode == "" {
		req.DeliveryMode = DeliveryModeAnyAvailableDevice
	}

	filesBytes, err := json.Marshal(req.Files)
	if err != nil {
		return fmt.Errorf("failed to marshal files json: %w", err)
	}

	query := `
		INSERT INTO transfer_requests (
			request_id, sender_user_id, sender_landrop_id, sender_device_id, sender_device_name,
			recipient_user_id, target_device_id, delivery_mode, files_json, total_bytes, state,
			claimed_by_device_id, room_code, created_at, expires_at
		) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15)
	`
	_, err = s.db.Exec(
		query,
		req.RequestID, req.SenderUserID, req.SenderLandropID, req.SenderDeviceID, req.SenderDeviceName,
		req.RecipientUserID, req.TargetDeviceID, string(req.DeliveryMode), filesBytes, req.TotalBytes,
		string(req.State), req.ClaimedByDeviceID, req.RoomCode, req.CreatedAt, req.ExpiresAt,
	)
	return err
}

func (s *PostgresStore) GetTransferRequest(requestID string) (*TransferRequest, error) {
	query := `
		SELECT request_id, sender_user_id, sender_landrop_id, sender_device_id, sender_device_name,
		       recipient_user_id, target_device_id, delivery_mode, files_json, total_bytes, state,
		       claimed_by_device_id, room_code, created_at, expires_at
		FROM transfer_requests
		WHERE request_id = $1
	`
	row := s.db.QueryRow(query, requestID)
	req := &TransferRequest{}
	var senderUser, senderLandrop, targetDev, claimedDev, roomCode sql.NullString
	var deliveryMode, state string
	var filesBytes []byte

	err := row.Scan(
		&req.RequestID, &senderUser, &senderLandrop, &req.SenderDeviceID, &req.SenderDeviceName,
		&req.RecipientUserID, &targetDev, &deliveryMode, &filesBytes, &req.TotalBytes, &state,
		&claimedDev, &roomCode, &req.CreatedAt, &req.ExpiresAt,
	)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return nil, ErrTransferNotFound
		}
		return nil, err
	}

	req.SenderUserID = senderUser.String
	req.SenderLandropID = senderLandrop.String
	req.TargetDeviceID = targetDev.String
	req.ClaimedByDeviceID = claimedDev.String
	req.RoomCode = roomCode.String
	req.DeliveryMode = DeliveryMode(deliveryMode)
	req.State = TransferState(state)

	if len(filesBytes) > 0 {
		_ = json.Unmarshal(filesBytes, &req.Files)
	}
	return req, nil
}

func (s *PostgresStore) UpdateTransferRequest(req *TransferRequest) error {
	query := `
		UPDATE transfer_requests
		SET state = $1, claimed_by_device_id = $2, room_code = $3
		WHERE request_id = $4
	`
	res, err := s.db.Exec(query, string(req.State), req.ClaimedByDeviceID, req.RoomCode, req.RequestID)
	if err != nil {
		return err
	}
	n, _ := res.RowsAffected()
	if n == 0 {
		return ErrTransferNotFound
	}
	return nil
}

// ClaimTransferRequest atomically claims a transfer request in Mode B using an atomic conditional update.
func (s *PostgresStore) ClaimTransferRequest(requestID, deviceID string) (*TransferRequest, error) {
	tx, err := s.db.Begin()
	if err != nil {
		return nil, err
	}
	defer tx.Rollback()

	// Select with row-level lock
	query := `
		SELECT request_id, sender_user_id, sender_landrop_id, sender_device_id, sender_device_name,
		       recipient_user_id, target_device_id, delivery_mode, files_json, total_bytes, state,
		       claimed_by_device_id, room_code, created_at, expires_at
		FROM transfer_requests
		WHERE request_id = $1
		FOR UPDATE
	`
	row := tx.QueryRow(query, requestID)
	req := &TransferRequest{}
	var senderUser, senderLandrop, targetDev, claimedDev, roomCode sql.NullString
	var deliveryMode, state string
	var filesBytes []byte

	err = row.Scan(
		&req.RequestID, &senderUser, &senderLandrop, &req.SenderDeviceID, &req.SenderDeviceName,
		&req.RecipientUserID, &targetDev, &deliveryMode, &filesBytes, &req.TotalBytes, &state,
		&claimedDev, &roomCode, &req.CreatedAt, &req.ExpiresAt,
	)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return nil, ErrTransferNotFound
		}
		return nil, err
	}

	req.SenderUserID = senderUser.String
	req.SenderLandropID = senderLandrop.String
	req.TargetDeviceID = targetDev.String
	req.ClaimedByDeviceID = claimedDev.String
	req.RoomCode = roomCode.String
	req.DeliveryMode = DeliveryMode(deliveryMode)
	req.State = TransferState(state)

	if len(filesBytes) > 0 {
		_ = json.Unmarshal(filesBytes, &req.Files)
	}

	if time.Now().UTC().After(req.ExpiresAt) {
		req.State = TransferStateExpired
		_, _ = tx.Exec(`UPDATE transfer_requests SET state = 'EXPIRED' WHERE request_id = $1`, requestID)
		_ = tx.Commit()
		return nil, ErrTransferExpired
	}

	if req.ClaimedByDeviceID != "" && req.ClaimedByDeviceID != deviceID {
		return nil, ErrTransferAlreadyClaimed
	}

	req.ClaimedByDeviceID = deviceID
	req.State = TransferStateAccepted

	updateQuery := `
		UPDATE transfer_requests
		SET state = 'ACCEPTED', claimed_by_device_id = $1
		WHERE request_id = $2
	`
	if _, err := tx.Exec(updateQuery, deviceID, requestID); err != nil {
		return nil, err
	}

	if err := tx.Commit(); err != nil {
		return nil, err
	}
	return req, nil
}

// --- Metadata History ---

func (s *PostgresStore) RecordHistory(rec *TransferHistoryRecord) error {
	if rec.StartedAt.IsZero() {
		rec.StartedAt = time.Now().UTC()
	}
	if rec.CompletedAt.IsZero() {
		rec.CompletedAt = time.Now().UTC()
	}
	if rec.TransportType == "" {
		rec.TransportType = "WebRtcDataChannel"
	}

	filesBytes, err := json.Marshal(rec.Files)
	if err != nil {
		return fmt.Errorf("failed to marshal history files json: %w", err)
	}

	query := `
		INSERT INTO transfer_history (
			transfer_id, user_id, direction, peer_user_id, peer_landrop_id, peer_device_name,
			files_json, total_bytes, status, transport_type, started_at, completed_at
		) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12)
		ON CONFLICT (transfer_id, user_id) DO UPDATE
		SET status = EXCLUDED.status, completed_at = EXCLUDED.completed_at
	`
	_, err = s.db.Exec(
		query,
		rec.TransferID, rec.UserID, rec.Direction, rec.PeerUserID, rec.PeerLandropID, rec.PeerDeviceName,
		filesBytes, rec.TotalBytes, string(rec.Status), rec.TransportType, rec.StartedAt, rec.CompletedAt,
	)
	return err
}

func (s *PostgresStore) GetHistory(userID string, limit int) ([]*TransferHistoryRecord, error) {
	if limit <= 0 {
		limit = 20
	}
	query := `
		SELECT transfer_id, user_id, direction, peer_user_id, peer_landrop_id, peer_device_name,
		       files_json, total_bytes, status, transport_type, started_at, completed_at
		FROM transfer_history
		WHERE user_id = $1
		ORDER BY completed_at DESC
		LIMIT $2
	`
	rows, err := s.db.Query(query, userID, limit)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var history []*TransferHistoryRecord
	for rows.Next() {
		rec := &TransferHistoryRecord{}
		var peerUser, peerLandrop, peerDev sql.NullString
		var status string
		var filesBytes []byte

		if err := rows.Scan(
			&rec.TransferID, &rec.UserID, &rec.Direction, &peerUser, &peerLandrop, &peerDev,
			&filesBytes, &rec.TotalBytes, &status, &rec.TransportType, &rec.StartedAt, &rec.CompletedAt,
		); err != nil {
			return nil, err
		}
		rec.PeerUserID = peerUser.String
		rec.PeerLandropID = peerLandrop.String
		rec.PeerDeviceName = peerDev.String
		rec.Status = TransferState(status)
		if len(filesBytes) > 0 {
			_ = json.Unmarshal(filesBytes, &rec.Files)
		}
		history = append(history, rec)
	}
	return history, rows.Err()
}

func (s *PostgresStore) DeleteHistoryItem(userID, transferID string) error {
	query := `DELETE FROM transfer_history WHERE user_id = $1 AND transfer_id = $2`
	_, err := s.db.Exec(query, userID, transferID)
	return err
}

func (s *PostgresStore) ClearHistory(userID string) error {
	query := `DELETE FROM transfer_history WHERE user_id = $1`
	_, err := s.db.Exec(query, userID)
	return err
}
