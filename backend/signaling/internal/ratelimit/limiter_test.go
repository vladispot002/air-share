package ratelimit

import (
	"testing"
	"time"
)

func TestLimiterBlocking(t *testing.T) {
	limiter := NewLimiter(5, 100*time.Millisecond, 200*time.Millisecond)
	defer limiter.Close()

	ip := "192.168.1.50"

	// 4 failures should still be allowed
	for i := 0; i < 4; i++ {
		if !limiter.Allow(ip) {
			t.Fatalf("expected attempt %d to be allowed", i+1)
		}
		limiter.RecordFailure(ip)
	}

	// 5th attempt allowed
	if !limiter.Allow(ip) {
		t.Fatalf("expected 5th attempt to be allowed")
	}
	limiter.RecordFailure(ip)

	// Now 5 failures recorded, 6th attempt must be blocked
	if limiter.Allow(ip) {
		t.Fatalf("expected 6th attempt to be blocked")
	}

	// Another IP is unaffected
	otherIP := "192.168.1.51"
	if !limiter.Allow(otherIP) {
		t.Fatalf("other IP should be allowed")
	}

	// After block time expires, original IP is allowed again
	time.Sleep(250 * time.Millisecond)
	if !limiter.Allow(ip) {
		t.Fatalf("expected IP to be allowed after block duration expired")
	}
}

func TestLimiterSuccessClears(t *testing.T) {
	limiter := NewLimiter(3, 1*time.Minute, 1*time.Minute)
	defer limiter.Close()

	ip := "10.0.0.1"
	limiter.RecordFailure(ip)
	limiter.RecordFailure(ip)

	// Record success should clear history
	limiter.RecordSuccess(ip)

	for i := 0; i < 2; i++ {
		if !limiter.Allow(ip) {
			t.Fatalf("expected attempt %d after success to be allowed", i+1)
		}
		limiter.RecordFailure(ip)
	}
	if !limiter.Allow(ip) {
		t.Fatalf("should still allow after 2 failures")
	}
}
