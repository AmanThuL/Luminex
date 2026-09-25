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
    if (!m_visible || (m_result.status == ActionStatus::Succeeded &&
                       nowSeconds - m_postedSeconds >= kSuccessSeconds)) {
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
