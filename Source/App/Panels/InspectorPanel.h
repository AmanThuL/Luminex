//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorPanel.h
/// @brief Declares the Inspector panel's drawing entry point and the state it edits.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/LightingDisplay.h"
#include "App/Model/Rendering/Settings/EditorRenderSettings.h"
#include "App/Model/Rendering/Settings/ExposureReset.h"
#include "App/Model/Rendering/Temporal/DynamicResolution.h"
#include "App/Model/Rendering/Temporal/TemporalEditorState.h"
#include "App/Model/Scene/EditorSelection.h"
#include "App/Model/Scene/SceneSession.h"
#include "App/Model/VisibilityDisplay.h"
#include "Engine/Scene/Scene.h"
#include "Engine/View/Camera.h"
#include "Render/Renderer/Renderer.h"

#include <string>

namespace lmx::app {

/// The Dear ImGui window name this panel submits. The shell's dock builder places the window under
/// exactly this name, so both sides read it from here.
inline constexpr const char* kInspectorPanelWindowName = "Inspector";

/// The editor state the Inspector draws and edits, borrowed for the duration of one draw call.
///
/// Every reference names storage the shell owns, so the panel edits the shell's values in place and
/// holds nothing past the call that resolved them. Selection is a resolved editor-local value;
/// overview links may change its rendering category, without borrowing any scene row.
struct InspectorPanelContext {
    EditorSelection& selection;     ///< Resolved subject; overview links select a rendering topic.
    SceneSession& session;          ///< Borrowed active scene, camera, and playback operations.
    render::Renderer& renderer;     ///< Borrowed for clear color and read-only display status.
    EditorRenderSettings& settings; ///< Editor-owned render knobs.
    ExposureResetContext& exposureContext; ///< What `shouldResetExposure` last compared against.
    /// Raised when an edit here is one of the exposure reset triggers, and consumed by the frame
    /// loop rather than by this panel.
    bool& exposureResetPending;
    /// The scene generation counter, camera-cut latch, and TemporalLab defaults the shell keeps
    /// (TemporalEditorState.h). The Temporal block's "Reset history" button raises the latch here.
    TemporalEditorState& temporalState;
    /// The dynamic-resolution controller's last-observed frame GPU time (DynamicResolution.h),
    /// for the Temporal block's "Frame GPU time" status row. Read-only here -- the shell's
    /// applyDynamicResolution() is what advances it, once per buildUI.
    const DynamicResolutionState& dynamicResolutionState;
    const rojoRHI::TemporalScalerSupport&
        temporalSupport;                  ///< Fixed device capability and display name.
    uint32_t viewportWidth = 0;           ///< Measured image backing width in pixels.
    uint32_t viewportHeight = 0;          ///< Measured image backing height in pixels.
    bool viewportVisible = false;         ///< Whether the image was measured this frame.
    bool selectionHiddenByFilter = false; ///< Selected subject remains valid but search hides it.
    const VisibilityDisplay* visibilityDisplay = nullptr; ///< Last rendered object identity map.
    std::string* sceneFilter = nullptr; ///< Borrowed Scene search text for the Clear filter action.
    const LightingDisplay* lightingDisplay =
        nullptr; ///< Coherent lighting readings and latest warnings.
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
