//----------------------------------------------------------------------------------------------------------------------
/// @file ViewportPanel.cpp
/// @brief Implements the Viewport panel's toolbar, image display, and size measurement.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/ViewportPanel.h"

#include "App/EditorShell.h"
#include "RHI/Metal4/Metal4ImGui.h"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <iterator>

namespace lmx::app {

namespace {

//======================================================================================================================
// ImGui reports panel sizes in points; the scene target is sized in pixels.
uint32_t toPixels(float points, float scale) {
    return static_cast<uint32_t>(std::max(points * scale, 0.0f) + 0.5f);
}

// Kept compact so the shadow-filter combo does not dominate a one-line toolbar.
constexpr float kShadowFilterComboWidth = 80.0f;

//======================================================================================================================
// One compact row above the image (spec section 8): active scene, Reset Camera, and quick toggles
// for wireframe, exposure mode, bloom, and shadow filter. Edits the exact same
// EditorRenderSettings/ExposureResetContext storage the Inspector's Rendering section does, so a
// toggle here and the matching Inspector row can never disagree.
void drawToolbar(const ViewportPanelContext& context) {
    ImGui::TextUnformatted(context.activeSceneName.data(),
                           context.activeSceneName.data() + context.activeSceneName.size());

    ImGui::SameLine();
    if (ImGui::Button("Reset Camera")) {
        context.camera = cameraFromScene(context.scene.initialCamera);
    }

    ImGui::SameLine();
    ImGui::Checkbox("Wireframe", &context.settings.wireframe);

    ImGui::SameLine();
    bool autoExposureEnabled = context.settings.autoExposureEnabled;
    if (ImGui::Checkbox("Auto Exposure", &autoExposureEnabled)) {
        setAutoExposureEnabled(context.settings, context.exposureContext,
                               context.exposureResetPending, autoExposureEnabled);
    }

    ImGui::SameLine();
    ImGui::Checkbox("Bloom", &context.settings.bloomEnabled);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(kShadowFilterComboWidth);
    int filterIndex = static_cast<int>(context.settings.shadowFilter);
    constexpr const char* kFilterNames[] = {"PCF", "PCSS"};
    if (ImGui::Combo("##shadowFilter", &filterIndex, kFilterNames,
                     static_cast<int>(std::size(kFilterNames)))) {
        context.settings.shadowFilter = static_cast<render::ShadowFilter>(filterIndex);
    }
}

} // namespace

//======================================================================================================================
ViewportPanelResult drawViewportPanel(bool& open, const ViewportPanelContext& context) {
    ViewportPanelResult result;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin(kViewportPanelWindowName, &open);
    ImGui::PopStyleVar();

    if (visible) {
        drawToolbar(context);
        ImGui::Separator();

        result.measured = true;
        result.hovered = ImGui::IsWindowHovered();
        result.focused = ImGui::IsWindowFocused();

        const ImGuiIO& io = ImGui::GetIO();
        // The toolbar and separator already advanced the cursor, so this is the region below them.
        const ImVec2 available = ImGui::GetContentRegionAvail();
        result.width = toPixels(available.x, io.DisplayFramebufferScale.x);
        result.height = toPixels(available.y, io.DisplayFramebufferScale.y);

        if (available.x > 0.0f && available.y > 0.0f) {
            // Stretch the last good target while a resize is pending.
            ImGui::Image(rhi::metal4::imguiTextureID(context.renderer.colorTarget()), available);
        }
    }
    ImGui::End();
    return result;
}

} // namespace lmx::app
