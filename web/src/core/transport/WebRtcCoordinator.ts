import { SignalingClient } from './SignalingClient';

export interface TurnServerConfig {
  urls: string | string[];
  username?: string;
  credential?: string;
}

export interface TurnApiResponse {
  ice_servers: TurnServerConfig[];
  ttl_seconds: number;
}

export async function fetchTurnConfig(
  signalingHttpUrl: string,
  token: string,
  clientId?: string,
  timeoutMs: number = 4000
): Promise<RTCConfiguration> {
  const url = new URL('/api/v1/turn-config', signalingHttpUrl);
  // DEF-12: Never expose secret token in URL query string
  if (clientId) {
    url.searchParams.set('client_id', clientId);
  }

  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);

  try {
    const res = await fetch(url.toString(), {
      method: 'GET',
      headers: {
        Authorization: `Bearer ${token}`,
        Accept: 'application/json',
      },
      signal: controller.signal,
    });

    if (!res.ok) {
      throw new Error(`Failed to fetch TURN configuration: HTTP ${res.status}`);
    }

    const data = (await res.json()) as TurnApiResponse;
    return {
      iceServers: data.ice_servers as RTCIceServer[],
    };
  } finally {
    clearTimeout(timer);
  }
}

/**
 * Wait for RTCDataChannel buffered amount to drop below threshold, with timeout and error resilience (DEF-07).
 */
export function waitForBufferedAmountLow(
  channel: RTCDataChannel,
  threshold: number = 1024 * 1024,
  timeoutMs: number = 30000
): Promise<void> {
  if (channel.bufferedAmount <= threshold) {
    return Promise.resolve();
  }
  return new Promise<void>((resolve, reject) => {
    channel.bufferedAmountLowThreshold = threshold;
    let timer: any = null;

    const cleanup = () => {
      channel.removeEventListener('bufferedamountlow', onLow);
      channel.removeEventListener('close', onClose);
      channel.removeEventListener('error', onError);
      if (timer) {
        clearTimeout(timer);
        timer = null;
      }
    };

    const onLow = () => {
      cleanup();
      resolve();
    };

    const onClose = () => {
      cleanup();
      reject(new Error('RTCDataChannel closed while waiting for backpressure relief'));
    };

    const onError = () => {
      cleanup();
      reject(new Error('RTCDataChannel error while waiting for backpressure relief'));
    };

    timer = setTimeout(() => {
      cleanup();
      reject(new Error(`Timeout (${timeoutMs}ms) waiting for bufferedamountlow on RTCDataChannel`));
    }, timeoutMs);

    channel.addEventListener('bufferedamountlow', onLow);
    channel.addEventListener('close', onClose);
    channel.addEventListener('error', onError);
  });
}

export class WebRtcCoordinator {
  private peerConnection: RTCPeerConnection | null = null;
  private dataChannel: RTCDataChannel | null = null;
  private pendingRemoteCandidates: RTCIceCandidateInit[] = [];
  private hasRemoteDescription: boolean = false;
  private isInitiator: boolean = false;

  // Stored listener references for unbinding in close() (DEF-04)
  private onIceCandidateHandler?: (payload: any) => Promise<void>;
  private onSdpAnswerHandler?: (payload: any) => Promise<void>;
  private onSdpOfferHandler?: (payload: any) => Promise<void>;
  private onErrorHandler?: (payload: any) => void;
  private onPeerLeftHandler?: (payload: any) => void;

  constructor(
    private signalingClient: SignalingClient,
    private config?: RTCConfiguration
  ) {}

  get channel(): RTCDataChannel | null {
    return this.dataChannel;
  }

  get connection(): RTCPeerConnection | null {
    return this.peerConnection;
  }

  get initiator(): boolean {
    return this.isInitiator;
  }

  async setupPeerConnection(customConfig?: RTCConfiguration): Promise<RTCPeerConnection> {
    const rtcConfig = customConfig || this.config || {
      iceServers: [{ urls: 'stun:stun.l.google.com:19302' }],
    };
    if (typeof window !== 'undefined' && (window as any).__LAN_DROP_ICE_POLICY) {
      rtcConfig.iceTransportPolicy = (window as any).__LAN_DROP_ICE_POLICY;
    }

    const pc = new RTCPeerConnection(rtcConfig);
    this.peerConnection = pc;
    this.hasRemoteDescription = false;
    this.pendingRemoteCandidates = [];

    pc.onicecandidate = (event) => {
      if (event.candidate && event.candidate.candidate) {
        if (typeof window !== 'undefined' && typeof (window as any).__LAN_DROP_CANDIDATE_FILTER === 'function') {
          if (!(window as any).__LAN_DROP_CANDIDATE_FILTER(event.candidate.candidate)) {
            return;
          }
        }
        this.signalingClient.sendIceCandidate(event.candidate.toJSON());
      }
    };

    if (this.onIceCandidateHandler) {
      this.signalingClient.off('ice_candidate', this.onIceCandidateHandler as any);
    }

    this.onIceCandidateHandler = async (payload) => {
      const candidateInit: RTCIceCandidateInit = {
        candidate: payload.candidate,
        sdpMid: payload.sdp_mid,
        sdpMLineIndex: payload.sdp_mline_index,
      };
      await this.handleRemoteCandidate(candidateInit);
    };

    this.signalingClient.on('ice_candidate', this.onIceCandidateHandler);

    return pc;
  }

  private async handleRemoteCandidate(candidate: RTCIceCandidateInit): Promise<void> {
    if (!candidate || !candidate.candidate) {
      return;
    }
    if (typeof window !== 'undefined' && typeof (window as any).__LAN_DROP_CANDIDATE_FILTER === 'function') {
      if (!(window as any).__LAN_DROP_CANDIDATE_FILTER(candidate.candidate)) {
        return;
      }
    }
    if (!this.peerConnection || !this.hasRemoteDescription) {
      this.pendingRemoteCandidates.push(candidate);
      return;
    }
    try {
      await this.peerConnection.addIceCandidate(new RTCIceCandidate(candidate));
    } catch (err) {
      console.warn('Failed to add remote ICE candidate:', err);
    }
  }

  private async flushPendingCandidates(): Promise<void> {
    if (!this.peerConnection) return;
    while (this.pendingRemoteCandidates.length > 0) {
      const c = this.pendingRemoteCandidates.shift();
      if (c && c.candidate) {
        try {
          await this.peerConnection.addIceCandidate(new RTCIceCandidate(c));
        } catch (err) {
          console.warn('Failed to flush remote ICE candidate:', err);
        }
      }
    }
  }

  async startInitiator(timeoutMs: number = 25000): Promise<RTCDataChannel> {
    this.isInitiator = true;
    const pc = await this.setupPeerConnection();

    const dc = pc.createDataChannel('lan-drop-data', {
      ordered: true,
    });
    this.configureDataChannel(dc);

    // Wait for peer to join the room before transmitting SDP offer
    if (!this.signalingClient.hasPeerJoined) {
      await new Promise<void>((resolve, reject) => {
        let timer: any = setTimeout(() => {
          cleanup();
          reject(new Error(`Timeout (${timeoutMs}ms) waiting for peer to join room`));
        }, timeoutMs);

        const onPeerJoined = () => {
          cleanup();
          resolve();
        };
        const onError = (errPayload: any) => {
          cleanup();
          reject(new Error(`Signaling error while waiting for peer: [${errPayload.code}] ${errPayload.message}`));
        };
        const cleanup = () => {
          if (timer) {
            clearTimeout(timer);
            timer = null;
          }
          this.signalingClient.off('peer_joined', onPeerJoined as any);
          this.signalingClient.off('error', onError as any);
        };
        this.signalingClient.on('peer_joined', onPeerJoined);
        this.signalingClient.on('error', onError);
      });
    }

    const offer = await pc.createOffer();
    await pc.setLocalDescription(offer);
    this.signalingClient.sendSdpOffer(offer.sdp!);

    return new Promise((resolve, reject) => {
      let settled = false;

      let timeoutTimer: any = setTimeout(() => {
        if (!settled) {
          settled = true;
          cleanup();
          reject(new Error(`Connection timeout (${timeoutMs}ms) establishing DataChannel with peer`));
        }
      }, timeoutMs);

      const onIceStateChange = () => {
        if (pc.iceConnectionState === 'failed') {
          if (!settled) {
            settled = true;
            cleanup();
            reject(new Error('WebRTC ICE connection failed (NAT traversal or relay failure)'));
          }
        }
      };

      const onConnStateChange = () => {
        if (pc.connectionState === 'failed') {
          if (!settled) {
            settled = true;
            cleanup();
            reject(new Error('WebRTC PeerConnection state failed'));
          }
        }
      };

      pc.addEventListener('iceconnectionstatechange', onIceStateChange);
      pc.addEventListener('connectionstatechange', onConnStateChange);

      const cleanup = () => {
        if (timeoutTimer) {
          clearTimeout(timeoutTimer);
          timeoutTimer = null;
        }
        pc.removeEventListener('iceconnectionstatechange', onIceStateChange);
        pc.removeEventListener('connectionstatechange', onConnStateChange);

        if (this.onSdpAnswerHandler) {
          this.signalingClient.off('sdp_answer', this.onSdpAnswerHandler as any);
          this.onSdpAnswerHandler = undefined;
        }
        if (this.onErrorHandler) {
          this.signalingClient.off('error', this.onErrorHandler as any);
          this.onErrorHandler = undefined;
        }
        if (this.onPeerLeftHandler) {
          this.signalingClient.off('peer_left', this.onPeerLeftHandler as any);
          this.onPeerLeftHandler = undefined;
        }
      };

      // DEF-03: Listen for glare or signaling errors and reject immediately
      this.onErrorHandler = (errPayload: any) => {
        if (!settled) {
          settled = true;
          cleanup();
          reject(new Error(`Signaling error during initiator setup: [${errPayload.code}] ${errPayload.message}`));
        }
      };
      this.signalingClient.on('error', this.onErrorHandler);

      this.onPeerLeftHandler = (peerPayload: any) => {
        if (!settled) {
          settled = true;
          cleanup();
          reject(new Error(`Peer left during connection setup: ${peerPayload.reason || 'disconnected'}`));
        }
      };
      this.signalingClient.on('peer_left', this.onPeerLeftHandler);

      this.onSdpAnswerHandler = async (payload) => {
        try {
          await pc.setRemoteDescription(new RTCSessionDescription({ type: 'answer', sdp: payload.sdp }));
          this.hasRemoteDescription = true;
          await this.flushPendingCandidates();
        } catch (err) {
          if (!settled) {
            settled = true;
            cleanup();
            reject(err);
          }
        }
      };
      this.signalingClient.on('sdp_answer', this.onSdpAnswerHandler);

      if (dc.readyState === 'open') {
        settled = true;
        cleanup();
        resolve(dc);
        return;
      }

      dc.onopen = () => {
        if (!settled) {
          settled = true;
          cleanup();
          resolve(dc);
        }
      };
      dc.onerror = () => {
        if (!settled) {
          settled = true;
          cleanup();
          reject(new Error('DataChannel error before opening'));
        }
      };
      dc.onclose = () => {
        if (!settled) {
          settled = true;
          cleanup();
          reject(new Error('DataChannel closed before opening'));
        }
      };
    });
  }

  async startJoiner(timeoutMs: number = 25000): Promise<RTCDataChannel> {
    this.isInitiator = false;
    const pc = await this.setupPeerConnection();

    return new Promise((resolve, reject) => {
      let settled = false;

      let timeoutTimer: any = setTimeout(() => {
        if (!settled) {
          settled = true;
          cleanup();
          reject(new Error(`Connection timeout (${timeoutMs}ms) negotiating with host`));
        }
      }, timeoutMs);

      const onIceStateChange = () => {
        if (pc.iceConnectionState === 'failed') {
          if (!settled) {
            settled = true;
            cleanup();
            reject(new Error('WebRTC ICE connection failed on joiner (NAT traversal or relay failure)'));
          }
        }
      };

      const onConnStateChange = () => {
        if (pc.connectionState === 'failed') {
          if (!settled) {
            settled = true;
            cleanup();
            reject(new Error('WebRTC PeerConnection state failed on joiner'));
          }
        }
      };

      pc.addEventListener('iceconnectionstatechange', onIceStateChange);
      pc.addEventListener('connectionstatechange', onConnStateChange);

      const cleanup = () => {
        if (timeoutTimer) {
          clearTimeout(timeoutTimer);
          timeoutTimer = null;
        }
        pc.removeEventListener('iceconnectionstatechange', onIceStateChange);
        pc.removeEventListener('connectionstatechange', onConnStateChange);

        if (this.onSdpOfferHandler) {
          this.signalingClient.off('sdp_offer', this.onSdpOfferHandler as any);
          this.onSdpOfferHandler = undefined;
        }
        if (this.onErrorHandler) {
          this.signalingClient.off('error', this.onErrorHandler as any);
          this.onErrorHandler = undefined;
        }
        if (this.onPeerLeftHandler) {
          this.signalingClient.off('peer_left', this.onPeerLeftHandler as any);
          this.onPeerLeftHandler = undefined;
        }
      };

      // DEF-03: Glare / error detection
      this.onErrorHandler = (errPayload: any) => {
        if (!settled) {
          settled = true;
          cleanup();
          reject(new Error(`Signaling error during joiner setup: [${errPayload.code}] ${errPayload.message}`));
        }
      };
      this.signalingClient.on('error', this.onErrorHandler);

      this.onPeerLeftHandler = (peerPayload: any) => {
        if (!settled) {
          settled = true;
          cleanup();
          reject(new Error(`Peer left during connection setup: ${peerPayload.reason || 'disconnected'}`));
        }
      };
      this.signalingClient.on('peer_left', this.onPeerLeftHandler);

      pc.ondatachannel = (ev) => {
        const dc = ev.channel;
        this.configureDataChannel(dc);
        if (dc.readyState === 'open') {
          if (!settled) {
            settled = true;
            cleanup();
            resolve(dc);
          }
        } else {
          dc.onopen = () => {
            if (!settled) {
              settled = true;
              cleanup();
              resolve(dc);
            }
          };
          dc.onerror = () => {
            if (!settled) {
              settled = true;
              cleanup();
              reject(new Error('DataChannel error on joiner'));
            }
          };
        }
      };

      this.onSdpOfferHandler = async (payload) => {
        try {
          await pc.setRemoteDescription(new RTCSessionDescription({ type: 'offer', sdp: payload.sdp }));
          this.hasRemoteDescription = true;
          await this.flushPendingCandidates();

          const answer = await pc.createAnswer();
          await pc.setLocalDescription(answer);
          this.signalingClient.sendSdpAnswer(answer.sdp!);
        } catch (err) {
          if (!settled) {
            settled = true;
            cleanup();
            reject(err);
          }
        }
      };
      this.signalingClient.on('sdp_offer', this.onSdpOfferHandler);
    });
  }

  private configureDataChannel(dc: RTCDataChannel): void {
    dc.binaryType = 'arraybuffer';
    dc.bufferedAmountLowThreshold = 1024 * 1024; // 1 MB backpressure threshold
    this.dataChannel = dc;
  }

  close(): void {
    if (this.onIceCandidateHandler) {
      this.signalingClient.off('ice_candidate', this.onIceCandidateHandler as any);
      this.onIceCandidateHandler = undefined;
    }
    if (this.onSdpAnswerHandler) {
      this.signalingClient.off('sdp_answer', this.onSdpAnswerHandler as any);
      this.onSdpAnswerHandler = undefined;
    }
    if (this.onSdpOfferHandler) {
      this.signalingClient.off('sdp_offer', this.onSdpOfferHandler as any);
      this.onSdpOfferHandler = undefined;
    }
    if (this.onErrorHandler) {
      this.signalingClient.off('error', this.onErrorHandler as any);
      this.onErrorHandler = undefined;
    }
    if (this.onPeerLeftHandler) {
      this.signalingClient.off('peer_left', this.onPeerLeftHandler as any);
      this.onPeerLeftHandler = undefined;
    }

    if (this.dataChannel) {
      try {
        this.dataChannel.close();
      } catch {}
      this.dataChannel = null;
    }
    if (this.peerConnection) {
      try {
        this.peerConnection.close();
      } catch {}
      this.peerConnection = null;
    }
    this.pendingRemoteCandidates = [];
    this.hasRemoteDescription = false;
  }
}
