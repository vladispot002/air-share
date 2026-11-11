export const SIGNALING_PROTOCOL_VERSION = '2.0';

export enum SignalingAction {
  CreateRoom = 'CREATE_ROOM',
  RoomCreated = 'ROOM_CREATED',
  JoinRoom = 'JOIN_ROOM',
  PeerJoined = 'PEER_JOINED',
  SdpOffer = 'SDP_OFFER',
  SdpAnswer = 'SDP_ANSWER',
  IceCandidate = 'ICE_CANDIDATE',
  PeerLeft = 'PEER_LEFT',
  LeaveRoom = 'LEAVE_ROOM',
  Error = 'ERROR',
}

export interface SignalingEnvelope<T = any> {
  version: string;
  action: string;
  room_code?: string;
  client_id?: string;
  timestamp?: number;
  payload?: T;
}

export interface RoomCreatedPayload {
  room_code: string;
  room_token: string;
  role: 'initiator';
  expires_at: number;
}

export interface PeerJoinedPayload {
  room_code: string;
  peer_id: string;
  role: 'initiator' | 'joiner';
  room_token?: string;
}

export interface SdpPayload {
  type: 'offer' | 'answer';
  sdp: string;
  e2ee_public_key?: string;
}

export interface IceCandidatePayload {
  candidate: string;
  sdp_mid?: string;
  sdp_mline_index: number;
}

export interface PeerLeftPayload {
  peer_id?: string;
  reason: string;
}

export interface ErrorPayload {
  code: string;
  message: string;
}

export type SignalingEventMap = {
  room_created: RoomCreatedPayload;
  peer_joined: PeerJoinedPayload;
  sdp_offer: SdpPayload;
  sdp_answer: SdpPayload;
  ice_candidate: IceCandidatePayload;
  peer_left: PeerLeftPayload;
  error: ErrorPayload;
  close: void;
};

export class SignalingClient {
  private ws: WebSocket | null = null;
  private clientId: string = `c-web-${Math.random().toString(36).substring(2, 10)}`;
  private listeners: { [K in keyof SignalingEventMap]?: Array<(data: SignalingEventMap[K]) => void> } = {};
  private rawListeners: Array<(env: SignalingEnvelope) => void> = [];
  private currentRoomCode: string = '';
  private currentRoomToken: string = '';
  private pingInterval: any = null;
  private peerJoined: boolean = false;
  private pendingOffer: SdpPayload | null = null;
  private pendingAnswer: SdpPayload | null = null;
  private pendingCandidates: IceCandidatePayload[] = [];

  constructor(clientId?: string) {
    if (clientId) this.clientId = clientId;
  }

  get id(): string {
    return this.clientId;
  }

  get roomCode(): string {
    return this.currentRoomCode;
  }

  get roomToken(): string {
    return this.currentRoomToken;
  }

  get hasPeerJoined(): boolean {
    return this.peerJoined;
  }

  on<K extends keyof SignalingEventMap>(event: K, handler: (data: SignalingEventMap[K]) => void): void {
    if (!this.listeners[event]) {
      this.listeners[event] = [];
    }
    this.listeners[event]!.push(handler);

    // Flush early-arriving payloads that arrived before listener registration
    if (event === 'sdp_offer' && this.pendingOffer) {
      const offer = this.pendingOffer;
      this.pendingOffer = null;
      queueMicrotask(() => handler(offer as any));
    } else if (event === 'sdp_answer' && this.pendingAnswer) {
      const answer = this.pendingAnswer;
      this.pendingAnswer = null;
      queueMicrotask(() => handler(answer as any));
    } else if (event === 'ice_candidate' && this.pendingCandidates.length > 0) {
      const cands = [...this.pendingCandidates];
      this.pendingCandidates = [];
      queueMicrotask(() => {
        for (const c of cands) handler(c as any);
      });
    }
  }

  off<K extends keyof SignalingEventMap>(event: K, handler: (data: SignalingEventMap[K]) => void): void {
    if (!this.listeners[event]) return;
    this.listeners[event] = (this.listeners[event] as any[]).filter((h) => h !== handler);
  }

  private emit<K extends keyof SignalingEventMap>(event: K, data: SignalingEventMap[K]): void {
    const handlers = this.listeners[event];
    if (handlers) {
      for (const h of handlers) {
        h(data);
      }
    }
  }

  connect(wsUrl: string): Promise<void> {
    return new Promise((resolve, reject) => {
      try {
        this.ws = new WebSocket(wsUrl);
      } catch (err) {
        return reject(err);
      }

      this.ws.onopen = () => {
        this.startHeartbeat();
        resolve();
      };

      this.ws.onerror = () => {
        reject(new Error(`WebSocket error connecting to ${wsUrl}`));
      };


      this.ws.onclose = () => {
        this.stopHeartbeat();
        this.emit('close', undefined);
      };

      this.ws.onmessage = (event) => {
        try {
          const env = JSON.parse(event.data) as SignalingEnvelope;
          this.handleIncomingEnvelope(env);
        } catch (err) {
          console.error('Failed to parse signaling message:', err);
        }
      };
    });
  }

  private startHeartbeat(): void {
    this.stopHeartbeat();
    this.pingInterval = setInterval(() => {
      if (this.ws && this.ws.readyState === WebSocket.OPEN) {
        if (typeof (this.ws as any).ping === 'function') {
          (this.ws as any).ping();
        }
      }
    }, 25000);
  }

  private stopHeartbeat(): void {
    if (this.pingInterval) {
      clearInterval(this.pingInterval);
      this.pingInterval = null;
    }
  }

  private handleIncomingEnvelope(env: SignalingEnvelope): void {
    if (env.client_id) {
      this.clientId = env.client_id;
    }
    for (const listener of this.rawListeners) {
      try {
        listener(env);
      } catch (e) {
        console.error('Error in raw envelope listener:', e);
      }
    }
    switch (env.action) {
      case SignalingAction.RoomCreated: {
        const payload = env.payload as RoomCreatedPayload;
        this.currentRoomCode = payload.room_code;
        this.currentRoomToken = payload.room_token;
        this.emit('room_created', payload);
        break;
      }

      case SignalingAction.PeerJoined: {
        const payload = env.payload as PeerJoinedPayload;
        this.currentRoomCode = payload.room_code;
        if (payload.room_token) {
          this.currentRoomToken = payload.room_token;
        }
        this.peerJoined = true;
        this.emit('peer_joined', payload);
        break;
      }
      case SignalingAction.SdpOffer: {
        const payload = env.payload as SdpPayload;
        if (!this.listeners['sdp_offer'] || this.listeners['sdp_offer']!.length === 0) {
          this.pendingOffer = payload;
        } else {
          this.emit('sdp_offer', payload);
        }
        break;
      }
      case SignalingAction.SdpAnswer: {
        const payload = env.payload as SdpPayload;
        if (!this.listeners['sdp_answer'] || this.listeners['sdp_answer']!.length === 0) {
          this.pendingAnswer = payload;
        } else {
          this.emit('sdp_answer', payload);
        }
        break;
      }
      case SignalingAction.IceCandidate: {
        const payload = env.payload as IceCandidatePayload;
        if (!this.listeners['ice_candidate'] || this.listeners['ice_candidate']!.length === 0) {
          this.pendingCandidates.push(payload);
        } else {
          this.emit('ice_candidate', payload);
        }
        break;
      }
      case SignalingAction.PeerLeft: {
        this.peerJoined = false;
        this.emit('peer_left', env.payload as PeerLeftPayload);
        break;
      }
      case SignalingAction.Error: {
        this.emit('error', env.payload as ErrorPayload);
        break;
      }
    }
  }

  sendEnvelope(action: SignalingAction | string, roomCode?: string, payload?: any): void {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      throw new Error('WebSocket is not open');
    }
    const env: SignalingEnvelope = {
      version: SIGNALING_PROTOCOL_VERSION,
      action,
      room_code: roomCode || this.currentRoomCode,
      client_id: this.clientId,
      timestamp: Math.floor(Date.now() / 1000),
      payload,
    };
    this.ws.send(JSON.stringify(env));
  }

  createRoom(): Promise<RoomCreatedPayload> {
    return new Promise((resolve, reject) => {
      const onCreated = (payload: RoomCreatedPayload) => {
        cleanup();
        resolve(payload);
      };
      const onError = (payload: ErrorPayload) => {
        cleanup();
        reject(new Error(`Signaling error [${payload.code}]: ${payload.message}`));
      };
      const cleanup = () => {
        this.off('room_created', onCreated);
        this.off('error', onError);
      };

      this.on('room_created', onCreated);
      this.on('error', onError);
      this.sendEnvelope(SignalingAction.CreateRoom);
    });
  }

  joinRoom(roomCode: string): Promise<PeerJoinedPayload> {
    return new Promise((resolve, reject) => {
      const onJoined = (payload: PeerJoinedPayload) => {
        cleanup();
        resolve(payload);
      };
      const onError = (payload: ErrorPayload) => {
        cleanup();
        reject(new Error(`Signaling error [${payload.code}]: ${payload.message}`));
      };
      const cleanup = () => {
        this.off('peer_joined', onJoined);
        this.off('error', onError);
      };

      this.on('peer_joined', onJoined);
      this.on('error', onError);
      this.sendEnvelope(SignalingAction.JoinRoom, roomCode, { room_code: roomCode });
    });
  }

  sendSdpOffer(sdp: string, roomCode?: string): void {
    this.sendEnvelope(SignalingAction.SdpOffer, roomCode, { type: 'offer', sdp });
  }

  sendSdpAnswer(sdp: string, roomCode?: string): void {
    this.sendEnvelope(SignalingAction.SdpAnswer, roomCode, { type: 'answer', sdp });
  }

  sendIceCandidate(candidate: RTCIceCandidateInit, roomCode?: string): void {
    this.sendEnvelope(SignalingAction.IceCandidate, roomCode, {
      candidate: candidate.candidate || '',
      sdp_mid: candidate.sdpMid || undefined,
      sdp_mline_index: candidate.sdpMLineIndex ?? 0,
    });
  }

  leaveRoom(): void {
    if (this.currentRoomCode) {
      if (this.ws && this.ws.readyState === WebSocket.OPEN) {
        try {
          this.sendEnvelope(SignalingAction.LeaveRoom, this.currentRoomCode);
        } catch {
          // Ignore send failures during teardown
        }
      }
      this.currentRoomCode = '';
      this.currentRoomToken = '';
      this.peerJoined = false;
    }
  }

  getWebSocket(): WebSocket | null {
    return this.ws;
  }

  onRawEnvelope(listener: (env: SignalingEnvelope) => void): () => void {
    this.rawListeners.push(listener);
    return () => {
      this.rawListeners = this.rawListeners.filter((l) => l !== listener);
    };
  }

  close(): void {
    this.leaveRoom();
    this.stopHeartbeat();
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
    this.listeners = {};
  }
}
