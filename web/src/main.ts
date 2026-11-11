import { QrCode } from './core/session/QrCode';
import { normalizeRoomCode } from './core/session/RoomCodeNormalizer';
import { SignalingClient } from './core/transport/SignalingClient';
import { WebRtcCoordinator, fetchTurnConfig } from './core/transport/WebRtcCoordinator';
import { FileStreamSender } from './core/streaming/FileStreamSender';
import { FileStreamReceiver, ReceivedFileResult } from './core/streaming/FileStreamReceiver';
import { IdentityClient, IdentityState } from './core/identity/IdentityClient';
import { ContactManager } from './core/identity/ContactManager';
import { DeviceManager } from './core/identity/DeviceManager';
import { TransferCoordinatorClient } from './core/identity/TransferCoordinatorClient';
import { TransferRequest } from './core/identity/types';

console.log('LAN-Drop Web Client initialized with Wire Protocol v2 & Identity Ecosystem');

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
    if (signalingClient.getWebSocket()) { identityClient.bindWebSocket(signalingClient.getWebSocket()!); }

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
    if (signalingClient.getWebSocket()) { identityClient.bindWebSocket(signalingClient.getWebSocket()!); }

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

// --- Identity Ecosystem State & DOM Hooks ---
const identityClient = new IdentityClient(getSignalingHttpUrl());
const contactManager = new ContactManager(identityClient, getSignalingHttpUrl());
const deviceManager = new DeviceManager(identityClient, getSignalingHttpUrl());
const transferCoordinator = new TransferCoordinatorClient(identityClient, getSignalingHttpUrl());

const identityIcon = document.getElementById('identityIcon');
const identityName = document.getElementById('identityName');
const guestBadge = document.getElementById('guestBadge');
const openAuthBtn = document.getElementById('openAuthBtn') as HTMLButtonElement | null;
const openContactsBtn = document.getElementById('openContactsBtn') as HTMLButtonElement | null;
const openDevicesBtn = document.getElementById('openDevicesBtn') as HTMLButtonElement | null;
const signOutBtn = document.getElementById('signOutBtn') as HTMLButtonElement | null;

const authModal = document.getElementById('authModal');
const oauthGoogleBtn = document.getElementById('oauthGoogleBtn') as HTMLButtonElement | null;
const oauthGithubBtn = document.getElementById('oauthGithubBtn') as HTMLButtonElement | null;
const oauthAppleBtn = document.getElementById('oauthAppleBtn') as HTMLButtonElement | null;
const oauthMicrosoftBtn = document.getElementById('oauthMicrosoftBtn') as HTMLButtonElement | null;
const authEmailInput = document.getElementById('authEmailInput') as HTMLInputElement | null;
const authNameInput = document.getElementById('authNameInput') as HTMLInputElement | null;
const authSubmitBtn = document.getElementById('authSubmitBtn') as HTMLButtonElement | null;
const closeAuthModalBtn = document.getElementById('closeAuthModalBtn') as HTMLButtonElement | null;

const contactsModal = document.getElementById('contactsModal');
const contactSearchInput = document.getElementById('contactSearchInput') as HTMLInputElement | null;
const addContactBtn = document.getElementById('addContactBtn') as HTMLButtonElement | null;
const contactsList = document.getElementById('contactsList');
const closeContactsModalBtn = document.getElementById('closeContactsModalBtn') as HTMLButtonElement | null;

const devicesModal = document.getElementById('devicesModal');
const devicesList = document.getElementById('devicesList');
const pairNewDeviceBtn = document.getElementById('pairNewDeviceBtn') as HTMLButtonElement | null;
const closeDevicesModalBtn = document.getElementById('closeDevicesModalBtn') as HTMLButtonElement | null;

const incomingTransferModal = document.getElementById('incomingTransferModal');
const incomingTransferSender = document.getElementById('incomingTransferSender');
const incomingTransferFiles = document.getElementById('incomingTransferFiles');
const incomingTransferSize = document.getElementById('incomingTransferSize');
const acceptTransferBtn = document.getElementById('acceptTransferBtn') as HTMLButtonElement | null;
const declineTransferBtn = document.getElementById('declineTransferBtn') as HTMLButtonElement | null;

let currentIncomingRequest: TransferRequest | null = null;
let backgroundSignaling: SignalingClient | null = null;

function renderIdentityBar(s: IdentityState) {
  if (s.isGuest) {
    if (identityIcon) identityIcon.textContent = '👤';
    if (identityName) identityName.textContent = 'Guest Mode';
    if (guestBadge) {
      guestBadge.style.display = 'inline';
      guestBadge.textContent = 'Instant Entry';
    }
    if (openAuthBtn) openAuthBtn.style.display = 'inline-flex';
    if (openContactsBtn) openContactsBtn.style.display = 'none';
    if (openDevicesBtn) openDevicesBtn.style.display = 'none';
    if (signOutBtn) signOutBtn.style.display = 'none';
  } else {
    if (identityIcon) identityIcon.textContent = '🛡️';
    if (identityName) identityName.textContent = s.user?.landrop_id || s.user?.display_name || 'User';
    if (guestBadge) guestBadge.style.display = 'none';
    if (openAuthBtn) openAuthBtn.style.display = 'none';
    if (openContactsBtn) openContactsBtn.style.display = 'inline-flex';
    if (openDevicesBtn) openDevicesBtn.style.display = 'inline-flex';
    if (signOutBtn) signOutBtn.style.display = 'inline-flex';
  }
}

identityClient.onAuthStateChanged((s) => {
  renderIdentityBar(s);
  setupBackgroundSignaling();
});

async function setupBackgroundSignaling() {
  const s = identityClient.getState();
  if (s.isGuest || !s.token) {
    if (backgroundSignaling) {
      backgroundSignaling.close();
      backgroundSignaling = null;
    }
    return;
  }
  if (backgroundSignaling) return;

  try {
    backgroundSignaling = new SignalingClient();
    await backgroundSignaling.connect(getSignalingWsUrl());
    if (backgroundSignaling.getWebSocket()) {
      identityClient.bindWebSocket(backgroundSignaling.getWebSocket()!);
    }

    backgroundSignaling.onRawEnvelope((env) => {
      if (env.action === 'transfer.requested') {
        const req = env.payload as TransferRequest;
        currentIncomingRequest = req;
        if (incomingTransferModal && incomingTransferSender && incomingTransferFiles && incomingTransferSize) {
          incomingTransferSender.textContent = `${req.sender_landrop_id || 'Guest'} (${req.sender_device_name || 'Device'}) wants to send files`;
          incomingTransferFiles.textContent = `Files: ${req.files?.map(f => f.file_name).join(', ') || 'N/A'}`;
          incomingTransferSize.textContent = `Total Size: ${formatBytes(req.total_bytes || 0)}`;
          incomingTransferModal.classList.add('visible');
        }
      } else if (env.action === 'transfer.claimed') {
        if (currentIncomingRequest && currentIncomingRequest.request_id === env.payload?.request_id) {
          if (incomingTransferModal) incomingTransferModal.classList.remove('visible');
          setStatus(`Transfer claimed by another device (${env.payload?.claimed_by_device_id})`, 'ready');
        }
      } else if (env.action === 'transfer.declined') {
        setStatus('Transfer declined by recipient', 'error');
      }
    });
  } catch (err) {
    console.warn('Background signaling failed:', err);
  }
}

// Modal Toggle Helpers
function toggleModal(el: HTMLElement | null, show: boolean) {
  if (!el) return;
  if (show) el.classList.add('visible');
  else el.classList.remove('visible');
}

if (openAuthBtn) openAuthBtn.onclick = () => toggleModal(authModal, true);
if (closeAuthModalBtn) closeAuthModalBtn.onclick = () => toggleModal(authModal, false);

if (signOutBtn) {
  signOutBtn.onclick = () => {
    identityClient.logout();
    setStatus('Signed out. Switched to guest mode.', 'ready');
  };
}

async function handleLogin(provider: string, email?: string, displayName?: string) {
  try {
    setStatus('Signing in...', 'active');
    await identityClient.loginWithProvider({
      provider,
      email: email || `${provider}_user@landrop.app`,
      display_name: displayName || `${provider.toUpperCase()} User`,
    });
    toggleModal(authModal, false);
    setStatus(`Signed in as ${identityClient.getState().user?.landrop_id}`, 'connected');
  } catch (err) {
    alert(`Sign in failed: ${(err as Error).message}`);
    setStatus(`Sign in failed`, 'error');
  }
}

if (oauthGoogleBtn) oauthGoogleBtn.onclick = () => handleLogin('google');
if (oauthGithubBtn) oauthGithubBtn.onclick = () => handleLogin('github');
if (oauthAppleBtn) oauthAppleBtn.onclick = () => handleLogin('apple');
if (oauthMicrosoftBtn) oauthMicrosoftBtn.onclick = () => handleLogin('microsoft');

if (authSubmitBtn && authEmailInput) {
  authSubmitBtn.onclick = () => {
    const val = authEmailInput.value.trim();
    if (!val) {
      alert('Please enter an email or username');
      return;
    }
    const name = authNameInput?.value.trim() || val.split('@')[0];
    handleLogin('email', val, name);
  };
}

// Contacts Modal
if (openContactsBtn) {
  openContactsBtn.onclick = async () => {
    toggleModal(contactsModal, true);
    await refreshContactsList();
  };
}
if (closeContactsModalBtn) closeContactsModalBtn.onclick = () => toggleModal(contactsModal, false);

async function refreshContactsList() {
  if (!contactsList) return;
  try {
    const contacts = await contactManager.getContacts();
    if (contacts.length === 0) {
      contactsList.innerHTML = '<div class="desc" style="text-align: center; padding: 1rem;">No contacts yet. Add someone by their @landrop_id!</div>';
      return;
    }
    contactsList.innerHTML = '';
    for (const c of contacts) {
      const row = document.createElement('div');
      row.className = 'item-row';
      row.innerHTML = `
        <div class="item-info">
          <span class="item-name">${c.display_name || c.landrop_id}</span>
          <span class="item-sub">${c.landrop_id} (${c.state})</span>
        </div>
        <div>
          ${c.state === 'ACCEPTED' ? `<button class="btn-primary identity-btn send-to-contact" data-user="${c.target_user_id}">Send File</button>` : ''}
          ${c.state === 'REQUEST_RECEIVED' ? `<button class="btn-primary identity-btn accept-contact" data-user="${c.target_user_id}">Accept</button>` : ''}
        </div>
      `;
      contactsList.appendChild(row);
    }

    contactsList.querySelectorAll('.accept-contact').forEach((btn) => {
      (btn as HTMLButtonElement).onclick = async (e) => {
        const uid = (e.target as HTMLButtonElement).dataset.user;
        if (uid) {
          await contactManager.acceptContactRequest(uid);
          await refreshContactsList();
        }
      };
    });

    contactsList.querySelectorAll('.send-to-contact').forEach((btn) => {
      (btn as HTMLButtonElement).onclick = async (e) => {
        const uid = (e.target as HTMLButtonElement).dataset.user;
        if (uid) {
          toggleModal(contactsModal, false);
          if (fileInput) fileInput.click();
        }
      };
    });
  } catch (err) {
    contactsList.innerHTML = `<div class="desc" style="color: var(--danger); padding: 1rem;">Failed to load contacts: ${(err as Error).message}</div>`;
  }
}

if (addContactBtn && contactSearchInput) {
  addContactBtn.onclick = async () => {
    const target = contactSearchInput.value.trim();
    if (!target) return;
    try {
      await contactManager.sendContactRequest(target);
      contactSearchInput.value = '';
      alert(`Contact request sent to ${target}!`);
      await refreshContactsList();
    } catch (err) {
      alert(`Failed to add contact: ${(err as Error).message}`);
    }
  };
}

// Devices Modal
if (openDevicesBtn) {
  openDevicesBtn.onclick = async () => {
    toggleModal(devicesModal, true);
    await refreshDevicesList();
  };
}
if (closeDevicesModalBtn) closeDevicesModalBtn.onclick = () => toggleModal(devicesModal, false);

async function refreshDevicesList() {
  if (!devicesList) return;
  try {
    const devices = await deviceManager.getDevices();
    if (devices.length === 0) {
      devicesList.innerHTML = '<div class="desc" style="text-align: center; padding: 1rem;">No registered devices.</div>';
      return;
    }
    devicesList.innerHTML = '';
    for (const d of devices) {
      const row = document.createElement('div');
      row.className = 'item-row';
      row.innerHTML = `
        <div class="item-info">
          <span class="item-name">${d.device_name} (${d.platform})</span>
          <span class="item-sub">Status: ${d.status} • Presence: ${d.presence}</span>
        </div>
        <div>
          ${d.status === 'active' ? `<button class="btn-secondary identity-btn revoke-dev" data-dev="${d.device_id}">Revoke</button>` : ''}
        </div>
      `;
      devicesList.appendChild(row);
    }
    devicesList.querySelectorAll('.revoke-dev').forEach((btn) => {
      (btn as HTMLButtonElement).onclick = async (e) => {
        const did = (e.target as HTMLButtonElement).dataset.dev;
        if (did) {
          await deviceManager.revokeDevice(did);
          await refreshDevicesList();
        }
      };
    });
  } catch (err) {
    devicesList.innerHTML = `<div class="desc" style="color: var(--danger); padding: 1rem;">Failed to load devices: ${(err as Error).message}</div>`;
  }
}

if (pairNewDeviceBtn) {
  pairNewDeviceBtn.onclick = async () => {
    try {
      const sess = await deviceManager.createPairingSession();
      toggleModal(devicesModal, false);
      const pairingUrl = `${window.location.origin}${window.location.pathname}?pairing_session=${sess.session_id}&secret=${sess.secret}`;
      openQrModal('PAIR-DEV', pairingUrl);
      if (qrStatus) qrStatus.textContent = 'Scan with your new device to pair with this account.';
    } catch (err) {
      alert(`Failed to create pairing session: ${(err as Error).message}`);
    }
  };
}

// Incoming Transfer Modal Buttons
if (acceptTransferBtn) {
  acceptTransferBtn.onclick = async () => {
    if (!currentIncomingRequest) return;
    try {
      const claimed = await transferCoordinator.claimTransfer(currentIncomingRequest.request_id);
      toggleModal(incomingTransferModal, false);
      if (claimed.room_code) {
        joinRoomFlow(claimed.room_code);
      }
    } catch (err) {
      alert(`Failed to accept transfer: ${(err as Error).message}`);
    }
  };
}

if (declineTransferBtn) {
  declineTransferBtn.onclick = async () => {
    if (!currentIncomingRequest) return;
    try {
      await transferCoordinator.declineTransfer(currentIncomingRequest.request_id);
    } catch {}
    toggleModal(incomingTransferModal, false);
  };
}

checkUrlHash();
window.addEventListener('hashchange', checkUrlHash);
