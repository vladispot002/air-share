package main

import (
	"context"
	"flag"
	"log"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"landrop/signaling/internal/server"
)

func main() {
	port := flag.String("port", getEnv("PORT", "8080"), "Signaling server port")
	turnSecret := flag.String("turn-secret", getEnv("TURN_SECRET", "local-test-turn-secret"), "Coturn shared secret")
	stunRaw := flag.String("stun", getEnv("STUN_SERVERS", "stun:stun.l.google.com:19302,stun:global.stun.twilio.com:3478"), "Comma-separated STUN URLs")
	turnRaw := flag.String("turn", getEnv("TURN_SERVERS", "turn:127.0.0.1:3478?transport=udp,turn:127.0.0.1:3478?transport=tcp"), "Comma-separated TURN URLs")
	webDirFlag := flag.String("web-dir", getEnv("WEB_DIR", ""), "Path to static web assets")
	flag.Parse()

	stunList := splitNonEmpty(*stunRaw, ",")
	turnList := splitNonEmpty(*turnRaw, ",")

	webDir := *webDirFlag
	if webDir == "" {
		candidates := []string{"web/dist", "../../web/dist", "../web/dist"}
		for _, c := range candidates {
			if fi, err := os.Stat(c); err == nil && fi.IsDir() {
				webDir = c
				break
			}
		}
	}

	cfg := server.Config{
		Port:        *port,
		TurnSecret:  *turnSecret,
		StunURLs:    stunList,
		TurnURLs:    turnList,
		RoomTTL:     10 * time.Minute,
		MaxFailures: 5,
		RateWindow:  10 * time.Minute,
		BlockTime:   10 * time.Minute,
		WebDir:      webDir,
	}


	srvInstance := server.NewServer(cfg)
	defer srvInstance.Close()

	httpServer := &http.Server{
		Addr:         ":" + *port,
		Handler:      srvInstance.Handler(),
		ReadTimeout:  15 * time.Second,
		WriteTimeout: 15 * time.Second,
	}

	go func() {
		log.Printf("LAN-Drop Signaling Service v2.0 listening on :%s", *port)
		if err := httpServer.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("Server error: %v", err)
		}
	}()

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	<-sigChan

	log.Println("Shutting down signaling server gracefully...")
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	if err := httpServer.Shutdown(ctx); err != nil {
		log.Fatalf("Server shutdown failed: %v", err)
	}
	log.Println("Signaling server terminated cleanly.")
}

func getEnv(key, fallback string) string {
	if val := os.Getenv(key); val != "" {
		return val
	}
	return fallback
}

func splitNonEmpty(s, sep string) []string {
	var result []string
	for _, part := range strings.Split(s, sep) {
		trimmed := strings.TrimSpace(part)
		if trimmed != "" {
			result = append(result, trimmed)
		}
	}
	return result
}
