//----------------------------------------------------------------------------------------------------------------------
/// @file ViewportPanel.h
/// @brief Declares the Viewport panel's drawing entry point, toolbar state, and per-frame extent.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/EditorActions.h"
#include "App/Model/EditorRenderSettings.h"
#include "App/Model/EditorSelection.h"
#include "App/Model/ExposureReset.h"
#include "App/Model/SceneSession.h"
#include "App/Model/TemporalEditorState.h"
#include "App/Model/VisibilityDisplay.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Types/Camera.h"
#include "Render/Renderer.h"

#include <cstdint>
#include <string_view>

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

/// The editor state the Viewport's toolbar reads and edits, borrowed for the duration of one draw
/// call. `settings`, `exposureContext`, and `exposureResetPending` are the exact storage the
/// Inspector's Rendering section edits (spec section 8): a toolbar toggle and the matching
/// Inspector row are visible to each other on the same UI frame because there is only one value
/// between them, never two.
struct ViewportPanelContext {
    render::Renderer& renderer;      ///< The scene target the panel displays.
    rojoRHI::Texture& outlineTarget; ///< Separate display target for the enabled object cue.
    bool& showOutline; ///< Editor-local cue toggle; leaves framing and capture unchanged.
    std::string_view activeSceneName; ///< Shown in the toolbar.
    render::Camera& camera;           ///< Reset Camera writes the active scene's initial pose here.
    const scene::Scene& scene;        ///< Source of Reset Camera's initial pose.
    EditorRenderSettings& settings;   ///< Editor-owned render knobs the quick toggles edit.
    ExposureResetContext& exposureContext; ///< What `shouldResetExposure` last compared against.
    /// Raised when a toolbar edit is one of the exposure reset triggers, and consumed by the frame
    /// loop rather than by this panel.
    bool& exposureResetPending;
    SceneSession& session;              ///< Shared playback and camera owner.
    TemporalEditorState& temporalState; ///< Camera discontinuity and status provenance.
    EditorSelection selection; ///< Resolved selected subject for editor-only framing and bounds.
    EditorActions& actions;    ///< Shared capture status and action intent.
    scene::SceneId sceneId;    ///< Catalog identity for concise lab context.
    const VisibilityDisplay* visibilityDisplay =
        nullptr; ///< Matched retired bounds and source view.
};

/// Draws the Viewport panel: a compact toolbar (active scene, Reset Camera, and quick toggles for
/// wireframe, exposure mode, bloom, and shadow filter -- spec section 8) followed by the renderer's
/// current color target stretched over the remaining content region. `open` follows the window's
/// close button, exactly as `ImGui::Begin` writes it.
///
/// The panel only measures the region below the toolbar; the caller owns the resize debounce and
/// the target replacement it eventually drives, which is why a pending resize shows as a stretched
/// last-good image for the frames the debounce is still counting. A toolbar of stable height never
/// changes what that region measures on its own.
ViewportPanelResult drawViewportPanel(bool& open, const ViewportPanelContext& context);

} // namespace lmx::app
