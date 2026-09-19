//----------------------------------------------------------------------------------------------------------------------
/// @file ViewportPanel.cpp
/// @brief Draws the scene image, editor selection, camera tools and diagnostic legend.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/ViewportPanel.h"

#include "App/Model/DiagnosticLegend.h"
#include "App/Model/SelectionBounds.h"
#include "App/Panels/ActionFeedback.h"
#include "App/Panels/EditorStyle.h"
#include "RHI/Metal4/Metal4ImGui.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace lmx::app {
namespace {

//======================================================================================================================
uint32_t toPixels(float points, float scale) {
    return static_cast<uint32_t>(std::max(points * scale, 0.0f) + 0.5f);
}

//======================================================================================================================
void nextToolbarItem(float width) {
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (right - ImGui::GetItemRectMax().x >= width + ImGui::GetStyle().ItemSpacing.x) {
        ImGui::SameLine();
    }
}

//======================================================================================================================
void drawToolbar(const ViewportPanelContext& context) {
    ImGui::TextUnformatted(context.activeSceneName.data(),
                           context.activeSceneName.data() + context.activeSceneName.size());
    nextToolbarItem(ImGui::CalcTextSize("Reset camera").x + ImGui::GetStyle().FramePadding.x * 2);
    if (ImGui::Button("Reset camera")) {
        context.camera = scene::cameraFromScene(context.scene.initialCamera);
        context.settings.followCameraTrack = false;
        requestCameraCut(context.temporalState);
    }
    editorTooltip(
        "Restore the scene's initial camera, stop rail following and reset temporal history.");
    nextToolbarItem(ImGui::CalcTextSize("Camera help").x + ImGui::GetStyle().FramePadding.x * 2);
    if (ImGui::Button("Camera help")) {
        ImGui::OpenPopup("camera-help");
    }
    editorTooltip("Hold the right mouse button over the image to look; WASD moves, Q/E "
                  "lowers/raises the camera.");
    if (ImGui::BeginPopup("camera-help")) {
        ImGui::TextUnformatted("Hold RMB over the image to look.");
        ImGui::TextUnformatted("While held: WASD move, Q down, E up.");
        ImGui::TextUnformatted("Release RMB to return to editing.");
        ImGui::TextUnformatted("Text entry keeps camera and capture keys inactive.");
        ImGui::EndPopup();
    }
    nextToolbarItem(ImGui::CalcTextSize("GPU capture").x + ImGui::GetStyle().FramePadding.x * 2);
    if (ImGui::Button("GPU capture")) {
        ImGui::OpenPopup("capture-result");
    }
    editorTooltip(
        "Capture the next acquired GPU frame for Xcode inspection. C uses the same action.");
    if (ImGui::BeginPopup("capture-result")) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + editor_style::scaled(400.0f));
        ImGui::BeginDisabled(!context.actions.captureAvailable() ||
                             context.actions.captureResult().status == ActionStatus::Pending);
        if (ImGui::Button("Capture next frame (C)")) {
            context.actions.requestCapture();
        }
        ImGui::EndDisabled();
        drawActionFeedback("viewport-capture", context.actions.captureResult());
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
    const auto captureStatus = context.actions.captureResult().status;
    if (context.actions.captureFeedbackVisible()) {
        ImGui::TextWrapped("Capture %s: %s", actionStatusName(captureStatus),
                           context.actions.captureResult().message.c_str());
        if (ImGui::SmallButton("Dismiss capture status")) {
            context.actions.dismissCaptureFeedback();
        }
    }
    const bool objectSelected = context.selection.subject == EditorSubject::Object &&
                                context.selection.index < context.scene.objects.size();
    const auto bounds = selectedObjectBounds(context.scene, context.selection);
    ImGui::BeginDisabled(!bounds);
    if (ImGui::Button("Frame selected")) {
        const float aspect = static_cast<float>(context.renderer.width()) /
                             static_cast<float>(context.renderer.height());
        if (frameSelection(context.camera, *bounds, aspect)) {
            context.settings.followCameraTrack = false;
            requestCameraCut(context.temporalState);
        }
    }
    ImGui::EndDisabled();
    if (!objectSelected) {
        editorTooltip("Select an object in Hierarchy to frame its geometry.");
    } else if (!bounds) {
        editorTooltip("Framing unavailable: this object has no reliable geometry bounds.");
    } else {
        editorTooltip(
            "Fit this object's bounds while preserving view direction and field of view. "
            "Stops camera-rail following and resets temporal history; does not remove occluders.");
    }
    nextToolbarItem(ImGui::CalcTextSize("Selection outline").x + ImGui::GetFrameHeight() +
                    ImGui::GetStyle().ItemInnerSpacing.x);
    const bool outlineReady = context.outlineTarget.width() == context.renderer.width() &&
                              context.outlineTarget.height() == context.renderer.height();
    ImGui::BeginDisabled(!objectSelected || !outlineReady);
    ImGui::Checkbox("Selection outline", &context.showOutline);
    ImGui::EndDisabled();
    editorTooltip(objectSelected
                      ? "Show a soft outline along the selected object's visible geometry. "
                        "Hidden surfaces stay hidden. Editor-only; scene captures and history "
                        "are unchanged."
                      : "Select an object in Hierarchy to show its visible-geometry outline.");
    if (!outlineReady) {
        ImGui::TextWrapped(
            "Outline unavailable after allocation failure. Resize the viewport to retry.");
    }
    const auto lab = labDescription(context.sceneId);
    if (!lab.empty()) {
        ImGui::TextWrapped("%.*s", static_cast<int>(lab.size()), lab.data());
    }
}

//======================================================================================================================
void drawLegend(const ViewportPanelContext& context) {
    if (context.settings.lightDebugView != render::LightDebugView::Off) {
        const auto legend = diagnosticLegend(context.settings.lightDebugView);
        ImGui::Text("View: %.*s", static_cast<int>(legend.name.size()), legend.name.data());
        nextToolbarItem(ImGui::CalcTextSize("Return to Final").x +
                        ImGui::GetStyle().FramePadding.x * 2);
        if (ImGui::Button("Return to Final"))
            context.settings.lightDebugView = render::LightDebugView::Off;
        if (context.scene.enabledLightCount() == 0)
            ImGui::TextWrapped(
                "Requested view unavailable: no enabled local lights. Showing Final.");
        else
            ImGui::TextWrapped("%.*s", static_cast<int>(legend.description.size()),
                               legend.description.data());
        return;
    }
    if (context.settings.hzbDebugLevel >= 0) {
        uint32_t width = (context.renderer.width() + 1) / 2;
        uint32_t height = (context.renderer.height() + 1) / 2;
        int32_t top = 0;
        while (width > 16 || height > 16) {
            width = (width + 1) / 2;
            height = (height + 1) / 2;
            ++top;
        }
        const auto effectiveLevel = std::min(context.settings.hzbDebugLevel, top);
        ImGui::Text("HZB level %d: farthest reversed depth (black is uncovered)", effectiveLevel);
        if (effectiveLevel != context.settings.hzbDebugLevel)
            ImGui::Text("Requested level %d clamped to available level %d",
                        context.settings.hzbDebugLevel, effectiveLevel);
        nextToolbarItem(ImGui::CalcTextSize("Return to Final").x +
                        ImGui::GetStyle().FramePadding.x * 2);
        if (ImGui::Button("Return to Final"))
            context.settings.hzbDebugLevel = -1;
        return;
    }
    if (!context.settings.temporalEnabled ||
        context.settings.temporalDebugView == render::TemporalDebugView::Off) {
        return;
    }
    const auto legend = diagnosticLegend(context.settings.temporalDebugView);
    ImGui::Text("View: %.*s", static_cast<int>(legend.name.size()), legend.name.data());
    nextToolbarItem(ImGui::CalcTextSize("Return to Final").x +
                    ImGui::GetStyle().FramePadding.x * 2);
    if (ImGui::Button("Return to Final")) {
        context.settings.temporalDebugView = render::TemporalDebugView::Off;
    }
    ImGui::TextWrapped("%.*s", static_cast<int>(legend.description.size()),
                       legend.description.data());
    const auto note = diagnosticModeNote(context.settings.temporalDebugView,
                                         context.renderer.temporalStatus().reconstruction);
    if (!note.empty()) {
        ImGui::TextWrapped("%.*s", static_cast<int>(note.size()), note.data());
    }
}

//======================================================================================================================
void drawOcclusionOverlay(const ViewportPanelContext& context, ImVec2 origin, ImVec2 size) {
    if (!context.settings.occlusionEnabled || !context.visibilityDisplay)
        return;
    const auto& display = *context.visibilityDisplay;
    const auto& status = display.status();
    const auto& params = status.occlusionParams;
    if (!status.isRetired || status.sceneGeneration != context.temporalState.sceneGeneration ||
        !params.sourceWidth || !params.sourceHeight)
        return;
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    const auto pixel = [&](float x, float y) {
        return ImVec2(origin.x + x / params.sourceWidth * size.x,
                      origin.y + y / params.sourceHeight * size.y);
    };
    const auto bounds = [&](const render::InstanceVisibility& candidate, ImU32 color) {
        std::array<ImVec2, 8> points{};
        for (uint32_t corner = 0; corner < 8; ++corner) {
            const auto& box = candidate.worldBounds;
            const glm::vec4 p{corner & 1 ? box.maximum.x : box.minimum.x,
                              corner & 2 ? box.maximum.y : box.minimum.y,
                              corner & 4 ? box.maximum.z : box.minimum.z, 1.0f};
            const float w = glm::dot(params.sourceRows[3], p);
            if (!std::isfinite(w) || w <= render::kOcclusionNearGuard)
                return;
            const float x = glm::dot(params.sourceRows[0], p) / w;
            const float y = glm::dot(params.sourceRows[1], p) / w;
            if (!std::isfinite(x) || !std::isfinite(y))
                return;
            points[corner] = pixel((x * 0.5f + 0.5f) * params.sourceWidth,
                                   (0.5f - y * 0.5f) * params.sourceHeight);
        }
        for (uint32_t corner = 0; corner < 8; ++corner)
            for (uint32_t bit : {1u, 2u, 4u})
                if ((corner & bit) == 0)
                    draw->AddLine(points[corner], points[corner | bit], color);
    };
    uint32_t rejected = 0;
    if (context.settings.showOcclusionBounds) {
        for (const auto& candidate : status.scene.candidates) {
            if (candidate.reason != render::VisibilityReason::Occluded)
                continue;
            if (rejected == 128)
                break;
            bounds(candidate, IM_COL32(255, 105, 80, 150));
            ++rejected;
        }
    }
    bool selected = false;
    if (context.selection.subject == EditorSubject::Object &&
        context.selection.index < context.scene.objects.size()) {
        const auto id = context.scene.objects[context.selection.index].id;
        if (const auto* candidate =
                display.find(id, status, context.temporalState.sceneGeneration)) {
            selected = true;
            bounds(*candidate, IM_COL32(255, 220, 80, 230));
            const auto& rectangle = candidate->occlusion.rectangle;
            if (rectangle[2] > rectangle[0] && rectangle[3] > rectangle[1])
                draw->AddRect(pixel(rectangle[0], rectangle[1]), pixel(rectangle[2], rectangle[3]),
                              IM_COL32(70, 220, 255, 240), 0, 0, 2.0f);
        }
    }
    if (selected || context.settings.showOcclusionBounds) {
        const auto label = std::format(
            "HZB source frame {}: yellow bounds / cyan test rectangle; rejected {} / 128",
            status.occlusionSourceFrame, rejected);
        draw->AddText(ImVec2(origin.x + 8, origin.y + 8), IM_COL32(255, 240, 180, 255),
                      label.c_str());
    }
    draw->PopClipRect();
}

} // namespace

//======================================================================================================================
ViewportPanelResult drawViewportPanel(bool& open, const ViewportPanelContext& context) {
    ViewportPanelResult result;
    if (ImGui::Begin(kViewportPanelWindowName, &open)) {
        drawToolbar(context);
        drawLegend(context);
        ImGui::Separator();
        const auto available = ImGui::GetContentRegionAvail();
        const ImVec2 imageSize(available.x, std::max(available.y, 1.0f));
        result.measured = available.x > 0.0f && available.y > 0.0f;
        result.focused = ImGui::IsWindowFocused();
        if (result.measured) {
            const bool outlineReady = context.outlineTarget.width() == context.renderer.width() &&
                                      context.outlineTarget.height() == context.renderer.height();
            ImGui::Image(rhi::metal4::imguiTextureID(context.showOutline && outlineReady &&
                                                             context.selection.subject ==
                                                                 EditorSubject::Object
                                                         ? context.outlineTarget
                                                         : context.renderer.colorTarget()),
                         imageSize);
            result.hovered = ImGui::IsItemHovered();
            drawOcclusionOverlay(context, ImGui::GetItemRectMin(), imageSize);
            const auto scale = ImGui::GetWindowViewport()->FramebufferScale;
            result.backingScale = scale.x;
            result.width = toPixels(imageSize.x, scale.x);
            result.height = toPixels(imageSize.y, scale.y);
        }
    }
    ImGui::End();
    return result;
}

} // namespace lmx::app
