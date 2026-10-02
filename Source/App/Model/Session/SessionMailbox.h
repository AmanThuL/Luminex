//----------------------------------------------------------------------------------------------------------------------
/// @file SessionMailbox.h
/// @brief Declares the thread-safe exchange between the session listener and editor.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lmx::app {

/// A complete client line or an ordered connection lifecycle event.
struct InboundLine {
    uint64_t connection = 0; ///< Listener-generated connection identity.
    std::string text;        ///< Complete line without LF, empty for lifecycle events.
    bool opened = false;     ///< The peer was accepted and authenticated.
    bool closed = false;     ///< The peer disconnected or was rejected.
};

/// Bounded inbound events and response handoff shared by two threads.
class SessionMailbox {
public:
    /// Adds an event when fewer than 64 inbound events are pending; returns false when full.
    bool pushInbound(InboundLine line);
    /// Retains one close event after a failed push, appending it if the editor drained meanwhile
    /// or deferring it until the older 64 events drain. A second deferred close is refused.
    bool retainClose(uint64_t connection);
    /// Takes every pending request and lifecycle event in arrival order.
    std::vector<InboundLine> takeInbound();
    /// Whether another inbound item can be queued without exceeding 64 entries or passing a close.
    bool hasInboundCapacity();
    /// Queues a complete encoded response for the connection.
    void pushOutbound(uint64_t connection, std::string line);
    /// Takes all pending encoded responses.
    std::vector<std::pair<uint64_t, std::string>> takeOutbound();
    /// Takes connections whose outbound response could not fit and must be closed.
    std::vector<uint64_t> takeOutboundClosures();

private:
    std::mutex m_mutex;
    std::deque<InboundLine> m_inbound;
    std::optional<InboundLine> m_deferredClose;
    std::deque<std::pair<uint64_t, std::string>> m_outbound;
    size_t m_outboundBytes = 0;
    std::vector<uint64_t> m_outboundClosures;
};

} // namespace lmx::app
