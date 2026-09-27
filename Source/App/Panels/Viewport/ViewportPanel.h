//----------------------------------------------------------------------------------------------------------------------
/// @file ViewportPanel.h
/// @brief Declares the Viewport panel's drawing entry point, overlay state, and per-frame extent.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/Rendering/Settings/EditorRenderSettings.h"
#include "App/Model/Rendering/Temporal/TemporalEditorState.h"
#include "App/Model/Rendering/Visibility/VisibilityDisplay.h"
#include "App/Model/Scene/EditorSelection.h"
#include "Engine/Scene/Scene.h"
#include "Render/Renderer/Renderer.h"

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
    float backingScale = 1.0f; ///< Scale of this panel's own platform window.
    /// Whether the pointer is over the panel. This is what gates camera look.
    bool hovered = false;
    /// Whether the panel has keyboard focus. Display-only.
    bool focused = false;
};

/// State borrowed while drawing the scene image and diagnostic overlays.
struct ViewportPanelContext {
    render::Renderer& renderer;         ///< Scene target and retired diagnostics.
    rojoRHI::Texture& outlineTarget;    ///< Separate editor selection presentation.
    bool& showOutline;                  ///< Global selection outline preference.
    const engine::Scene& scene;         ///< Selected geometry and enabled light count.
    EditorRenderSettings& settings;     ///< Diagnostic request edited by the chip.
    TemporalEditorState& temporalState; ///< Provenance for retired occlusion overlays.
    EditorSelection selection;          ///< Resolved selection for editor-only overlays.
    /// Reconstruction after vendor fallback; gates the chip's native-only temporal entries.
    render::ReconstructionMode effectiveReconstruction;
    const VisibilityDisplay* visibilityDisplay = nullptr; ///< Matched retired bounds.
};

/// Number of HZB levels `render::hzbLayout` allocates for the renderer's current output extent;
/// zero while that extent is empty. The View menu and the viewport chip share this count.
uint32_t viewportHzbLevels(const render::Renderer& renderer);

/// Draws the scene image over the full content region, with diagnostic overlays when active.
/// Reports its backing-pixel extent for the shell's debounced resize; closing updates `open`.
ViewportPanelResult drawViewportPanel(bool& open, const ViewportPanelContext& context);

} // namespace lmx::app
