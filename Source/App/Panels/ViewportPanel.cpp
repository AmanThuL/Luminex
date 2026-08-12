//----------------------------------------------------------------------------------------------------------------------
/// @file ViewportPanel.cpp
/// @brief Implements the Viewport panel's image display and size measurement.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/ViewportPanel.h"

#include "RHI/Metal4/Metal4ImGui.h"

#include <imgui.h>

#include <algorithm>

namespace lmx::app {

namespace {

//======================================================================================================================
// ImGui reports panel sizes in points; the scene target is sized in pixels.
uint32_t toPixels(float points, float scale) {
    return static_cast<uint32_t>(std::max(points * scale, 0.0f) + 0.5f);
}

} // namespace

//======================================================================================================================
ViewportPanelResult drawViewportPanel(bool& open, render::Renderer& renderer) {
    ViewportPanelResult result;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin(kViewportPanelWindowName, &open);
    ImGui::PopStyleVar();

    if (visible) {
        result.measured = true;
        result.hovered = ImGui::IsWindowHovered();
        result.focused = ImGui::IsWindowFocused();

        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        result.width = toPixels(available.x, io.DisplayFramebufferScale.x);
        result.height = toPixels(available.y, io.DisplayFramebufferScale.y);

        if (available.x > 0.0f && available.y > 0.0f) {
            // Stretch the last good target while a resize is pending.
            ImGui::Image(rhi::metal4::imguiTextureID(renderer.colorTarget()), available);
        }
    }
    ImGui::End();
    return result;
}

} // namespace lmx::app
