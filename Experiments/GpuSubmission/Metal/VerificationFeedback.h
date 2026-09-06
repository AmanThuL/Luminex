//----------------------------------------------------------------------------------------------------------------------
/// @file VerificationFeedback.h
/// @brief Tracks verification commit delivery independently of native resource ownership.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Model/Workload.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>

namespace lmx::experimental::submission::detail {

/// CPU-only callback ledger; a captured shared owner may safely outlive the native host.
class VerificationFeedback {
public:
    /// Reserves callback bookkeeping before warmup; contains no Metal objects or borrowed pointers.
    explicit VerificationFeedback(size_t capacity) { m_delivered.reserve(capacity); }

    /// Registers a commit before submission and returns its one-based immutable sequence.
    uint64_t recordSubmission() {
        std::lock_guard lock(m_mutex);
        m_delivered.push_back(0);
        return m_delivered.size();
    }

    /// Records callback completion, retaining the first diagnostic even after later successes.
    /// Call after extracting native error data and draining the callback's autorelease pool.
    void complete(uint64_t sequence, std::string error) {
        std::lock_guard lock(m_mutex);
        if (sequence == 0 || sequence > m_delivered.size() || m_delivered[sequence - 1]) {
            if (m_error.empty()) {
                m_error =
                    "Duplicate or unknown verification commit callback " + std::to_string(sequence);
            }
        } else {
            if (!error.empty() && m_error.empty()) {
                m_error = std::move(error);
            }
            m_delivered[sequence - 1] = 1;
            while (m_frontier < m_delivered.size() && m_delivered[m_frontier]) {
                ++m_frontier;
            }
        }
        m_ready.notify_all();
    }

    /// Waits for every callback through sequence, including after errors; never mistakes an error
    /// for proof that later submissions completed. Returns failure for errors or missing delivery.
    Result<void> waitThrough(uint64_t sequence, std::chrono::steady_clock::time_point deadline) {
        std::unique_lock lock(m_mutex);
        if (sequence > m_delivered.size()) {
            return std::unexpected("Verification wait references an unsubmitted commit");
        }
        if (!m_ready.wait_until(lock, deadline, [&] { return m_frontier >= sequence; })) {
            return std::unexpected("Verification callback delivery timed out through commit " +
                                   std::to_string(sequence) + " (delivered through " +
                                   std::to_string(m_frontier) + ")" +
                                   (m_error.empty() ? "" : "; first GPU error: " + m_error));
        }
        if (!m_error.empty()) {
            return std::unexpected(m_error);
        }
        return {};
    }

    /// Reports completion separately from success, so failed-but-completed work can be torn down.
    bool deliveredThrough(uint64_t sequence) const {
        std::lock_guard lock(m_mutex);
        return m_frontier >= sequence;
    }

    /// Reports requested CPU ledger storage; query before concurrent callback delivery begins.
    uint64_t requestedBytes() const {
        return sizeof(*this) + m_delivered.capacity() * sizeof(uint8_t);
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_ready;
    std::vector<uint8_t> m_delivered;
    uint64_t m_frontier = 0;
    std::string m_error;
};

} // namespace lmx::experimental::submission::detail
