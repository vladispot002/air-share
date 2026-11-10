package tests

import (
	"fmt"
	"net/http/httptest"
	"sync"
	"testing"
	"time"

	"landrop/signaling/internal/ratelimit"
	"landrop/signaling/internal/server"
)

// TC-SEC-SPOOF: X-Forwarded-For spoofing protection test
func TestXForwardedForSpoofingProtection(t *testing.T) {
	cfg := server.Config{
		TurnSecret:   "test-secret",
		TrustProxy:   true,
		TrustedCIDRs: []string{"127.0.0.1/32", "172.18.0.0/16"},
	}
	srv := server.NewServer(cfg)
	defer srv.Close()

	// 1. Untrusted RemoteAddr sending spoofed X-Forwarded-For
	reqUntrusted := httptest.NewRequest("GET", "http://drop.example.com/healthz", nil)
	reqUntrusted.RemoteAddr = "198.51.100.25:44556" // Public untrusted IP
	reqUntrusted.Header.Set("X-Forwarded-For", "203.0.113.99")

	// Since RemoteAddr is untrusted, XFF should be completely ignored
	extractedUntrusted := srv.ExtractClientIPForTesting(reqUntrusted)
	if extractedUntrusted != "198.51.100.25" {
		t.Errorf("Expected untrusted client IP 198.51.100.25, got %s (XFF spoofing vulnerability!)", extractedUntrusted)
	}

	// 2. Trusted RemoteAddr (e.g. Caddy at 172.18.0.2)
	reqTrusted := httptest.NewRequest("GET", "http://drop.example.com/healthz", nil)
	reqTrusted.RemoteAddr = "172.18.0.2:38920" // Trusted reverse proxy
	reqTrusted.Header.Set("X-Forwarded-For", "attacker-spoofed-ip, 203.0.113.99")

	// From trusted proxy, rightmost untrusted hop is the true client IP
	extractedTrusted := srv.ExtractClientIPForTesting(reqTrusted)
	if extractedTrusted != "203.0.113.99" {
		t.Errorf("Expected extracted client IP 203.0.113.99 from trusted proxy, got %s", extractedTrusted)
	}
}

// TC-SEC-ISOLATION: Rate Limiter user isolation test
func TestRateLimiterUserIsolation(t *testing.T) {
	limiter := ratelimit.NewLimiter(5, 10*time.Minute, 10*time.Minute)
	defer limiter.Close()

	ipAttacker := "198.51.100.1"
	ipLegitimate := "198.51.100.2"

	// Attacker fails 5 times
	for i := 0; i < 5; i++ {
		limiter.RecordFailure(ipAttacker)
	}

	// 6th attempt from attacker should be blocked
	if limiter.Allow(ipAttacker) {
		t.Errorf("Attacker should be blocked after 5 failures")
	}

	// Legitimate user from different IP must be completely unhindered
	if !limiter.Allow(ipLegitimate) {
		t.Errorf("Legitimate user must NOT be blocked by attacker's activity (lack of IP isolation)")
	}
}

// TC-SEC-OVERFLOW: Bounded limiter under high-volume load (100,000 distinct IPs)
func TestRateLimiterHighLoadCapacity(t *testing.T) {
	// Create limiter with default capacity bound (50,000)
	limiter := ratelimit.NewLimiter(5, 10*time.Minute, 10*time.Minute)
	defer limiter.Close()

	const numIPs = 100000
	var wg sync.WaitGroup
	workers := 10

	for w := 0; w < workers; w++ {
		wg.Add(1)
		go func(workerID int) {
			defer wg.Done()
			for i := 0; i < numIPs/workers; i++ {
				ip := fmt.Sprintf("10.%d.%d.%d", workerID, (i>>8)&0xFF, i&0xFF)
				limiter.RecordFailure(ip)
			}
		}(w)
	}

	wg.Wait()

	// Verify limiter remains healthy and does not crash or panic
	testIP := "192.168.1.1"
	if !limiter.Allow(testIP) {
		t.Errorf("Limiter failed to evaluate healthy IP after load test")
	}
}
