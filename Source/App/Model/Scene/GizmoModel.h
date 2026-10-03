//----------------------------------------------------------------------------------------------------------------------
/// @file GizmoModel.h
/// @brief Declares viewport transform permissions, shared edits and reversible drag state.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Scene/EditorSelection.h"
#include "App/Model/Scene/SceneSession.h"

namespace lmx::app {

/// The session's viewport transform operation.
enum class GizmoTool {
    View,     ///< Hide the transform handles.
    Move,     ///< Translate along an axis or plane.
    Rotate,   ///< Rotate about an axis.
    Scale,    ///< Scale an object.
    Combined, ///< Offer every operation supported by the selected subject.
};

/// The axes used by transform handles.
enum class GizmoSpace {
    World, ///< Use the scene's world axes.
    Local, ///< Use the subject's rotated axes.
};

/// Transient viewport choices, independent of saved workspace or scene state.
struct GizmoState {
    GizmoTool tool = GizmoTool::Move;     ///< Current operation.
    GizmoSpace space = GizmoSpace::World; ///< Current axis frame.
};

/// The live scene pose and shared edit permission for one selected subject.
struct GizmoSubject {
    glm::mat4 world{1.0f}; ///< Object TRS or light pose with local -Z along its ray direction.
    bool move = false;     ///< Translation is supported.
    bool rotate = false;   ///< Rotation is supported.
    bool scale = false;    ///< Scale is supported.
    bool live = false;     ///< Supported operations may be applied now.
    std::string reason;    ///< Why handles are inert; empty when live.
};

/// Reads the current pose; missing/unsupported subjects return nullopt. Shared pose locks,
/// effective enabled state and stopped playback decide whether supported operations are live.
/// The caller supplies a selection already resolved against the active scene ID.
std::optional<GizmoSubject> gizmoSubject(const SceneSession& session,
                                         const EditorSelection& selection, bool stopped);
/// Builds a finite right-handed [-1,1] depth projection from the camera lens, with far = 1000 m.
/// XY matches the unjittered reversed-depth camera projection; aspect and lens must be valid.
glm::mat4 gizmoProjection(const engine::Camera& camera, float aspect);
/// Applies a finite decomposable world pose through the Inspector's SceneSession edit routes.
/// Object scales clamp to [0.01,100]; spot -Z becomes normalized ray direction. Shared pose locks
/// and effective disabled state refuse edits. The caller requires stopped playback.
/// The selection must already be resolved against the active scene ID.
rojoRHI::Result<void> applyGizmo(SceneSession& session, const EditorSelection& selection,
                                 const glm::mat4& world);

/// A captured pose for Escape cancellation; the borrowed session must outlive this drag.
/// Activation changes invalidate the capture even when scene IDs or storage are reused.
class GizmoDrag {
public:
    /// Captures an editable subject before its first applyGizmo call; unsupported/locked subjects
    /// leave no active drag. The caller requires stopped playback.
    void begin(const SceneSession& session, const EditorSelection& selection);
    /// Whether a capture is held, until end or cancel releases it.
    bool active() const;
    /// Whether selection and the original scene activation still match the capture.
    bool matches(const EditorSelection& selection) const;
    /// Restores captured pose fields through shared edit permission and always releases capture.
    /// A changed activation, missing subject or newly locked subject fails without mutation.
    /// Non-pose light edits survive. With no capture this succeeds without doing anything.
    rojoRHI::Result<void> cancel(SceneSession& session);
    /// Releases the capture without restoring; use after completion or playback starts.
    void end();

private:
    const SceneSession* m_session = nullptr;
    uint64_t m_activation = 0;
    EditorSelection m_selection;
    DecomposedTransform m_object;
    glm::vec3 m_position{0};
    glm::vec3 m_direction{0};
};

} // namespace lmx::app
