import { QrCode } from './core/session/QrCode';
import { normalizeRoomCode } from './core/session/RoomCodeNormalizer';
import { SignalingClient } from './core/transport/SignalingClient';
import { WebRtcCoordinator, fetchTurnConfig } from './core/transport/WebRtcCoordinator';
import { FileStreamSender } from './core/streaming/FileStreamSender';
import { FileStreamReceiver, ReceivedFileResult } from './core/streaming/FileStreamReceiver';

console.log('LAN-Drop Web Client initialized with Wire Protocol v2');

// Helper: Format bytes
function formatBytes(bytes: number): string {
  if (bytes === 0) return '0 B';
  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return `${(bytes / Math.pow(k, i)).toFixed(i === 0 ? 0 : 1)} ${sizes[i]}`;
}

// Signaling URLs: WebSocket endpoint mounted at /ws/signaling per backend/signaling/internal/server/server.go
function getSignalingWsUrl(): string {
  const isHttps = window.location.protocol === 'https:';
  const proto = isHttps ? 'wss:' : 'ws:';
  const host = window.location.host || 'localhost:8848';
  return `${proto}//${host}/ws/signaling`;
}

function getSignalingHttpUrl(): string {
  const isHttps = window.location.protocol === 'https:';
  const proto = isHttps ? 'https:' : 'http:';
  const host = window.location.host || 'localhost:8848';
  return `${proto}//${host}`;
}

// ADR-015: URL Hash Schema (RFC 3986 fragment never sent over HTTP headers)
function getPairingUrl(roomCode: string): string {
  return `${window.location.origin}${window.location.pathname}#${roomCode}`;
}

// DOM Elements
const hostBtn = document.getElementById('hostBtn') as HTMLButtonElement | null;
const joinBtn = document.getElementById('joinBtn') as HTMLButtonElement | null;
const roomInput = document.getElementById('roomCodeInput') as HTMLInputElement | null;
const statusDot = document.getElementById('statusDot');
const statusText = document.getElementById('statusText');
const dropzone = document.getElementById('dropzone');
const fileInput = document.getElementById('fileInput') as HTMLInputElement | null;

const transferCard = document.getElementById('transferCard');
const transferFileName = document.getElementById('transferFileName');
const transferSpeed = document.getElementById('transferSpeed');
const transferProgressBar = document.getElementById('transferProgressBar') as HTMLProgressElement | null;
const transferPercent = document.getElementById('transferPercent');
const transferStatusText = document.getElementById('transferStatusText');
const downloadsContainer = document.getElementById('downloadsContainer');

// Modal Elements
const qrModal = document.getElementById('qrModal');
const qrContainer = document.getElementById('qrContainer');
const qrCodeText = document.getElementById('qrCodeText');
const qrStatus = document.getElementById('qrStatus');
const copyCodeBtn = document.getElementById('copyCodeBtn') as HTMLButtonElement | null;
const copyLinkBtn = document.getElementById('copyLinkBtn') as HTMLButtonElement | null;
const closeModalBtn = document.getElementById('closeModalBtn') as HTMLButtonElement | null;

// Session State
let signalingClient: SignalingClient | null = null;
let coordinator: WebRtcCoordinator | null = null;
let activeChannel: RTCDataChannel | null = null;
let activeRoomCode: string | null = null;
let isBusy = false;

// FIFO Send Queue to prevent concurrent frame collision over single RTCDataChannel (Defect 7)
const sendQueue: File[] = [];
let isSending = false;

function setStatus(text: string, state: 'ready' | 'active' | 'connected' | 'error' = 'ready') {
  if (statusText) statusText.textContent = text;
  if (statusDot) {
    statusDot.className = 'status-dot';
    if (state === 'active') statusDot.classList.add('active');
    else if (state === 'connected') statusDot.classList.add('connected');
    else if (state === 'error') statusDot.classList.add('error');
  }
}

function showTransferCard(fileName: string, totalBytes: number, direction: 'Sending' | 'Receiving') {
  if (transferCard) transferCard.classList.add('visible');
  if (transferFileName) transferFileName.textContent = `${direction}: ${fileName} (${formatBytes(totalBytes)})`;
  if (transferSpeed) transferSpeed.textContent = '0.0 MB/s';
  if (transferProgressBar) {
    transferProgressBar.value = 0;
    transferProgressBar.max = totalBytes > 0 ? totalBytes : 1;
  }
  if (transferPercent) transferPercent.textContent = '0%';
  if (transferStatusText) transferStatusText.textContent = `${direction} file...`;
}

function updateTransferProgress(current: number, total: number, startTime: number, direction: 'Sending' | 'Receiving') {
  if (transferProgressBar) transferProgressBar.value = current;
  const pct = total > 0 ? Math.min(100, Math.round((current / total) * 100)) : 100;
  if (transferPercent) transferPercent.textContent = `${pct}%`;

  const elapsedSec = (performance.now() - startTime) / 1000;
  if (elapsedSec > 0 && transferSpeed) {
    const bytesPerSec = current / elapsedSec;
    transferSpeed.textContent = `${(bytesPerSec / (1024 * 1024)).toFixed(1)} MB/s`;
  }
  if (transferStatusText) {
    transferStatusText.textContent = `${direction} (${formatBytes(current)} / ${formatBytes(total)})...`;
  }
}

function hideTransferCard() {
  if (transferCard) transferCard.classList.remove('visible');
}

function openQrModal(roomCode: string, pairingUrl: string) {
  if (!qrModal || !qrContainer || !qrCodeText || !qrStatus) return;
  const qr = QrCode.encodeText(pairingUrl);
  qrContainer.innerHTML = qr.toSvgString(4, '#000000', '#ffffff');
  qrCodeText.textContent = roomCode;
  qrStatus.textContent = 'Waiting for peer to scan or connect...';
  qrModal.classList.add('active');
}

function closeQrModal() {
  if (qrModal) qrModal.classList.remove('active');
}

if (copyCodeBtn) {
  copyCodeBtn.onclick = () => {
    if (activeRoomCode) {
      navigator.clipboard.writeText(activeRoomCode);
      if (qrStatus) qrStatus.textContent = '✓ Room code copied to clipboard!';
    }
  };
}

if (copyLinkBtn) {
  copyLinkBtn.onclick = () => {
    if (activeRoomCode) {
      navigator.clipboard.writeText(getPairingUrl(activeRoomCode));
      if (qrStatus) qrStatus.textContent = '✓ Pairing link copied to clipboard!';
    }
  };
}

if (closeModalBtn) {
  closeModalBtn.onclick = closeQrModal;
}

// Continuous Receiver Loop for RTCDataChannel
async function startReceivingLoop(channel: RTCDataChannel) {
  while (channel.readyState === 'open') {
    try {
      let startTime = 0;
      const receiver = new FileStreamReceiver(channel, {
        onFileHeader: (hdr) => {
          showTransferCard(hdr.fileName, Number(hdr.fileSize), 'Receiving');
          startTime = performance.now();
          return true; // auto-accept incoming transfers in web client
        },
        onProgress: (received, total) => {
          updateTransferProgress(received, total, startTime, 'Receiving');
        },
      });

      const res = await receiver.receiveFile();
      onFileReceived(res);
    } catch (err) {
      if (channel.readyState === 'open') {
        console.error('File receive error:', err);
        hideTransferCard();
      }
      break;
    }
  }
}

function onFileReceived(res: ReceivedFileResult) {
  hideTransferCard();
  const blob = res.blob || new Blob();
  const downloadUrl = URL.createObjectURL(blob);

  // Auto trigger download
  const a = document.createElement('a');
  a.href = downloadUrl;
  a.download = res.fileName;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);

  // Safely display in downloads list without raw HTML entity expansion
  if (downloadsContainer) {
    const item = document.createElement('div');
    item.className = 'download-item';

    const info = document.createElement('div');
    info.className = 'download-info';

    const nameSpan = document.createElement('span');
    nameSpan.className = 'download-name';
    nameSpan.textContent = `📄 ${res.fileName}`;

    const metaSpan = document.createElement('span');
    metaSpan.className = 'download-meta';
    metaSpan.innerHTML = `${formatBytes(res.size)} • <span class="verified-badge">✓ SHA-256 Verified</span>`;

    info.appendChild(nameSpan);
    info.appendChild(metaSpan);

    const saveBtn = document.createElement('a');
    saveBtn.href = downloadUrl;
    saveBtn.download = res.fileName;
    saveBtn.className = 'btn-secondary';
    saveBtn.style.cssText = 'padding: 0.4rem 0.8rem; text-decoration: none; font-size: 0.8rem;';
    saveBtn.textContent = 'Save Again';

    item.appendChild(info);
    item.appendChild(saveBtn);
    downloadsContainer.prepend(item);
  }
}

function setupDataChannel(channel: RTCDataChannel, roomCode: string) {
  activeChannel = channel;
  activeRoomCode = roomCode;
  channel.binaryType = 'arraybuffer';

  setStatus(`Connected (${roomCode})`, 'connected');
  if (qrStatus) {
    qrStatus.textContent = '✓ Peer connected! Ready for transfers.';
  }

  channel.onclose = () => {
    setStatus('Peer disconnected', 'ready');
    activeChannel = null;
    coordinator?.close();
  };

  channel.onerror = (e) => {
    console.error('DataChannel error:', e);
    setStatus('Connection error', 'error');
  };

  startReceivingLoop(channel);

  // Process any pending items in the send queue
  processSendQueue();
}

// Sequential Sender using FIFO queue
async function processSendQueue() {
  if (isSending || sendQueue.length === 0) return;
  if (!activeChannel || activeChannel.readyState !== 'open') return;

  isSending = true;
  const file = sendQueue.shift()!;
  try {
    showTransferCard(file.name, file.size, 'Sending');
    const startTime = performance.now();
    const sender = new FileStreamSender(activeChannel, {
      onHashProgress: (hashed, total) => {
        if (transferStatusText) {
          transferStatusText.textContent = `Computing SHA-256 (${Math.round((hashed / total) * 100)}%)...`;
        }
      },
      onProgress: (sent, total) => {
        updateTransferProgress(sent, total, startTime, 'Sending');
      },
    });

    await sender.sendFile(file);
    if (transferStatusText) {
      transferStatusText.textContent = '✓ Transfer completed successfully!';
    }
    setTimeout(() => {
      hideTransferCard();
    }, 2500);
  } catch (err) {
    console.error('Failed to send file:', err);
    if (transferStatusText) {
      transferStatusText.textContent = `Transfer failed: ${(err as Error).message}`;
    }
  } finally {
    isSending = false;
    if (sendQueue.length > 0) {
      processSendQueue();
    }
  }
}

function sendFile(file: File) {
  sendQueue.push(file);
  if (!activeChannel || activeChannel.readyState !== 'open') {
    hostRoomFlow();
  } else {
    processSendQueue();
  }
}

async function hostRoomFlow() {
  if (isBusy) return;
  isBusy = true;
  setStatus('Connecting to signaling...', 'active');

  try {
    if (coordinator) coordinator.close();
    if (signalingClient) signalingClient.close();

    signalingClient = new SignalingClient();
    await signalingClient.connect(getSignalingWsUrl());

    setStatus('Creating room...', 'active');
    const created = await signalingClient.createRoom();
    activeRoomCode = created.room_code;

    const pairingUrl = getPairingUrl(created.room_code);
    openQrModal(created.room_code, pairingUrl);

    let rtcConfig: RTCConfiguration = { iceServers: [{ urls: 'stun:stun.l.google.com:19302' }] };
    try {
      rtcConfig = await fetchTurnConfig(getSignalingHttpUrl(), created.room_token, signalingClient.id);
    } catch {
      // Use STUN fallback
    }

    coordinator = new WebRtcCoordinator(signalingClient, rtcConfig);
    setStatus('Waiting for peer to connect...', 'active');

    const channel = await coordinator.startInitiator();
    setupDataChannel(channel, created.room_code);
  } catch (err) {
    console.error('Host room failed:', err);
    setStatus(`Host failed: ${(err as Error).message}`, 'error');
  } finally {
    isBusy = false;
  }
}

async function joinRoomFlow(rawCode: string) {
  if (isBusy) return;
  let code: string;
  try {
    code = normalizeRoomCode(rawCode);
  } catch (err) {
    alert(`Invalid room code: ${(err as Error).message}`);
    return;
  }

  isBusy = true;
  setStatus(`Connecting to room ${code}...`, 'active');

  try {
    if (coordinator) coordinator.close();
    if (signalingClient) signalingClient.close();

    signalingClient = new SignalingClient();
    await signalingClient.connect(getSignalingWsUrl());

    const joined = await signalingClient.joinRoom(code);
    activeRoomCode = code;

    let rtcConfig: RTCConfiguration = { iceServers: [{ urls: 'stun:stun.l.google.com:19302' }] };
    if (joined.room_token) {
      try {
        rtcConfig = await fetchTurnConfig(getSignalingHttpUrl(), joined.room_token, signalingClient.id);
      } catch {
        // Fallback STUN
      }
    }

    coordinator = new WebRtcCoordinator(signalingClient, rtcConfig);
    setStatus('Negotiating WebRTC with host...', 'active');

    const channel = await coordinator.startJoiner();
    setupDataChannel(channel, code);
  } catch (err) {
    console.error('Join room failed:', err);
    setStatus(`Join failed: ${(err as Error).message}`, 'error');
  } finally {
    isBusy = false;
  }
}

// Optical Pairing URL Hash Auto-Join (ADR-015)
function checkUrlHash() {
  const hash = window.location.hash.replace(/^#/, '').trim();
  if (hash) {
    try {
      const canonical = normalizeRoomCode(hash);
      if (roomInput) roomInput.value = canonical;
      joinRoomFlow(canonical);
    } catch {
      // Ignored if hash is not a room code
    }
  }
}

// Event Listeners
if (hostBtn) {
  hostBtn.onclick = () => hostRoomFlow();
}

if (joinBtn && roomInput) {
  joinBtn.onclick = () => {
    const val = roomInput.dataset.normalized || roomInput.value;
    if (val) joinRoomFlow(val);
  };

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

  roomInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
      const val = roomInput.dataset.normalized || roomInput.value;
      if (val) joinRoomFlow(val);
    }
  });
}

// Dropzone & File Input
if (dropzone && fileInput) {
  dropzone.onclick = () => fileInput.click();
  fileInput.onchange = () => {
    if (fileInput.files && fileInput.files.length > 0) {
      for (let i = 0; i < fileInput.files.length; i++) {
        sendFile(fileInput.files[i]);
      }
    }
  };
  dropzone.ondragover = (e) => {
    e.preventDefault();
    dropzone.classList.add('dragover');
  };
  dropzone.ondragleave = () => {
    dropzone.classList.remove('dragover');
  };
  dropzone.ondrop = (e) => {
    e.preventDefault();
    dropzone.classList.remove('dragover');
    if (e.dataTransfer && e.dataTransfer.files.length > 0) {
      for (let i = 0; i < e.dataTransfer.files.length; i++) {
        sendFile(e.dataTransfer.files[i]);
      }
    }
  };
}

// Check URL Hash on load & on hashchange
checkUrlHash();
window.addEventListener('hashchange', checkUrlHash);
