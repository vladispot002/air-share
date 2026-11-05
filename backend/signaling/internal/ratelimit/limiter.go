package ratelimit

import (
	"sync"
	"time"
)

type ipRecord struct {
	failures  []time.Time
	blockedTo time.Time
}

// Limiter tracks failed attempts per IP address to guard against brute-force attacks.
type Limiter struct {
	mu          sync.Mutex
	maxFailures int
	window      time.Duration
	blockTime   time.Duration
	records     map[string]*ipRecord
	stopCleanup chan struct{}
}

// NewLimiter creates an IP-based rate limiter with configurable thresholds.
func NewLimiter(maxFailures int, window, blockTime time.Duration) *Limiter {
	l := &Limiter{
		maxFailures: maxFailures,
		window:      window,
		blockTime:   blockTime,
		records:     make(map[string]*ipRecord),
		stopCleanup: make(chan struct{}),
	}

	go l.cleanupLoop()
	return l
}

// Close terminates background cleanup goroutine.
func (l *Limiter) Close() {
	select {
	case <-l.stopCleanup:
	default:
		close(l.stopCleanup)
	}
}

// Allow checks if the given IP address is permitted to attempt an action.
func (l *Limiter) Allow(ip string) bool {
	l.mu.Lock()
	defer l.mu.Unlock()

	now := time.Now()
	rec, exists := l.records[ip]
	if !exists {
		return true
	}

	if now.Before(rec.blockedTo) {
		return false
	}

	// Filter out failures outside the window
	validFailures := rec.failures[:0]
	cutoff := now.Add(-l.window)
	for _, t := range rec.failures {
		if t.After(cutoff) {
			validFailures = append(validFailures, t)
		}
	}
	rec.failures = validFailures

	return len(rec.failures) < l.maxFailures
}

// RecordFailure notes a failed attempt from the given IP address.
func (l *Limiter) RecordFailure(ip string) {
	l.mu.Lock()
	defer l.mu.Unlock()

	now := time.Now()
	rec, exists := l.records[ip]
	if !exists {
		rec = &ipRecord{}
		l.records[ip] = rec
	}

	// Filter out expired failures
	validFailures := rec.failures[:0]
	cutoff := now.Add(-l.window)
	for _, t := range rec.failures {
		if t.After(cutoff) {
			validFailures = append(validFailures, t)
		}
	}
	rec.failures = append(validFailures, now)

	if len(rec.failures) >= l.maxFailures {
		rec.blockedTo = now.Add(l.blockTime)
	}
}

// RecordSuccess clears or reduces the penalty for an IP upon successful action.
func (l *Limiter) RecordSuccess(ip string) {
	l.mu.Lock()
	defer l.mu.Unlock()

	delete(l.records, ip)
}

func (l *Limiter) cleanupLoop() {
	ticker := time.NewTicker(1 * time.Minute)
	defer ticker.Stop()

	for {
		select {
		case <-l.stopCleanup:
			return
		case now := <-ticker.C:
			l.mu.Lock()
			cutoff := now.Add(-l.window)
			for ip, rec := range l.records {
				if now.After(rec.blockedTo) {
					validCount := 0
					for _, t := range rec.failures {
						if t.After(cutoff) {
							validCount++
						}
					}
					if validCount == 0 {
						delete(l.records, ip)
					}
				}
			}
			l.mu.Unlock()
		}
	}
}
