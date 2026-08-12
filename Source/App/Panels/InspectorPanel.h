//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorPanel.h
/// @brief Declares the Inspector panel's drawing entry point and the state it edits.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/EditorRenderSettings.h"
#include "App/ExposureReset.h"
#include "Engine/Scene.h"
#include "Render/Camera.h"
#include "Render/Renderer.h"

namespace lmx::app {

/// The Dear ImGui window name this panel submits. The shell's dock builder places the window under
/// exactly this name, so both sides read it from here.
inline constexpr const char* kInspectorPanelWindowName = "Inspector";

/// The editor state the Inspector draws and edits, borrowed for the duration of one draw call.
///
/// Every member is a reference to storage the shell owns, so the panel edits the shell's values in
/// place and holds nothing past the call that resolved them.
struct InspectorPanelContext {
    render::Camera& camera;     ///< The fly camera; angles are presented in degrees.
    render::Renderer& renderer; ///< Borrowed for the scene target's clear color only.
    engine::Scene& scene;       ///< The active scene, whose lights and objects are edited in place.
    EditorRenderSettings& settings;        ///< Editor-owned render knobs.
    ExposureResetContext& exposureContext; ///< What `shouldResetExposure` last compared against.
    /// Raised when an edit here is one of the exposure reset triggers, and consumed by the frame
    /// loop rather than by this panel.
    bool& exposureResetPending;
};

/// Draws the Inspector panel: camera, lights, render settings, and object transforms. `open`
/// follows the window's close button, exactly as `ImGui::Begin` writes it.
///
/// Interactive ranges clamp to values the renderer accepts -- a light direction is never stored as
/// a zero vector, the metering percentile window never empties, and pitch stays off the poles.
void drawInspectorPanel(bool& open, const InspectorPanelContext& context);

} // namespace lmx::app
