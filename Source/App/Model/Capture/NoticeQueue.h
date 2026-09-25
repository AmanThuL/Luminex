//----------------------------------------------------------------------------------------------------------------------
/// @file NoticeQueue.h
/// @brief Retains the latest editor notice with bounded success visibility.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Capture/ActionResult.h"

namespace lmx::app {

/// Owns one replaceable notice; callers supply monotonic time in seconds.
class NoticeQueue {
public:
    /// Duration for a successful result before it disappears automatically.
    static constexpr double kSuccessSeconds = 6.0;

    /// Replaces the current notice. Ready or a result with no text or path clears it.
    void post(ActionResult result, double nowSeconds);
    /// Borrows the visible notice, or null when empty, dismissed or expired; valid until post.
    const ActionResult* current(double nowSeconds) const;
    /// Borrows mutable feedback so clipboard/Finder errors remain with the visible result.
    ActionResult* current(double nowSeconds);
    /// Hides the current notice until another result is posted.
    void dismiss();

private:
    ActionResult m_result;
    double m_postedSeconds = 0.0;
    bool m_visible = false;
};

} // namespace lmx::app
