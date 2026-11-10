package ratelimit

import (
	"sync"
	"time"
)

const defaultMaxCapacity = 50000

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
	maxCapacity int
	records     map[string]*ipRecord
	stopCleanup chan struct{}
}

// NewLimiter creates an IP-based rate limiter with configurable thresholds.
func NewLimiter(maxFailures int, window, blockTime time.Duration) *Limiter {
	l := &Limiter{
		maxFailures: maxFailures,
		window:      window,
		blockTime:   blockTime,
		maxCapacity: defaultMaxCapacity,
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

	// Filter out failures outside the sliding window
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
		// Enforce capacity bound to prevent OOM
		if len(l.records) >= l.maxCapacity {
			l.pruneOldestLocked(now)
			if len(l.records) >= l.maxCapacity {
				for evictIP := range l.records {
					delete(l.records, evictIP)
					break
				}
			}
		}
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

// RecordSuccess decrements the failure penalty without allowing instant brute-force bypass.
func (l *Limiter) RecordSuccess(ip string) {
	l.mu.Lock()
	defer l.mu.Unlock()

	rec, exists := l.records[ip]
	if !exists {
		return
	}

	if len(rec.failures) > 0 {
		// Reduce failure penalty by 1
		rec.failures = rec.failures[1:]
	}
	if len(rec.failures) == 0 && time.Now().After(rec.blockedTo) {
		delete(l.records, ip)
	}
}

func (l *Limiter) pruneOldestLocked(now time.Time) {
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
		// If capacity is recovered, break early
		if len(l.records) < l.maxCapacity {
			return
		}
	}
}

func (l *Limiter) cleanupLoop() {
	ticker := time.NewTicker(30 * time.Second)
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
