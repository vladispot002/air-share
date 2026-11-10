package server

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/gorilla/websocket"

	"landrop/signaling/internal/protocol"
	"landrop/signaling/internal/ratelimit"
	"landrop/signaling/internal/room"
	"landrop/signaling/internal/turn"
)

const (
	maxMessageSize = 64 * 1024       // 64 KB max WebSocket frame payload
	readWait       = 60 * time.Second // Time allowed to read the next pong message from the peer
	writeWait      = 5 * time.Second  // Time allowed to write a message to the peer
	pingPeriod     = 25 * time.Second // Send pings to peer with this period (must be < readWait)
	sendQueueSize  = 64               // Buffered outbound queue size per client
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
	TrustProxy  bool
	WebDir      string
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
				return true // Guest mode allows cross-origin requests
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

	if s.cfg.WebDir != "" {
		if fi, err := os.Stat(s.cfg.WebDir); err == nil && fi.IsDir() {
			fileServer := http.FileServer(neuteredFileSystem{http.Dir(s.cfg.WebDir)})
			s.mux.Handle("/", fileServer)
		}
	}
}

// neuteredFileSystem prevents automatic directory listings for folders without an index.html.
type neuteredFileSystem struct {
	fs http.FileSystem
}

func (nfs neuteredFileSystem) Open(path string) (http.File, error) {
	f, err := nfs.fs.Open(path)
	if err != nil {
		return nil, err
	}
	s, err := f.Stat()
	if err != nil {
		_ = f.Close()
		return nil, err
	}
	if s.IsDir() {
		// Only allow directory if it contains an index.html file
		indexPath := filepath.Join(path, "index.html")
		if idxFile, err := nfs.fs.Open(indexPath); err != nil {
			_ = f.Close()
			return nil, os.ErrNotExist
		} else {
			_ = idxFile.Close()
		}
	}
	return f, nil
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
	// Enable CORS for web clients
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.Header().Set("Access-Control-Allow-Methods", "GET, OPTIONS")
	w.Header().Set("Access-Control-Allow-Headers", "Authorization, Content-Type")

	if r.Method == http.MethodOptions {
		w.WriteHeader(http.StatusNoContent)
		return
	}

	token := r.Header.Get("Authorization")
	token = strings.TrimPrefix(token, "Bearer ")
	if token == "" {
		token = r.URL.Query().Get("token")
	}

	if token == "" {
		http.Error(w, `{"error":"missing authorization token"}`, http.StatusUnauthorized)
		return
	}

	targetRoom, exists := s.roomMgr.GetRoomByToken(token)
	if !exists {
		http.Error(w, `{"error":"invalid or expired room token"}`, http.StatusUnauthorized)
		return
	}

	remaining := time.Until(targetRoom.ExpiresAt)
	if remaining <= 0 {
		http.Error(w, `{"error":"room has expired"}`, http.StatusUnauthorized)
		return
	}

	clientID := r.URL.Query().Get("client_id")
	if clientID == "" {
		clientID = "client-" + randomID()[:8]
	}

	// Bound TURN credentials TTL to remaining room lifetime
	turnTTL := remaining
	if turnTTL > 1*time.Hour {
		turnTTL = 1 * time.Hour
	}

	servers := turn.GenerateIceConfig(s.cfg.TurnSecret, clientID, s.cfg.StunURLs, s.cfg.TurnURLs, turnTTL)

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	_ = json.NewEncoder(w).Encode(map[string]any{
		"ice_servers": servers,
		"ttl_seconds": int64(turnTTL.Seconds()),
	})
}

// wsSender implements an asynchronous, non-blocking room.Sender with outbound queue and heartbeats.
type wsSender struct {
	conn      *websocket.Conn
	sendQueue chan *protocol.Envelope
	closeOnce sync.Once
	closed    chan struct{}
}

func newWsSender(conn *websocket.Conn) *wsSender {
	s := &wsSender{
		conn:      conn,
		sendQueue: make(chan *protocol.Envelope, sendQueueSize),
		closed:    make(chan struct{}),
	}
	go s.writePump()
	return s
}

func (w *wsSender) SendEnvelope(env *protocol.Envelope) error {
	select {
	case <-w.closed:
		return errors.New("sender closed")
	case w.sendQueue <- env:
		return nil
	default:
		// Queue full: peer is not consuming messages, drop to avoid HoL blocking
		return errors.New("send queue saturated; dropping message")
	}
}

func (w *wsSender) Close() error {
	w.closeOnce.Do(func() {
		close(w.closed)
		_ = w.conn.Close()
	})
	return nil
}

func (w *wsSender) writePump() {
	ticker := time.NewTicker(pingPeriod)
	defer func() {
		ticker.Stop()
		_ = w.conn.Close()
	}()

	for {
		select {
		case <-w.closed:
			return

		case env, ok := <-w.sendQueue:
			_ = w.conn.SetWriteDeadline(time.Now().Add(writeWait))
			if !ok {
				_ = w.conn.WriteMessage(websocket.CloseMessage, []byte{})
				return
			}
			if err := w.conn.WriteJSON(env); err != nil {
				return
			}

		case <-ticker.C:
			_ = w.conn.SetWriteDeadline(time.Now().Add(writeWait))
			if err := w.conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				return
			}
		}
	}
}

func (s *Server) handleWebSocket(w http.ResponseWriter, r *http.Request) {
	conn, err := s.upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("websocket upgrade error: %v", err)
		return
	}

	// Enforce strict read limit (64 KB) to mitigate OOM attacks
	conn.SetReadLimit(maxMessageSize)
	_ = conn.SetReadDeadline(time.Now().Add(readWait))
	conn.SetPongHandler(func(string) error {
		_ = conn.SetReadDeadline(time.Now().Add(readWait))
		return nil
	})

	ip := extractClientIP(r, s.cfg.TrustProxy)
	clientID := "c-" + randomID()
	sender := newWsSender(conn)
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
		// Reset read deadline on every message received
		_ = conn.SetReadDeadline(time.Now().Add(readWait))

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
	if client.RoomCode != "" {
		_ = client.Send(protocol.NewErrorEnvelope(
			"ERR_ALREADY_IN_ROOM",
			"client is already participating in a room",
			client.RoomCode,
			client.ID,
		))
		return
	}

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
	if client.RoomCode != "" {
		_ = client.Send(protocol.NewErrorEnvelope(
			"ERR_ALREADY_IN_ROOM",
			"client is already participating in a room",
			client.RoomCode,
			client.ID,
		))
		return
	}

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
		} else if err == room.ErrCannotJoinOwn {
			code = "ERR_CANNOT_JOIN_OWN_ROOM"
		}
		_ = client.Send(protocol.NewErrorEnvelope(code, err.Error(), targetCode, client.ID))
		return
	}

	s.limiter.RecordSuccess(client.IP)

	// Notify Joiner (supplying room token for TURN access)
	joinerPayload := protocol.PeerJoinedPayload{
		RoomCode:  r.Code,
		PeerID:    r.Initiator.ID,
		Role:      protocol.RoleJoiner,
		RoomToken: r.Token,
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

	// Enforce strict envelope action vs payload type synchronization
	if env.Action == protocol.ActionSdpOffer && sdpPayload.Type != "offer" {
		_ = client.Send(protocol.NewErrorEnvelope(
			protocol.ErrCodeGlareViolation,
			"action SDP_OFFER requires payload type 'offer'",
			client.RoomCode,
			client.ID,
		))
		return
	}
	if env.Action == protocol.ActionSdpAnswer && sdpPayload.Type != "answer" {
		_ = client.Send(protocol.NewErrorEnvelope(
			protocol.ErrCodeGlareViolation,
			"action SDP_ANSWER requires payload type 'answer'",
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

	// Sanitize client ID and room code to prevent spoofing
	env.ClientID = client.ID
	env.RoomCode = client.RoomCode

	// Forward envelope to peer
	_ = peer.Send(env)
}

func (s *Server) handleIceCandidateRelay(client *room.Client, env *protocol.Envelope) {
	r, exists := s.roomMgr.GetRoom(client.RoomCode)
	if !exists {
		return
	}

	var candPayload protocol.IceCandidatePayload
	if err := json.Unmarshal(env.Payload, &candPayload); err != nil || candPayload.Candidate == "" {
		_ = client.Send(protocol.NewErrorEnvelope(
			protocol.ErrCodeInvalidMessage,
			"malformed or empty ICE candidate payload",
			client.RoomCode,
			client.ID,
		))
		return
	}

	peer, err := r.GetPeer(client.ID)
	if err != nil {
		return
	}

	// Sanitize client ID and room code
	env.ClientID = client.ID
	env.RoomCode = client.RoomCode

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

func extractClientIP(r *http.Request, trustProxy bool) string {
	if trustProxy {
		if xff := r.Header.Get("X-Forwarded-For"); xff != "" {
			parts := strings.Split(xff, ",")
			return strings.TrimSpace(parts[0])
		}
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
