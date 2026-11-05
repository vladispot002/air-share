package server

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"log"
	"net"
	"net/http"
	"strings"
	"sync"
	"time"

	"github.com/gorilla/websocket"

	"landrop/signaling/internal/protocol"
	"landrop/signaling/internal/ratelimit"
	"landrop/signaling/internal/room"
	"landrop/signaling/internal/turn"
)

type Config struct {
	Port        string
	TurnSecret  string
	StunURLs    []string
	TurnURLs    []string
	RoomTTL     time.Duration
	MaxFailures int
	RateWindow  time.Duration
	BlockTime   time.Duration
}

type Server struct {
	cfg      Config
	roomMgr  *room.Manager
	limiter  *ratelimit.Limiter
	upgrader websocket.Upgrader
	mux      *http.ServeMux
}

func NewServer(cfg Config) *Server {
	if cfg.RoomTTL == 0 {
		cfg.RoomTTL = 10 * time.Minute
	}
	if cfg.MaxFailures == 0 {
		cfg.MaxFailures = 5
	}
	if cfg.RateWindow == 0 {
		cfg.RateWindow = 10 * time.Minute
	}
	if cfg.BlockTime == 0 {
		cfg.BlockTime = 10 * time.Minute
	}
	if len(cfg.StunURLs) == 0 {
		cfg.StunURLs = []string{"stun:stun.l.google.com:19302"}
	}

	s := &Server{
		cfg:     cfg,
		roomMgr: room.NewManager(cfg.RoomTTL),
		limiter: ratelimit.NewLimiter(cfg.MaxFailures, cfg.RateWindow, cfg.BlockTime),
		upgrader: websocket.Upgrader{
			ReadBufferSize:  4096,
			WriteBufferSize: 4096,
			CheckOrigin: func(r *http.Request) bool {
				return true // Guest mode allows all origins
			},
		},
		mux: http.NewServeMux(),
	}

	s.setupRoutes()
	return s
}

func (s *Server) Close() {
	s.roomMgr.Close()
	s.limiter.Close()
}

func (s *Server) Handler() http.Handler {
	return s.mux
}

func (s *Server) setupRoutes() {
	s.mux.HandleFunc("/healthz", s.handleHealthz)
	s.mux.HandleFunc("/api/v1/turn-config", s.handleTurnConfig)
	s.mux.HandleFunc("/ws/signaling", s.handleWebSocket)
}

func (s *Server) handleHealthz(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	_ = json.NewEncoder(w).Encode(map[string]any{
		"status":       "ok",
		"version":      protocol.CurrentVersion,
		"active_rooms": s.roomMgr.ActiveCount(),
	})
}

func (s *Server) handleTurnConfig(w http.ResponseWriter, r *http.Request) {
	token := r.Header.Get("Authorization")
	token = strings.TrimPrefix(token, "Bearer ")
	if token == "" {
		token = r.URL.Query().Get("token")
	}

	if token == "" {
		http.Error(w, `{"error":"missing authorization token"}`, http.StatusUnauthorized)
		return
	}

	_, exists := s.roomMgr.GetRoomByToken(token)
	if !exists {
		http.Error(w, `{"error":"invalid or expired room token"}`, http.StatusUnauthorized)
		return
	}

	clientID := r.URL.Query().Get("client_id")
	if clientID == "" {
		clientID = "client-" + randomID()[:8]
	}

	servers := turn.GenerateIceConfig(s.cfg.TurnSecret, clientID, s.cfg.StunURLs, s.cfg.TurnURLs, 1*time.Hour)

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	_ = json.NewEncoder(w).Encode(map[string]any{
		"ice_servers": servers,
		"ttl_seconds": 3600,
	})
}

// wsSender implements room.Sender over gorilla/websocket.
type wsSender struct {
	conn *websocket.Conn
	mu   sync.Mutex
}

func (w *wsSender) SendEnvelope(env *protocol.Envelope) error {
	w.mu.Lock()
	defer w.mu.Unlock()
	return w.conn.WriteJSON(env)
}

func (w *wsSender) Close() error {
	w.mu.Lock()
	defer w.mu.Unlock()
	return w.conn.Close()
}

func (s *Server) handleWebSocket(w http.ResponseWriter, r *http.Request) {
	conn, err := s.upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("websocket upgrade error: %v", err)
		return
	}

	ip := extractClientIP(r)
	clientID := "c-" + randomID()
	sender := &wsSender{conn: conn}
	client := room.NewClient(clientID, ip, sender)

	defer func() {
		s.cleanupClient(client)
		_ = client.Close()
	}()

	for {
		var env protocol.Envelope
		err := conn.ReadJSON(&env)
		if err != nil {
			break
		}

		s.processMessage(client, &env)
	}
}

func (s *Server) processMessage(client *room.Client, env *protocol.Envelope) {
	switch env.Action {
	case protocol.ActionCreateRoom:
		s.handleCreateRoom(client)

	case protocol.ActionJoinRoom:
		var payload protocol.JoinRoomPayload
		if len(env.Payload) > 0 {
			_ = json.Unmarshal(env.Payload, &payload)
		}
		targetCode := env.RoomCode
		if targetCode == "" {
			targetCode = payload.RoomCode
		}
		s.handleJoinRoom(client, targetCode)

	case protocol.ActionSdpOffer, protocol.ActionSdpAnswer:
		s.handleSdpRelay(client, env)

	case protocol.ActionIceCandidate:
		s.handleIceCandidateRelay(client, env)

	case protocol.ActionLeaveRoom:
		s.handleLeaveRoom(client)

	default:
		_ = client.Send(protocol.NewErrorEnvelope(
			protocol.ErrCodeInvalidMessage,
			"unrecognized action: "+env.Action,
			client.RoomCode,
			client.ID,
		))
	}
}

func (s *Server) handleCreateRoom(client *room.Client) {
	r, err := s.roomMgr.CreateRoom(client)
	if err != nil {
		_ = client.Send(protocol.NewErrorEnvelope(
			protocol.ErrCodeInternal,
			"failed to create room",
			"",
			client.ID,
		))
		return
	}

	payload := protocol.RoomCreatedPayload{
		RoomCode:  r.Code,
		RoomToken: r.Token,
		Role:      protocol.RoleInitiator,
		ExpiresAt: r.ExpiresAt.Unix(),
	}

	resp, _ := protocol.NewEnvelope(protocol.ActionRoomCreated, r.Code, client.ID, payload)
	_ = client.Send(resp)
}

func (s *Server) handleJoinRoom(client *room.Client, targetCode string) {
	if !s.limiter.Allow(client.IP) {
		_ = client.Send(protocol.NewErrorEnvelope(
			protocol.ErrCodeRateLimited,
			"maximum failed join attempts exceeded; temporarily throttled",
			targetCode,
			client.ID,
		))
		return
	}

	r, err := s.roomMgr.JoinRoom(targetCode, client)
	if err != nil {
		s.limiter.RecordFailure(client.IP)
		code := protocol.ErrCodeRoomNotFound
		if err == room.ErrRoomFull {
			code = protocol.ErrCodeRoomFull
		}
		_ = client.Send(protocol.NewErrorEnvelope(code, err.Error(), targetCode, client.ID))
		return
	}

	s.limiter.RecordSuccess(client.IP)

	// Notify Joiner
	joinerPayload := protocol.PeerJoinedPayload{
		RoomCode: r.Code,
		PeerID:   r.Initiator.ID,
		Role:     protocol.RoleJoiner,
	}
	joinerEnv, _ := protocol.NewEnvelope(protocol.ActionPeerJoined, r.Code, client.ID, joinerPayload)
	_ = client.Send(joinerEnv)

	// Notify Initiator
	initiatorPayload := protocol.PeerJoinedPayload{
		RoomCode: r.Code,
		PeerID:   client.ID,
		Role:     protocol.RoleInitiator,
	}
	initEnv, _ := protocol.NewEnvelope(protocol.ActionPeerJoined, r.Code, r.Initiator.ID, initiatorPayload)
	_ = r.Initiator.Send(initEnv)
}

func (s *Server) handleSdpRelay(client *room.Client, env *protocol.Envelope) {
	r, exists := s.roomMgr.GetRoom(client.RoomCode)
	if !exists {
		_ = client.Send(protocol.NewErrorEnvelope(
			protocol.ErrCodeRoomNotFound,
			"room not found",
			client.RoomCode,
			client.ID,
		))
		return
	}

	var sdpPayload protocol.SdpPayload
	if err := json.Unmarshal(env.Payload, &sdpPayload); err != nil {
		_ = client.Send(protocol.NewErrorEnvelope(
			protocol.ErrCodeInvalidMessage,
			"malformed SDP payload",
			client.RoomCode,
			client.ID,
		))
		return
	}

	// Enforce Glare Prevention
	if err := r.ValidateSdpRole(client.ID, sdpPayload.Type); err != nil {
		_ = client.Send(protocol.NewErrorEnvelope(
			protocol.ErrCodeGlareViolation,
			"glare violation: "+err.Error(),
			client.RoomCode,
			client.ID,
		))
		return
	}

	peer, err := r.GetPeer(client.ID)
	if err != nil {
		_ = client.Send(protocol.NewErrorEnvelope(
			protocol.ErrCodeRoomNotFound,
			"peer disconnected",
			client.RoomCode,
			client.ID,
		))
		return
	}

	if sdpPayload.Type == "answer" {
		r.SetEstablished()
	}

	// Forward envelope to peer
	_ = peer.Send(env)
}

func (s *Server) handleIceCandidateRelay(client *room.Client, env *protocol.Envelope) {
	r, exists := s.roomMgr.GetRoom(client.RoomCode)
	if !exists {
		return
	}

	peer, err := r.GetPeer(client.ID)
	if err != nil {
		return
	}

	_ = peer.Send(env)
}

func (s *Server) handleLeaveRoom(client *room.Client) {
	if client.RoomCode == "" {
		return
	}

	r, exists := s.roomMgr.GetRoom(client.RoomCode)
	if exists {
		if peer, err := r.GetPeer(client.ID); err == nil {
			leaveEnv, _ := protocol.NewEnvelope(protocol.ActionPeerLeft, r.Code, client.ID, protocol.PeerLeftPayload{
				PeerID: client.ID,
				Reason: "peer left the room",
			})
			_ = peer.Send(leaveEnv)
		}
		s.roomMgr.RemoveRoom(r.Code)
	}

	client.RoomCode = ""
}

func (s *Server) cleanupClient(client *room.Client) {
	if client.RoomCode == "" {
		return
	}

	r, exists := s.roomMgr.GetRoom(client.RoomCode)
	if exists {
		if peer, err := r.GetPeer(client.ID); err == nil {
			leftEnv, _ := protocol.NewEnvelope(protocol.ActionPeerLeft, r.Code, client.ID, protocol.PeerLeftPayload{
				PeerID: client.ID,
				Reason: "peer abruptly disconnected",
			})
			_ = peer.Send(leftEnv)
		}
		s.roomMgr.RemoveRoom(r.Code)
	}
}

func extractClientIP(r *http.Request) string {
	if xff := r.Header.Get("X-Forwarded-For"); xff != "" {
		parts := strings.Split(xff, ",")
		return strings.TrimSpace(parts[0])
	}
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return host
}

func randomID() string {
	bytes := make([]byte, 16)
	_, _ = rand.Read(bytes)
	return hex.EncodeToString(bytes)
}
