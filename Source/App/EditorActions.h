//----------------------------------------------------------------------------------------------------------------------
/// @file EditorActions.h
/// @brief Declares the App-owned action intents the main menu and shortcuts emit.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

namespace lmx::app {

/// Pending menu- and shortcut-triggered action intents (spec section 9's action boundary).
///
/// Drawing the menu, or handling an equivalent keyboard shortcut, only records that an action was
/// requested. It never quits SDL, waits on the device, resizes a target, or begins a Metal capture
/// directly; the frame loop consumes each intent at the boundary that already owns the operation.
/// Because a request is nothing more than setting a flag, the same intent can be raised from more
/// than one input source (menu and keyboard, for instance) without duplicating the work it
/// triggers.
///
/// The three intents -- quit, capture-next-frame, and reset-layout -- are independent: requesting,
/// inspecting, or consuming one never affects another. Requesting an intent already pending changes
/// nothing, so repeated gestures before consumption still leave exactly one pending occurrence. An
/// unconsumed intent survives frames whose caller does not consume it (a skipped drawable, for
/// instance); consuming it clears it regardless of what the caller does with the answer, so one
/// failed attempt is logged and not retried on the next frame.
class EditorActions {
public:
    /// Records a request to quit, coalescing with any request already pending.
    void requestQuit();
    /// Whether a quit request is pending, without consuming it.
    bool quitPending() const { return m_quitPending; }
    /// Consumes a pending quit request, clearing it. Returns whether one was pending.
    bool consumeQuit();

    /// Records a request to capture the next frame that successfully acquires a drawable,
    /// coalescing with any request already pending. The menu and the existing keyboard/environment
    /// triggers all raise this same intent, so duplicate gestures still leave exactly one pending.
    void requestCapture();
    /// Whether a capture request is pending, without consuming it.
    bool capturePending() const { return m_capturePending; }
    /// Consumes a pending capture request, clearing it. Returns whether one was pending. The caller
    /// performs the capture attempt and logs failure itself -- one attempt consumes the request
    /// regardless of whether it succeeds.
    bool consumeCapture();

    /// Records a request to reset the default dock layout, coalescing with any request already
    /// pending.
    void requestResetLayout();
    /// Whether a reset-layout request is pending, without consuming it.
    bool resetLayoutPending() const { return m_resetLayoutPending; }
    /// Consumes a pending reset-layout request, clearing it. Returns whether one was pending.
    bool consumeResetLayout();

private:
    bool m_quitPending = false;
    bool m_capturePending = false;
    bool m_resetLayoutPending = false;
};

} // namespace lmx::app
