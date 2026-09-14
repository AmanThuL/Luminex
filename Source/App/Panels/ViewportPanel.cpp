//----------------------------------------------------------------------------------------------------------------------
/// @file ViewportPanel.cpp
/// @brief Draws the scene image, editor selection, diagnostic legend and playback transport.
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
void drawTransport(const ViewportPanelContext& context) {
    const bool animated = !context.scene.animation.tracks.empty() ||
                          !context.scene.animation.emissiveTracks.empty() ||
                          !context.scene.animation.cameraTrack.empty();
    ImGui::BeginDisabled(!animated);
    if (ImGui::Button(context.settings.animationPlaying ? "Pause scene" : "Play scene")) {
        context.settings.animationPlaying = !context.settings.animationPlaying;
    }
    editorTooltip("Pause or resume scene animation. Rendering and diagnostic sampling continue.");
    nextToolbarItem(ImGui::CalcTextSize("Step 1/60 s").x + ImGui::GetStyle().FramePadding.x * 2);
    if (ImGui::Button("Step 1/60 s")) {
        context.settings.animationPlaying = false;
        context.session.stepAnimation();
    }
    editorTooltip("Advance animation exactly 1/60 second and leave playback paused.");
    nextToolbarItem(ImGui::CalcTextSize("Reset time").x + ImGui::GetStyle().FramePadding.x * 2);
    if (ImGui::Button("Reset time")) {
        context.session.rewindAnimation();
        requestCameraCut(context.temporalState);
    }
    editorTooltip("Return animation to time zero and reset temporal history; keep playback mode.");
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s | Time %.3f s | Rendering continues",
                       !animated                           ? "No scene animation"
                       : context.settings.animationPlaying ? "Playing"
                                                           : "Paused",
                       context.scene.animationTime);
    if (!context.scene.animation.cameraTrack.empty()) {
        ImGui::Checkbox("Follow camera rail", &context.settings.followCameraTrack);
        editorTooltip("Use the scene's authored camera path at the animation time; manual RMB look "
                      "temporarily takes control.");
    }
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
        float transportRows = 1.0f;
        float usedWidth = 0.0f;
        for (const char* label : {"Pause scene", "Step 1/60 s", "Reset time"}) {
            const float width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
            if (usedWidth > 0 && usedWidth + width > available.x) {
                transportRows += 1.0f;
                usedWidth = 0.0f;
            }
            usedWidth += width + ImGui::GetStyle().ItemSpacing.x;
        }
        if (!context.scene.animation.cameraTrack.empty()) {
            transportRows += 1.0f;
        }
        const float timeRows = available.x < editor_style::scaled(400.0f) ? 2.0f : 1.0f;
        const float transportHeight = ImGui::GetFrameHeightWithSpacing() * transportRows +
                                      ImGui::GetTextLineHeightWithSpacing() * timeRows +
                                      editor_style::scaled(8.0f);
        const ImVec2 imageSize(available.x, std::max(available.y - transportHeight, 1.0f));
        result.measured = imageSize.x > 0.0f && available.y > transportHeight;
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
            const auto scale = ImGui::GetWindowViewport()->FramebufferScale;
            result.backingScale = scale.x;
            result.width = toPixels(imageSize.x, scale.x);
            result.height = toPixels(imageSize.y, scale.y);
        }
        ImGui::Separator();
        drawTransport(context);
    }
    ImGui::End();
    return result;
}

} // namespace lmx::app
