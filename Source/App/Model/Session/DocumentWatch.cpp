//----------------------------------------------------------------------------------------------------------------------
/// @file DocumentWatch.cpp
/// @brief Waits for a stable document pair and one attributed proposal decision.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Session/DocumentWatch.h"

namespace lmx::app {

//======================================================================================================================
void DocumentWatch::reset(const FileStamp& loaded) {
    m_loaded = loaded;
    m_observed = {};
    m_lastPoll = -1.0;
    m_graceStart = -1.0;
    m_hash.clear();
    m_observedOnce = false;
    m_hashed = false;
    m_ready = false;
}

//======================================================================================================================
bool DocumentWatch::adoptSave(const FileStamp& beforeHash, const FileStamp& afterHash,
                              const std::filesystem::path& watchedPath,
                              const std::filesystem::path& writtenPath,
                              std::string_view writtenHash, std::string_view currentHash,
                              bool documentAdopted) {
    if (writtenHash.empty() || writtenHash != currentHash || beforeHash != afterHash ||
        afterHash.saving || watchedPath.lexically_normal() != writtenPath.lexically_normal()) {
        if (documentAdopted)
            reset(FileStamp{});
        return false;
    }
    reset(afterHash);
    return true;
}

//======================================================================================================================
bool DocumentWatch::due(double seconds) const {
    return m_lastPoll < 0.0 || seconds - m_lastPoll >= 0.5;
}

//======================================================================================================================
WatchDecision DocumentWatch::poll(const FileStamp& now, double seconds) {
    if (!due(seconds))
        return WatchDecision::Wait;
    m_lastPoll = seconds;
    if (now.saving || now == m_loaded) {
        m_observedOnce = false;
        m_hashed = false;
        m_ready = false;
        return WatchDecision::Wait;
    }
    if (!m_observedOnce || now != m_observed) {
        m_observed = now;
        m_observedOnce = true;
        m_hashed = false;
        m_ready = false;
        m_graceStart = -1.0;
        return WatchDecision::Wait;
    }
    if (!m_hashed)
        return WatchDecision::Hash;
    if (m_ready)
        return WatchDecision::Wait;
    if (m_graceStart >= 0.0 && seconds - m_graceStart >= 4.0)
        m_ready = true;
    return WatchDecision::Sidecar;
}

//======================================================================================================================
void DocumentWatch::hashed(std::string_view hash, bool sidecarMatches, double seconds) {
    if (!m_hashed || hash != m_hash) {
        m_hash = hash;
        m_graceStart = seconds;
    }
    m_hashed = true;
    m_ready = sidecarMatches || seconds - m_graceStart >= 4.0;
}

} // namespace lmx::app
