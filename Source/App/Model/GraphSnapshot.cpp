//----------------------------------------------------------------------------------------------------------------------
/// @file GraphSnapshot.cpp
/// @brief Publishes coherent graph records and their exact matched timing sets.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/GraphSnapshot.h"

namespace lmx::app {

//======================================================================================================================
void GraphSnapshot::update(double nowSeconds, const RetainedFrame* newest) {
    if (m_frozen || newest == nullptr || (m_frame && nowSeconds < m_nextPublishSeconds)) {
        return;
    }
    m_frame = *newest;
    if (!m_frame->timed) {
        m_frame->timings.clear();
    }
    m_nextPublishSeconds = nowSeconds + kDiagnosticRefreshIntervalSeconds;
}

//======================================================================================================================
void GraphSnapshot::freeze() {
    m_frozen = m_frame.has_value();
}

//======================================================================================================================
void GraphSnapshot::resume() {
    m_frozen = false;
    m_frame.reset();
    m_nextPublishSeconds = 0.0;
}

//======================================================================================================================
const RetainedFrame* GraphSnapshot::displayed() const {
    return m_frame ? &*m_frame : nullptr;
}

} // namespace lmx::app
