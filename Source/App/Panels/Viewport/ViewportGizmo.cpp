//----------------------------------------------------------------------------------------------------------------------
/// @file ViewportGizmo.cpp
/// @brief Draws editor-only transform handles through the shared scene model.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Viewport/ViewportGizmo.h"

#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include <imgui_internal.h>

#include <cmath>

namespace lmx::app {
namespace {

//======================================================================================================================
ImGuizmo::OPERATION operationFor(GizmoState state, const GizmoSubject& subject) {
    int operation = 0;
    if (subject.move && (state.tool == GizmoTool::Move || state.tool == GizmoTool::Combined))
        operation |= ImGuizmo::TRANSLATE;
    if (subject.rotate && (state.tool == GizmoTool::Rotate || state.tool == GizmoTool::Combined))
        operation |= ImGuizmo::ROTATE;
    // Ordinary SCALE forces the whole operation into Local in this pin. Universal scale keeps
    // Combined translation/rotation in the requested space; scale itself follows the local TRS.
    if (subject.scale && (state.tool == GizmoTool::Scale || state.tool == GizmoTool::Combined))
        operation |= state.tool == GizmoTool::Combined ? ImGuizmo::SCALEU : ImGuizmo::SCALE;
    return static_cast<ImGuizmo::OPERATION>(operation);
}

//======================================================================================================================
bool finiteMatrix(const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row]))
                return false;
    return true;
}

//======================================================================================================================
bool usablePose(const glm::mat4& world) {
    if (!finiteMatrix(world) || !finiteMatrix(glm::inverse(world)))
        return false;
    // The vendor normalizes each basis with a float squared length. A finite inverse alone
    // does not exclude overflow (huge authored scale) or underflow (a collapsed tiny axis).
    for (int axis = 0; axis < 3; ++axis) {
        const auto basis = glm::vec3(world[axis]);
        const float squaredLength = glm::dot(basis, basis);
        if (!std::isfinite(squaredLength) || squaredLength <= 0)
            return false;
    }
    return true;
}

//======================================================================================================================
void endInteraction(ViewportGizmoContext& context) {
    ImGuizmo::Enable(false);
    context.drag.end();
    if (context.lifecycle)
        context.lifecycle->suppressUntilRelease |= ImGui::IsMouseDown(ImGuiMouseButton_Left);
}

} // namespace

//======================================================================================================================
void setViewportGizmoColors(const ViewportGizmoColors& colors) {
    auto& target = ImGuizmo::GetStyle().Colors;
    for (int i = 0; i < 3; ++i) {
        target[ImGuizmo::DIRECTION_X + i] = colors.axis[i];
        target[ImGuizmo::PLANE_X + i] = colors.plane[i];
    }
    target[ImGuizmo::SELECTION] = colors.selection;
    target[ImGuizmo::INACTIVE] = colors.inactive;
    target[ImGuizmo::TRANSLATION_LINE] = colors.translation;
    target[ImGuizmo::SCALE_LINE] = colors.scale;
    target[ImGuizmo::ROTATION_USING_BORDER] = colors.rotationBorder;
    target[ImGuizmo::ROTATION_USING_FILL] = colors.rotationFill;
    target[ImGuizmo::HATCHED_AXIS_LINES] = colors.hatch;
    target[ImGuizmo::TEXT] = colors.text;
    target[ImGuizmo::TEXT_SHADOW] = colors.textShadow;
}

//======================================================================================================================
ViewportGizmoResult drawViewportGizmo(ViewportGizmoContext& context, ImVec2 origin, ImVec2 size) {
    ViewportGizmoResult result;
    // The pinned library accumulates handle hits across submissions until BeginFrame resets it.
    // Its empty helper window has no inputs; SetDrawlist below confines handles to Viewport.
    ImGuizmo::BeginFrame();
    if (!context.lifecycle) {
        endInteraction(context);
        result.error = "Viewport gizmo requires lifecycle state";
        return result;
    }
    auto& lifecycle = *context.lifecycle;
    const bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (!mouseDown)
        lifecycle.suppressUntilRelease = false;
    const bool toolChanged =
        lifecycle.initialized && (lifecycle.previous.tool != context.state.tool ||
                                  lifecycle.previous.space != context.state.space);
    lifecycle.previous = context.state;
    lifecycle.initialized = true;
    const auto subject = gizmoSubject(context.session, context.selection, context.stopped);
    const auto operation =
        subject ? operationFor(context.state, *subject) : static_cast<ImGuizmo::OPERATION>(0);
    const bool validRect = std::isfinite(origin.x) && std::isfinite(origin.y) &&
                           std::isfinite(size.x) && std::isfinite(size.y) && size.x > 0 &&
                           size.y > 0;
    const auto& camera = context.camera;
    const bool validLens = std::isfinite(camera.fovY) && camera.fovY > 0 &&
                           camera.fovY < glm::pi<float>() && std::isfinite(camera.nearZ) &&
                           camera.nearZ > 0 && camera.nearZ < 1000;
    if (!subject || operation == 0 || !validRect || !validLens) {
        endInteraction(context);
        return result;
    }
    const auto view = camera.viewMatrix();
    const auto projection = gizmoProjection(camera, size.x / size.y);
    const auto clip = projection * view * subject->world[3];
    // This pin pushes its clip rectangle before its behind-camera early return and never pops
    // that path. Reject the same clip-Z boundary before calling into the library.
    if (!finiteMatrix(view) || !finiteMatrix(projection) || !usablePose(subject->world) ||
        !std::isfinite(clip.z) || !std::isfinite(clip.w) || clip.z < 0.001f || clip.w <= 0) {
        endInteraction(context);
        return result;
    }

    const bool blocked =
        ImGui::GetIO().AppFocusLost || ImGui::GetIO().WantTextInput ||
        ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
        (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (toolChanged || !subject->live || blocked ||
        (context.drag.active() && !context.drag.matches(context.selection)))
        endInteraction(context);
    if (context.drag.active() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        const auto restored = context.drag.cancel(context.session);
        if (restored)
            result.edited = true;
        else
            result.error = restored.error().message;
        endInteraction(context);
    }

    auto* draw = ImGui::GetWindowDrawList();
    ImGuizmo::SetDrawlist(draw);
    ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::Enable(subject->live && !blocked && !lifecycle.suppressUntilRelease);
    // Always reread the scene, including an Escape restoration made above.
    auto world = gizmoSubject(context.session, context.selection, context.stopped)->world;
    const auto inputWorld = world;
    ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operation,
                         context.state.space == GizmoSpace::World ? ImGuizmo::WORLD
                                                                  : ImGuizmo::LOCAL,
                         glm::value_ptr(world));
    // This pin retains the last translation delta between drags. Its bool can be false for a
    // repeated delta even though it wrote a new matrix, so compare the returned pose itself.
    const bool changed = world != inputWorld;
    const bool usingNow = ImGuizmo::IsUsing();
    if (usingNow && !context.drag.active())
        context.drag.begin(context.session, context.selection);
    if (changed && context.drag.active() && context.drag.matches(context.selection)) {
        const auto applied = applyGizmo(context.session, context.selection, world);
        if (applied)
            result.edited = true;
        else {
            result.error = applied.error().message;
            endInteraction(context);
        }
    }
    if (!usingNow)
        context.drag.end();
    result.using_ = usingNow && context.drag.active();
    const bool inImage =
        ImGui::IsMouseHoveringRect(origin, ImVec2(origin.x + size.x, origin.y + size.y));
    result.hovered =
        result.using_ || (inImage && ImGui::IsWindowHovered() && ImGuizmo::IsOver(operation));
    if (result.hovered && !subject->live && !subject->reason.empty())
        ImGui::SetTooltip("%s", subject->reason.c_str());
    return result;
}

} // namespace lmx::app
