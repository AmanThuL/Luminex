//----------------------------------------------------------------------------------------------------------------------
/// @file NoticeQueue.cpp
/// @brief Applies notice replacement, dismissal and success expiration.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Capture/NoticeQueue.h"

#include <utility>

namespace lmx::app {

//======================================================================================================================
void NoticeQueue::post(ActionResult result, double nowSeconds) {
    m_result = std::move(result);
    m_postedSeconds = nowSeconds;
    m_visible =
        m_result.status != ActionStatus::Ready &&
        (!m_result.message.empty() || !m_result.path.empty() || !m_result.pathActionError.empty());
}

//======================================================================================================================
const ActionResult* NoticeQueue::current(double nowSeconds) const {
    // A clipboard/Finder failure written onto the stored result keeps it until dismissal.
    const bool expires =
        m_result.status == ActionStatus::Succeeded && m_result.pathActionError.empty();
    if (!m_visible || (expires && nowSeconds - m_postedSeconds >= kSuccessSeconds)) {
        return nullptr;
    }
    return &m_result;
}

//======================================================================================================================
ActionResult* NoticeQueue::current(double nowSeconds) {
    return std::as_const(*this).current(nowSeconds) ? &m_result : nullptr;
}

//======================================================================================================================
void NoticeQueue::dismiss() {
    m_visible = false;
}

} // namespace lmx::app
