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
  clientId?: string
): Promise<RTCConfiguration> {
  const url = new URL('/api/v1/turn-config', signalingHttpUrl);
  url.searchParams.set('token', token);
  if (clientId) {
    url.searchParams.set('client_id', clientId);
  }

  const res = await fetch(url.toString(), {
    method: 'GET',
    headers: {
      Authorization: `Bearer ${token}`,
      Accept: 'application/json',
    },
  });

  if (!res.ok) {
    throw new Error(`Failed to fetch TURN configuration: HTTP ${res.status}`);
  }

  const data = (await res.json()) as TurnApiResponse;
  return {
    iceServers: data.ice_servers as RTCIceServer[],
  };
}

export function waitForBufferedAmountLow(channel: RTCDataChannel, threshold: number = 1024 * 1024): Promise<void> {
  if (channel.bufferedAmount <= threshold) {
    return Promise.resolve();
  }
  return new Promise<void>((resolve) => {
    channel.bufferedAmountLowThreshold = threshold;
    const onLow = () => {
      channel.removeEventListener('bufferedamountlow', onLow);
      resolve();
    };
    channel.addEventListener('bufferedamountlow', onLow);
  });
}

export class WebRtcCoordinator {
  private peerConnection: RTCPeerConnection | null = null;
  private dataChannel: RTCDataChannel | null = null;
  private pendingRemoteCandidates: RTCIceCandidateInit[] = [];
  private hasRemoteDescription: boolean = false;
  private isInitiator: boolean = false;

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

    const pc = new RTCPeerConnection(rtcConfig);
    this.peerConnection = pc;
    this.hasRemoteDescription = false;
    this.pendingRemoteCandidates = [];

    pc.onicecandidate = (event) => {
      if (event.candidate) {
        this.signalingClient.sendIceCandidate(event.candidate.toJSON());
      }
    };

    this.signalingClient.on('ice_candidate', async (payload) => {
      const candidateInit: RTCIceCandidateInit = {
        candidate: payload.candidate,
        sdpMid: payload.sdp_mid,
        sdpMLineIndex: payload.sdp_mline_index,
      };
      await this.handleRemoteCandidate(candidateInit);
    });

    return pc;
  }

  private async handleRemoteCandidate(candidate: RTCIceCandidateInit): Promise<void> {
    if (!this.peerConnection || !this.hasRemoteDescription) {
      this.pendingRemoteCandidates.push(candidate);
      return;
    }
    try {
      await this.peerConnection.addIceCandidate(new RTCIceCandidate(candidate));
    } catch (err) {
      console.error('Failed to add remote ICE candidate:', err);
    }
  }

  private async flushPendingCandidates(): Promise<void> {
    if (!this.peerConnection) return;
    while (this.pendingRemoteCandidates.length > 0) {
      const c = this.pendingRemoteCandidates.shift();
      if (c) {
        try {
          await this.peerConnection.addIceCandidate(new RTCIceCandidate(c));
        } catch (err) {
          console.error('Failed to flush remote ICE candidate:', err);
        }
      }
    }
  }

  async startInitiator(): Promise<RTCDataChannel> {
    this.isInitiator = true;
    const pc = await this.setupPeerConnection();

    const dc = pc.createDataChannel('lan-drop-data', {
      ordered: true,
    });
    this.configureDataChannel(dc);

    const offer = await pc.createOffer();
    await pc.setLocalDescription(offer);
    this.signalingClient.sendSdpOffer(offer.sdp!);

    return new Promise((resolve, reject) => {
      this.signalingClient.on('sdp_answer', async (payload) => {
        try {
          await pc.setRemoteDescription(new RTCSessionDescription({ type: 'answer', sdp: payload.sdp }));
          this.hasRemoteDescription = true;
          await this.flushPendingCandidates();
        } catch (err) {
          reject(err);
        }
      });

      dc.onopen = () => {
        resolve(dc);
      };
      dc.onerror = () => {
        reject(new Error('DataChannel error before opening'));
      };

    });
  }

  async startJoiner(): Promise<RTCDataChannel> {
    this.isInitiator = false;
    const pc = await this.setupPeerConnection();

    return new Promise((resolve, reject) => {
      pc.ondatachannel = (ev) => {
        const dc = ev.channel;
        this.configureDataChannel(dc);
        if (dc.readyState === 'open') {
          resolve(dc);
        } else {
          dc.onopen = () => resolve(dc);
        }
      };

      this.signalingClient.on('sdp_offer', async (payload) => {
        try {
          await pc.setRemoteDescription(new RTCSessionDescription({ type: 'offer', sdp: payload.sdp }));
          this.hasRemoteDescription = true;
          await this.flushPendingCandidates();

          const answer = await pc.createAnswer();
          await pc.setLocalDescription(answer);
          this.signalingClient.sendSdpAnswer(answer.sdp!);
        } catch (err) {
          reject(err);
        }
      });
    });
  }

  private configureDataChannel(dc: RTCDataChannel): void {
    dc.binaryType = 'arraybuffer';
    dc.bufferedAmountLowThreshold = 1024 * 1024; // 1 MB backpressure threshold
    this.dataChannel = dc;
  }

  close(): void {
    if (this.dataChannel) {
      this.dataChannel.close();
      this.dataChannel = null;
    }
    if (this.peerConnection) {
      this.peerConnection.close();
      this.peerConnection = null;
    }
    this.pendingRemoteCandidates = [];
    this.hasRemoteDescription = false;
  }
}
