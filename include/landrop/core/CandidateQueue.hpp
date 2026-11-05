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
    CandidateQueue() = default;
    ~CandidateQueue() = default;

    /**
     * @brief Pushes an incoming candidate. If remote description is already set,
     * the candidate callback is fired immediately. Otherwise, it is buffered.
     */
    void push(IceCandidate cand) {
        std::function<void(const IceCandidate&)> callback;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_remoteDescriptionSet) {
                callback = m_onCandidateReady;
            } else {
                m_queue.push(std::move(cand));
                return;
            }
        }
        if (callback) {
            callback(cand);
        }
    }

    /**
     * @brief Marks remote description as set and drains all buffered candidates
     * sequentially to the registered callback.
     */
    void setRemoteDescriptionSet() {
        std::vector<IceCandidate> pending;
        std::function<void(const IceCandidate&)> callback;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_remoteDescriptionSet = true;
            callback = m_onCandidateReady;
            while (!m_queue.empty()) {
                pending.push_back(std::move(m_queue.front()));
                m_queue.pop();
            }
        }
        if (callback) {
            for (const auto& cand : pending) {
                callback(cand);
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

    void setOnCandidateReady(std::function<void(const IceCandidate&)> cb) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_onCandidateReady = std::move(cb);
    }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_remoteDescriptionSet = false;
        while (!m_queue.empty()) {
            m_queue.pop();
        }
    }

private:
    mutable std::mutex m_mutex;
    bool m_remoteDescriptionSet{false};
    std::queue<IceCandidate> m_queue;
    std::function<void(const IceCandidate&)> m_onCandidateReady;
};

} // namespace landrop
