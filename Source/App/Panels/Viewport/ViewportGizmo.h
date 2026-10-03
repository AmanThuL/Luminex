//----------------------------------------------------------------------------------------------------------------------
/// @file ViewportGizmo.h
/// @brief Declares editor-only transform handles and their interaction lifetime.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Scene/GizmoModel.h"
#include "App/Model/Workspace/EditorThemeTokens.h"

#include <imgui.h>

namespace lmx::app {

/// Shell-owned interaction state; cancellation remains suppressed until the left button rises.
struct ViewportGizmoLifecycle {
    GizmoState previous;               ///< Tool and space submitted on the preceding frame.
    bool initialized = false;          ///< Whether previous has been observed.
    bool suppressUntilRelease = false; ///< Prevents a held press from changing subjects or tools.
};

/// Borrowed scene data and shell-owned drag state for one viewport submission.
struct ViewportGizmoContext {
    SceneSession& session;            ///< Shared Inspector and gizmo edit route.
    const EditorSelection& selection; ///< Selection resolved against the active scene ID.
    const engine::Camera& camera;     ///< Unjittered editor view and lens.
    GizmoState state;                 ///< Current tool and axis frame.
    GizmoDrag& drag;                  ///< Original pose captured before the first edit.
    bool stopped;                     ///< Whether playback is fully stopped.
    ViewportGizmoLifecycle* lifecycle = nullptr; ///< Required owner; missing state fails closed.
};

/// Interaction and model changes produced during one UI frame.
struct ViewportGizmoResult {
    bool hovered = false; ///< A handle is hovered or owns the drag; excludes camera look.
    bool using_ = false;  ///< The library and model both own an active drag.
    bool edited = false;  ///< A pose changed, including Escape restoration; requests a camera cut.
    std::string error;    ///< Named edit or context error; empty on success.
};

/// Theme adapter independent of the vendored library's color enum.
struct ViewportGizmoColors {
    ImVec4 axis[3];        ///< X, Y and Z directions.
    ImVec4 plane[3];       ///< Translucent X, Y and Z planes.
    ImVec4 selection;      ///< Hovered or active handle.
    ImVec4 inactive;       ///< Locked subject handles.
    ImVec4 translation;    ///< Translation guide.
    ImVec4 scale;          ///< Scale guide.
    ImVec4 rotationBorder; ///< Active rotation outline.
    ImVec4 rotationFill;   ///< Active rotation wedge.
    ImVec4 hatch;          ///< Occluded axis marks.
    ImVec4 text;           ///< Numeric drag feedback.
    ImVec4 textShadow;     ///< Feedback contrast against the scene.
};

/// Applies colors supplied by the shell's ThemeRole mapping to the one vendored gizmo style.
void setViewportGizmoColors(const ViewportGizmoColors& colors);

/// Draws into the current viewport window only, clipped to the supplied image rectangle.
/// A nonpositive/invalid size ends the interaction without drawing, even outside Begin/End.
/// The caller invokes this every frame, passing an empty size for a closed/collapsed viewport.
ViewportGizmoResult drawViewportGizmo(ViewportGizmoContext& context, ImVec2 origin, ImVec2 size);

} // namespace lmx::app
