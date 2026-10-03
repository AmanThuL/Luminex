//----------------------------------------------------------------------------------------------------------------------
/// @file GizmoModel.cpp
/// @brief Implements viewport transform permissions, shared edits and reversible drag state.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Scene/GizmoModel.h"

#include "Core/Math/Aabb.h"
#include "Engine/Asset/Document/Orientation.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace lmx::app {

namespace {

//======================================================================================================================
rojoRHI::Result<void> refuse(std::string message) {
    return std::unexpected(rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc, std::move(message)});
}

} // namespace

//======================================================================================================================
std::optional<GizmoSubject> gizmoSubject(const SceneSession& session,
                                         const EditorSelection& selection, bool stopped) {
    if (!session.activeScene())
        return std::nullopt;
    GizmoSubject result;
    PoseLock lock;
    bool enabled;
    if (selection.subject == EditorSubject::Object) {
        if (selection.index >= session.scene().objects.size())
            return std::nullopt;
        const auto& object = session.scene().objects[selection.index];
        result.world = composeTransform({object.position, object.eulerDegrees, object.scale});
        result.move = result.rotate = result.scale = true;
        enabled = object.enabled;
        lock = session.objectPoseLock(selection.index);
    } else if (selection.subject == EditorSubject::LocalLight) {
        const auto* light = session.scene().light(selection.lightId);
        if (!light)
            return std::nullopt;
        result.world = glm::translate(glm::mat4(1), light->position);
        result.move = true;
        result.rotate = light->type == engine::LocalLightType::Spot;
        if (result.rotate)
            result.world *=
                glm::mat4_cast(asset::rotationForDirection(glm::normalize(light->direction)));
        enabled = light->enabled;
        lock = session.lightPoseLock(selection.subject, selection.index, selection.lightId);
    } else {
        return std::nullopt;
    }
    if (lock != PoseLock::None)
        result.reason = poseLockReason(lock);
    else if (!stopped)
        result.reason = "Stop playback to move this";
    else if (!enabled)
        result.reason = "Disabled";
    else
        result.live = true;
    return result;
}

//======================================================================================================================
glm::mat4 gizmoProjection(const engine::Camera& camera, float aspect) {
    return glm::perspectiveRH_NO(camera.fovY, aspect, camera.nearZ, 1000.0f);
}

//======================================================================================================================
rojoRHI::Result<void> applyGizmo(SceneSession& session, const EditorSelection& selection,
                                 const glm::mat4& world) {
    const auto subject = gizmoSubject(session, selection, true);
    if (!subject)
        return refuse("No gizmo subject");
    if (!subject->live)
        return refuse(subject->reason);
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(world[column][row]))
                return refuse("Gizmo pose must be finite");
    auto transform = decomposeTransform(world);
    if (!transform || !isFinite(transform->position) || !isFinite(transform->eulerDegrees) ||
        !isFinite(transform->scale))
        return refuse("Gizmo pose must be a finite translate-rotate-scale transform");
    if (selection.subject == EditorSubject::Object) {
        transform->scale = glm::clamp(transform->scale, glm::vec3(0.01f), glm::vec3(100.0f));
        return session.editObject(selection.index, *transform);
    }
    auto light = *session.scene().light(selection.lightId);
    light.position = transform->position;
    if (light.type == engine::LocalLightType::Spot) {
        const auto direction = -glm::vec3(world[2]);
        const float length = glm::length(direction);
        if (!std::isfinite(length) || length <= 0.0f)
            return refuse("Spot direction must be finite and nonzero");
        light.direction = direction / length;
    }
    light.enabled = session.localLightEnabled(selection.lightId);
    return session.editLocalLight(selection.lightId, light);
}

//======================================================================================================================
void GizmoDrag::begin(const SceneSession& session, const EditorSelection& selection) {
    end();
    const auto subject = gizmoSubject(session, selection, true);
    if (!subject || !subject->live)
        return;
    m_session = &session;
    m_activation = session.activationGeneration();
    m_selection = selection;
    if (selection.subject == EditorSubject::Object) {
        const auto& object = session.scene().objects[selection.index];
        m_object = {object.position, object.eulerDegrees, object.scale};
    } else {
        const auto& light = *session.scene().light(selection.lightId);
        m_position = light.position;
        m_direction = light.direction;
    }
}

//======================================================================================================================
bool GizmoDrag::active() const {
    return m_session != nullptr;
}

//======================================================================================================================
bool GizmoDrag::matches(const EditorSelection& selection) const {
    return active() && m_activation == m_session->activationGeneration() &&
           m_selection.sceneId == selection.sceneId && m_selection.subject == selection.subject &&
           m_selection.index == selection.index && m_selection.lightId == selection.lightId &&
           m_selection.node == selection.node &&
           m_selection.importedNode == selection.importedNode &&
           gizmoSubject(*m_session, selection, true).has_value();
}

//======================================================================================================================
rojoRHI::Result<void> GizmoDrag::cancel(SceneSession& session) {
    if (!active())
        return {};
    const bool current = m_session == &session && matches(m_selection);
    end();
    if (!current)
        return refuse("Gizmo drag no longer belongs to this scene activation");
    const auto subject = gizmoSubject(session, m_selection, true);
    if (!subject->live)
        return refuse(subject->reason);
    if (m_selection.subject == EditorSubject::Object)
        return session.editObject(m_selection.index, m_object);
    auto light = *session.scene().light(m_selection.lightId);
    light.position = m_position;
    light.direction = m_direction;
    light.enabled = session.localLightEnabled(m_selection.lightId);
    return session.editLocalLight(m_selection.lightId, light);
}

//======================================================================================================================
void GizmoDrag::end() {
    m_session = nullptr;
}

} // namespace lmx::app
