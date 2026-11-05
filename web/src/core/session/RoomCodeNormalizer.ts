export const CROCKFORD_BASE32_ALPHABET = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';

/**
 * Validates, cleanses, standardizes, and corrects common transcription confusions
 * in Crockford Base32 room codes (producing canonical DROP-XXXX-XXXX).
 */
export function normalizeRoomCode(input: string): string {
  if (!input) {
    throw new Error('Room code cannot be empty');
  }

  let raw = '';
  for (let i = 0; i < input.length; i++) {
    const ch = input[i];
    if (ch !== '-' && ch !== ' ' && ch !== '\t' && ch !== '\r' && ch !== '\n') {
      raw += ch.toUpperCase();
    }
  }

  if (raw.startsWith('DROP') && raw.length === 12) {
    raw = raw.slice(4);
  }

  let canonical = '';
  for (let i = 0; i < raw.length; i++) {
    const ch = raw[i];
    if (ch === 'O') {
      canonical += '0';
    } else if (ch === 'I' || ch === 'L') {
      canonical += '1';
    } else if (CROCKFORD_BASE32_ALPHABET.includes(ch)) {
      canonical += ch;
    } else {
      throw new Error(`Invalid character '${ch}' in room code`);
    }
  }

  if (canonical.length !== 8) {
    throw new Error(`Invalid room code length: expected 8 characters, got ${canonical.length}`);
  }

  return `DROP-${canonical.slice(0, 4)}-${canonical.slice(4, 8)}`;
}

export function isValidRoomCode(input: string): boolean {
  try {
    normalizeRoomCode(input);
    return true;
  } catch {
    return false;
  }
}

export function tryNormalizeRoomCode(input: string): { success: boolean; code?: string; error?: string } {
  try {
    const code = normalizeRoomCode(input);
    return { success: true, code };
  } catch (err) {
    return { success: false, error: (err as Error).message };
  }
}
