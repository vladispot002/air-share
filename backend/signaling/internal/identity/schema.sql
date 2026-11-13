-- LAN-Drop Identity, Device, Contact & Transfer Metadata Schema (PostgreSQL)
-- Architecture Principle: Transfer-First, Account-Optional, Zero File Content Storage

CREATE TABLE IF NOT EXISTS users (
    user_id VARCHAR(64) PRIMARY KEY,
    landrop_id VARCHAR(32) NOT NULL UNIQUE,
    display_name VARCHAR(128) NOT NULL,
    avatar_url TEXT,
    email VARCHAR(255),
    email_verified BOOLEAN DEFAULT FALSE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    status VARCHAR(32) NOT NULL DEFAULT 'active',
    password_hash VARCHAR(255)
);

ALTER TABLE users ADD COLUMN IF NOT EXISTS password_hash VARCHAR(255);

CREATE INDEX IF NOT EXISTS idx_users_landrop_id ON users (landrop_id);
CREATE INDEX IF NOT EXISTS idx_users_email ON users (email);

CREATE TABLE IF NOT EXISTS auth_identities (
    provider VARCHAR(32) NOT NULL,
    provider_user_id VARCHAR(255) NOT NULL,
    user_id VARCHAR(64) NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    email VARCHAR(255),
    linked_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (provider, provider_user_id)
);

CREATE INDEX IF NOT EXISTS idx_auth_identities_user_id ON auth_identities (user_id);

CREATE TABLE IF NOT EXISTS devices (
    device_id VARCHAR(64) PRIMARY KEY,
    user_id VARCHAR(64) NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    device_name VARCHAR(128) NOT NULL,
    device_type VARCHAR(32) NOT NULL DEFAULT 'desktop',
    platform VARCHAR(32) NOT NULL DEFAULT 'unknown',
    app_version VARCHAR(32),
    public_key TEXT,
    registered_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_seen_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    status VARCHAR(32) NOT NULL DEFAULT 'active',
    presence VARCHAR(32) NOT NULL DEFAULT 'offline'
);

CREATE INDEX IF NOT EXISTS idx_devices_user_id ON devices (user_id);

CREATE TABLE IF NOT EXISTS device_pairing_sessions (
    session_id VARCHAR(64) PRIMARY KEY,
    user_id VARCHAR(64) NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    secret VARCHAR(64) NOT NULL,
    expires_at TIMESTAMPTZ NOT NULL,
    used BOOLEAN DEFAULT FALSE
);

CREATE INDEX IF NOT EXISTS idx_pairing_sessions_user_id ON device_pairing_sessions (user_id);

CREATE TABLE IF NOT EXISTS contacts (
    user_id VARCHAR(64) NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    target_user_id VARCHAR(64) NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    state VARCHAR(32) NOT NULL DEFAULT 'NONE',
    is_favorite BOOLEAN DEFAULT FALSE,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (user_id, target_user_id)
);

CREATE INDEX IF NOT EXISTS idx_contacts_user_id ON contacts (user_id);
CREATE INDEX IF NOT EXISTS idx_contacts_target ON contacts (target_user_id);

CREATE TABLE IF NOT EXISTS recent_interactions (
    user_id VARCHAR(64) NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    target_user_id VARCHAR(64) NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    last_interaction TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_transfer_status VARCHAR(32),
    PRIMARY KEY (user_id, target_user_id)
);

CREATE INDEX IF NOT EXISTS idx_recent_interactions_user_id ON recent_interactions (user_id);

CREATE TABLE IF NOT EXISTS transfer_requests (
    request_id VARCHAR(64) PRIMARY KEY,
    sender_user_id VARCHAR(64),
    sender_landrop_id VARCHAR(32),
    sender_device_id VARCHAR(64),
    sender_device_name VARCHAR(128),
    recipient_user_id VARCHAR(64) NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    target_device_id VARCHAR(64),
    delivery_mode VARCHAR(32) NOT NULL DEFAULT 'ANY_AVAILABLE_DEVICE',
    files_json JSONB NOT NULL,
    total_bytes BIGINT NOT NULL DEFAULT 0,
    state VARCHAR(32) NOT NULL DEFAULT 'AWAITING_APPROVAL',
    claimed_by_device_id VARCHAR(64),
    room_code VARCHAR(16),
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMPTZ NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_transfer_requests_recipient ON transfer_requests (recipient_user_id);
CREATE INDEX IF NOT EXISTS idx_transfer_requests_claimed_by ON transfer_requests (claimed_by_device_id);

CREATE TABLE IF NOT EXISTS transfer_history (
    transfer_id VARCHAR(64) NOT NULL,
    user_id VARCHAR(64) NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    direction VARCHAR(16) NOT NULL, -- upload, download
    peer_user_id VARCHAR(64),
    peer_landrop_id VARCHAR(32),
    peer_device_name VARCHAR(128),
    files_json JSONB NOT NULL,
    total_bytes BIGINT NOT NULL DEFAULT 0,
    status VARCHAR(32) NOT NULL,
    transport_type VARCHAR(32) NOT NULL DEFAULT 'WebRtcDataChannel',
    started_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    completed_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (transfer_id, user_id)
);

CREATE INDEX IF NOT EXISTS idx_transfer_history_user_id ON transfer_history (user_id, completed_at DESC);
