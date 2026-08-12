//----------------------------------------------------------------------------------------------------------------------
/// @file ViewportPanel.h
/// @brief Declares the Viewport panel's drawing entry point and its per-frame measurements.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/Renderer.h"

#include <cstdint>

namespace lmx::app {

/// The Dear ImGui window name this panel submits. The shell's dock builder places the window under
/// exactly this name, so both sides read it from here.
inline constexpr const char* kViewportPanelWindowName = "Viewport";

/// What the Viewport panel observed while drawing one frame.
struct ViewportPanelResult {
    /// Whether the panel measured its content region at all. False while the window is collapsed,
    /// in which case the caller keeps the extent it already had rather than resizing the scene
    /// target to nothing.
    bool measured = false;
    /// Content-region width in backing pixels; meaningful only if `measured`.
    uint32_t width = 0;
    /// Content-region height in backing pixels; meaningful only if `measured`.
    uint32_t height = 0;
    /// Whether the pointer is over the panel. This is what gates camera look.
    bool hovered = false;
    /// Whether the panel has keyboard focus. Display-only.
    bool focused = false;
};

/// Draws the Viewport panel: the renderer's current color target stretched over the panel's whole
/// content region. `open` follows the window's close button, exactly as `ImGui::Begin` writes it.
///
/// The panel only measures; the caller owns the resize debounce and the target replacement it
/// eventually drives, which is why a pending resize shows as a stretched last-good image for the
/// frames the debounce is still counting.
ViewportPanelResult drawViewportPanel(bool& open, render::Renderer& renderer);

} // namespace lmx::app
