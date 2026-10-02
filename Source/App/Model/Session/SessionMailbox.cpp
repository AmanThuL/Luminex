//----------------------------------------------------------------------------------------------------------------------
/// @file SessionMailbox.cpp
/// @brief Implements bounded request and response transfer for a session listener.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Session/SessionMailbox.h"

#include "App/Model/Session/SessionProtocol.h"

#include <algorithm>
#include <utility>

namespace lmx::app {

//======================================================================================================================
bool SessionMailbox::pushInbound(InboundLine line) {
    std::lock_guard lock(m_mutex);
    if (m_inbound.size() == 64)
        return false;
    if (m_deferredClose)
        return false;
    m_inbound.push_back(std::move(line));
    return true;
}

//======================================================================================================================
bool SessionMailbox::retainClose(uint64_t connection) {
    std::lock_guard lock(m_mutex);
    if (m_deferredClose)
        return false;
    if (m_inbound.size() < 64)
        m_inbound.push_back({connection, {}, false, true});
    else
        m_deferredClose = InboundLine{connection, {}, false, true};
    return true;
}

//======================================================================================================================
std::vector<InboundLine> SessionMailbox::takeInbound() {
    std::lock_guard lock(m_mutex);
    std::vector<InboundLine> lines;
    lines.reserve(m_inbound.size());
    while (!m_inbound.empty()) {
        lines.push_back(std::move(m_inbound.front()));
        m_inbound.pop_front();
    }
    if (m_deferredClose) {
        m_inbound.push_back(std::move(*m_deferredClose));
        m_deferredClose.reset();
    }
    return lines;
}

//======================================================================================================================
bool SessionMailbox::hasInboundCapacity() {
    std::lock_guard lock(m_mutex);
    return m_inbound.size() < 64 && !m_deferredClose;
}

//======================================================================================================================
void SessionMailbox::pushOutbound(uint64_t connection, std::string line) {
    std::lock_guard lock(m_mutex);
    constexpr size_t kOutboundCapacity = 4 * kMaxLineBytes;
    if (line.size() > kMaxLineBytes || m_outboundBytes + line.size() > kOutboundCapacity) {
        if (m_outboundClosures.size() == 1 && m_outboundClosures.front() == 0)
            return;
        if (std::find(m_outboundClosures.begin(), m_outboundClosures.end(), connection) !=
            m_outboundClosures.end())
            return;
        if (m_outboundClosures.size() < 64)
            m_outboundClosures.push_back(connection);
        else
            m_outboundClosures = {0};
        return;
    }
    m_outboundBytes += line.size();
    m_outbound.emplace_back(connection, std::move(line));
}

//======================================================================================================================
std::vector<std::pair<uint64_t, std::string>> SessionMailbox::takeOutbound() {
    std::lock_guard lock(m_mutex);
    std::vector<std::pair<uint64_t, std::string>> lines;
    lines.reserve(m_outbound.size());
    while (!m_outbound.empty()) {
        lines.push_back(std::move(m_outbound.front()));
        m_outbound.pop_front();
    }
    m_outboundBytes = 0;
    return lines;
}

//======================================================================================================================
std::vector<uint64_t> SessionMailbox::takeOutboundClosures() {
    std::lock_guard lock(m_mutex);
    return std::exchange(m_outboundClosures, {});
}

} // namespace lmx::app
