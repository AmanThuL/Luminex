//----------------------------------------------------------------------------------------------------------------------
/// @file MetricsContextRevision.cpp
/// @brief Implements monotonic editor measurement revisions across configuration re-entry.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Performance/MetricsContextRevision.h"

namespace lmx::app {

//======================================================================================================================
uint64_t MetricsContextRevision::observe(uint64_t configurationKey) {
    if (!m_configurationKey || *m_configurationKey != configurationKey) {
        m_configurationKey = configurationKey;
        ++m_revision;
    }
    return m_revision;
}

} // namespace lmx::app
