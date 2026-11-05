import { normalizeRoomCode } from './core/session/RoomCodeNormalizer';

console.log('LAN-Drop Web Client initialized with Wire Protocol v2');

const roomInput = document.getElementById('roomCodeInput') as HTMLInputElement | null;
const joinBtn = document.getElementById('joinBtn') as HTMLButtonElement | null;

if (roomInput && joinBtn) {
  roomInput.addEventListener('input', () => {
    try {
      if (roomInput.value.trim().length >= 8) {
        const canonical = normalizeRoomCode(roomInput.value);
        roomInput.dataset.normalized = canonical;
      }
    } catch {
      delete roomInput.dataset.normalized;
    }
  });

  joinBtn.addEventListener('click', () => {
    const code = roomInput.dataset.normalized || roomInput.value;
    try {
      const normalized = normalizeRoomCode(code);
      console.log(`Connecting to room: ${normalized}`);
    } catch (err) {
      alert(`Invalid room code: ${(err as Error).message}`);
    }
  });
}
