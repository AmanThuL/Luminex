//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorPanel.h
/// @brief Declares the Inspector panel's drawing entry point and the state it edits.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/DynamicResolution.h"
#include "App/EditorRenderSettings.h"
#include "App/EditorSelection.h"
#include "App/ExposureReset.h"
#include "App/TemporalEditorState.h"
#include "Engine/Scene.h"
#include "Render/Camera.h"
#include "Render/Renderer.h"

namespace lmx::app {

/// The Dear ImGui window name this panel submits. The shell's dock builder places the window under
/// exactly this name, so both sides read it from here.
inline constexpr const char* kInspectorPanelWindowName = "Inspector";

/// The editor state the Inspector draws and edits, borrowed for the duration of one draw call.
///
/// Every reference names storage the shell owns, so the panel edits the shell's values in place and
/// holds nothing past the call that resolved them. `selection` is the one exception: it is a value
/// the shell already resolved this frame (spec section 5), not a reference, so the Inspector never
/// caches a pointer into `scene` that a scene switch or vector mutation could dangle.
struct InspectorPanelContext {
    EditorSelection selection;  ///< The resolved subject to draw; `None` shows the empty state.
    render::Camera& camera;     ///< The fly camera; angles are presented in degrees.
    render::Renderer& renderer; ///< Borrowed for the scene target's clear color only.
    engine::Scene& scene;       ///< The active scene, whose lights and objects are edited in place.
    EditorRenderSettings& settings;        ///< Editor-owned render knobs.
    ExposureResetContext& exposureContext; ///< What `shouldResetExposure` last compared against.
    /// Raised when an edit here is one of the exposure reset triggers, and consumed by the frame
    /// loop rather than by this panel.
    bool& exposureResetPending;
    /// The scene generation counter, camera-cut latch, and TemporalLab defaults the shell keeps
    /// (TemporalEditorState.h). The Temporal block's "Camera cut" button raises the latch here.
    TemporalEditorState& temporalState;
    /// The dynamic-resolution controller's last-observed frame GPU time (DynamicResolution.h),
    /// for the Temporal block's "Frame GPU time" status row. Read-only here -- the shell's
    /// applyDynamicResolution() is what advances it, once per buildUI.
    const DynamicResolutionState& dynamicResolutionState;
    const rhi::TemporalScalerSupport&
        temporalSupport; ///< Fixed device capability and display name.
};

/// Draws the Inspector panel over exactly one subject (spec section 7): its kind and display name,
/// then only the fields that subject owns. `None` shows `Select an item in Scene` and no editable
/// fallback section. `open` follows the window's close button, exactly as `ImGui::Begin` writes it.
///
/// Interactive ranges clamp to values the renderer accepts -- a light direction is never stored as
/// a zero vector, a camera's near clip never reaches its far clip, and an object's scale never
/// reaches zero.
void drawInspectorPanel(bool& open, const InspectorPanelContext& context);

} // namespace lmx::app
