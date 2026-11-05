#pragma once

#include <string>
#include <queue>
#include <vector>
#include <mutex>
#include <functional>

namespace landrop {

/**
 * @brief Represents a single WebRTC Trickle ICE candidate.
 */
struct IceCandidate {
    std::string candidate;
    std::string mid;
    int mlineIndex{0};
};

/**
 * @brief Thread-safe ICE candidate queue that buffers remote trickle candidates
 * arriving prior to remote SDP description completion (TC-RTC-01).
 */
class CandidateQueue {
public:
    static constexpr size_t kMaxQueueCapacity = 256;

    CandidateQueue() = default;
    ~CandidateQueue() = default;

    /**
     * @brief Pushes an incoming candidate. If remote description is already set and
     * a callback is registered, the candidate is delivered sequentially. Otherwise, it is buffered.
     */
    bool push(IceCandidate cand) {
        std::unique_lock<std::recursive_mutex> deliveryLock(m_deliveryMutex);
        std::vector<IceCandidate> toDeliver;
        std::function<void(const IceCandidate&)> cb;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_remoteDescriptionSet || !m_onCandidateReady) {
                if (m_queue.size() >= kMaxQueueCapacity) {
                    return false; // Bounded capacity: mitigate DoS flood (F-12)
                }
                m_queue.push(std::move(cand));
                return true;
            }
            cb = m_onCandidateReady;
            while (!m_queue.empty()) {
                toDeliver.push_back(std::move(m_queue.front()));
                m_queue.pop();
            }
            toDeliver.push_back(std::move(cand));
        }

        if (cb) {
            for (const auto& c : toDeliver) {
                cb(c);
            }
        }
        return true;
    }

    /**
     * @brief Marks remote description as set. If a callback is registered, drains all
     * buffered candidates sequentially. If not yet registered, candidates are preserved
     * in queue until setOnCandidateReady is called (F-11).
     */
    void setRemoteDescriptionSet() {
        std::unique_lock<std::recursive_mutex> deliveryLock(m_deliveryMutex);
        std::vector<IceCandidate> toDeliver;
        std::function<void(const IceCandidate&)> cb;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_remoteDescriptionSet = true;
            if (!m_onCandidateReady) {
                return; // Retain in queue until callback is attached (F-11)
            }
            cb = m_onCandidateReady;
            while (!m_queue.empty()) {
                toDeliver.push_back(std::move(m_queue.front()));
                m_queue.pop();
            }
        }

        if (cb) {
            for (const auto& cand : toDeliver) {
                cb(cand);
            }
        }
    }

    bool isRemoteDescriptionSet() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_remoteDescriptionSet;
    }

    size_t pendingCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

    /**
     * @brief Sets the callback for candidate delivery. If remote description is already
     * set and candidates are buffered, drains them immediately (F-11).
     */
    void setOnCandidateReady(std::function<void(const IceCandidate&)> cb) {
        std::unique_lock<std::recursive_mutex> deliveryLock(m_deliveryMutex);
        std::vector<IceCandidate> toDeliver;
        std::function<void(const IceCandidate&)> targetCb;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_onCandidateReady = std::move(cb);
            if (m_remoteDescriptionSet && m_onCandidateReady) {
                targetCb = m_onCandidateReady;
                while (!m_queue.empty()) {
                    toDeliver.push_back(std::move(m_queue.front()));
                    m_queue.pop();
                }
            }
        }

        if (targetCb) {
            for (const auto& cand : toDeliver) {
                targetCb(cand);
            }
        }
    }

    void reset() {
        std::unique_lock<std::recursive_mutex> deliveryLock(m_deliveryMutex);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_remoteDescriptionSet = false;
        m_onCandidateReady = nullptr;
        while (!m_queue.empty()) {
            m_queue.pop();
        }
    }

private:
    mutable std::mutex m_mutex;
    mutable std::recursive_mutex m_deliveryMutex;
    bool m_remoteDescriptionSet{false};
    std::queue<IceCandidate> m_queue;
    std::function<void(const IceCandidate&)> m_onCandidateReady;
};

} // namespace landrop
