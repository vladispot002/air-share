package room

import (
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"strings"
)

// CrockfordBase32Alphabet defines the unambiguous 32-character set (excludes I, L, O, U).
const CrockfordBase32Alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"

// GenerateRoomCode generates a formatted 8-character Crockford Base32 room code (DROP-XXXX-XXXX).
func GenerateRoomCode() (string, error) {
	bytes := make([]byte, 8)
	if _, err := rand.Read(bytes); err != nil {
		return "", fmt.Errorf("crypto/rand error: %w", err)
	}

	var sb strings.Builder
	sb.WriteString("DROP-")
	for i := 0; i < 8; i++ {
		idx := bytes[i] % 32
		sb.WriteByte(CrockfordBase32Alphabet[idx])
		if i == 3 {
			sb.WriteByte('-')
		}
	}

	return sb.String(), nil
}

// NormalizeRoomCode cleanses, standardizes, and corrects common transcription confusions.
func NormalizeRoomCode(input string) (string, error) {
	str := strings.TrimSpace(input)
	// Support URLs with hash fragments or paths (e.g. https://landrop-server.duckdns.org/#DROP-XXXX-XXXX)
	if hashIdx := strings.Index(str, "#"); hashIdx != -1 {
		str = str[hashIdx+1:]
	} else if strings.HasPrefix(str, "http://") || strings.HasPrefix(str, "https://") {
		if lastSlash := strings.LastIndex(str, "/"); lastSlash != -1 && lastSlash+1 < len(str) {
			str = str[lastSlash+1:]
		}
	}

	raw := strings.ToUpper(str)
	// Strip all hyphens, spaces, tabs, and newlines
	raw = strings.ReplaceAll(raw, "-", "")
	raw = strings.ReplaceAll(raw, " ", "")
	raw = strings.ReplaceAll(raw, "\t", "")
	raw = strings.ReplaceAll(raw, "\r", "")
	raw = strings.ReplaceAll(raw, "\n", "")

	// If prefixed with DROP and length is 12 (4 for DROP + 8 code chars), strip prefix once
	if strings.HasPrefix(raw, "DROP") && len(raw) == 12 {
		raw = raw[4:]
	}

	// Fix common transcription mistakes and validate characters
	var sb strings.Builder
	for _, ch := range raw {
		switch ch {
		case 'O':
			sb.WriteByte('0')
		case 'I', 'L':
			sb.WriteByte('1')
		default:
			if strings.ContainsRune(CrockfordBase32Alphabet, ch) {
				sb.WriteRune(ch)
			} else {
				return "", fmt.Errorf("invalid character '%c' in room code", ch)
			}
		}
	}

	canonical := sb.String()
	if len(canonical) != 8 {
		return "", fmt.Errorf("invalid room code length: expected 8 characters, got %d", len(canonical))
	}

	return fmt.Sprintf("DROP-%s-%s", canonical[0:4], canonical[4:8]), nil
}

// GenerateRoomToken generates a cryptographically secure 256-bit token for room authorization.
func GenerateRoomToken() (string, error) {
	bytes := make([]byte, 32)
	if _, err := rand.Read(bytes); err != nil {
		return "", err
	}
	return hex.EncodeToString(bytes), nil
}
