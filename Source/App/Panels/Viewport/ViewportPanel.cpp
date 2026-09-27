//----------------------------------------------------------------------------------------------------------------------
/// @file ViewportPanel.cpp
/// @brief Draws the scene image, editor selection and diagnostic legend chip.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Viewport/ViewportPanel.h"

#include "App/Model/Rendering/Settings/DebugView.h"
#include "App/Model/Rendering/Temporal/DiagnosticLegend.h"
#include "App/Panels/Shared/EditorStyle.h"
#include "Render/Passes/Occlusion/HzbStage.h"
#include <rojoRHI/Metal4/Metal4ImGui.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>

namespace lmx::app {
namespace {

//======================================================================================================================
uint32_t toPixels(float points, float scale) {
    return static_cast<uint32_t>(std::max(points * scale, 0.0f) + 0.5f);
}

/// What the diagnostic legend chip reported after drawing.
struct LegendChipResult {
    bool hovered = false;        ///< Whether the pointer is over the chip, not the scene image.
    std::optional<float> bottom; ///< Screen-space bottom edge when the chip was drawn.
};

//======================================================================================================================
LegendChipResult drawLegendChip(const ViewportPanelContext& context, ImVec2 origin,
                                ImVec2 imageSize) {
    const auto active = activeDebugView(context.settings);
    if (!active)
        return {};
    const uint32_t hzbLevels = viewportHzbLevels(context.renderer);
    const auto entries =
        debugViewEntries(context.settings, hzbLevels, context.effectiveReconstruction);
    const auto selected = std::ranges::find_if(entries, [&](const auto& entry) {
        return entry.view.topic == active->topic && entry.view.value == active->value;
    });
    const std::string title =
        selected != entries.end() ? selected->label : hzbLevelLabel(active->value);
    const float inset = editor_style::scaled(8.0f);
    const float width = std::min(editor_style::scaled(390.0f), imageSize.x - inset * 2);
    if (width <= 0 || imageSize.y <= inset * 2)
        return {};
    ImGui::SetCursorScreenPos(ImVec2(origin.x + inset, origin.y + inset));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.09f, 0.9f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(width, imageSize.y - inset * 2));
    bool hovered = false;
    if (ImGui::BeginChild("debug-legend", ImVec2(width, 0),
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize |
                              ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoSavedSettings)) {
        hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        ImGui::SetNextItemWidth(
            std::max(1.0f, ImGui::GetContentRegionAvail().x -
                               editor_style::iconButtonWidth(EditorIcon::Close) -
                               ImGui::GetStyle().ItemSpacing.x));
        if (ImGui::BeginCombo("##debug-view", title.c_str())) {
            for (const auto& entry : entries) {
                if (entry.view.topic != active->topic)
                    continue;
                ImGui::BeginDisabled(!entry.available);
                if (ImGui::Selectable(entry.label.c_str(), entry.view.value == active->value))
                    selectDebugView(context.settings, entry.view);
                ImGui::EndDisabled();
                if (!entry.available)
                    editorTooltip(entry.reason.c_str());
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (editor_style::iconButton("close-debug", EditorIcon::Close, true, "Return to Final"))
            selectDebugView(context.settings, std::nullopt);
        if (active->topic == DebugViewTopic::Occlusion) {
            // Zero levels (no output extent yet) still leaves level 0 as the only request.
            const int top = std::max(static_cast<int>(hzbLevels), 1) - 1;
            int level = context.settings.hzbDebugLevel;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGui::InputInt("##hzb-level", &level)) {
                level = std::clamp(level, 0, top);
                selectDebugView(context.settings,
                                DebugView{DebugViewTopic::Occlusion, static_cast<uint8_t>(level)});
            }
            editorTooltip("HZB mip level. Higher levels summarize a larger source region.");
            ImGui::TextWrapped("Farthest reversed depth; black is uncovered.");
            if (active->value > top)
                ImGui::Text("Requested level %u; showing available level %d", active->value, top);
        } else {
            const auto legend =
                active->topic == DebugViewTopic::Temporal
                    ? diagnosticLegend(static_cast<render::TemporalDebugView>(active->value))
                    : diagnosticLegend(static_cast<engine::LightDebugView>(active->value));
            ImGui::TextWrapped("%.*s", static_cast<int>(legend.description.size()),
                               legend.description.data());
            if (active->topic == DebugViewTopic::Lighting && context.scene.enabledLightCount() == 0)
                ImGui::TextWrapped("No enabled local lights. Showing Final.");
            if (active->topic == DebugViewTopic::Temporal) {
                const auto note =
                    diagnosticModeNote(static_cast<render::TemporalDebugView>(active->value),
                                       context.renderer.temporalStatus().reconstruction);
                if (!note.empty())
                    ImGui::TextWrapped("%.*s", static_cast<int>(note.size()), note.data());
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    return {hovered, ImGui::GetItemRectMax().y};
}

//======================================================================================================================
/// Draws retired occlusion bounds; its caption sits below the legend chip when one is shown.
void drawOcclusionOverlay(const ViewportPanelContext& context, ImVec2 origin, ImVec2 size,
                          std::optional<float> chipBottom) {
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
        const float labelY = chipBottom ? *chipBottom + 8 : origin.y + 8;
        draw->AddText(ImVec2(origin.x + 8, labelY), IM_COL32(255, 240, 180, 255), label.c_str());
    }
    draw->PopClipRect();
}

} // namespace

//======================================================================================================================
uint32_t viewportHzbLevels(const render::Renderer& renderer) {
    if (renderer.width() == 0 || renderer.height() == 0)
        return 0;
    return render::hzbLayout(renderer.width(), renderer.height()).levelCount;
}

//======================================================================================================================
ViewportPanelResult drawViewportPanel(bool& open, const ViewportPanelContext& context) {
    ViewportPanelResult result;
    if (ImGui::Begin(kViewportPanelWindowName, &open)) {
        const auto available = ImGui::GetContentRegionAvail();
        const ImVec2 imageSize(available.x, std::max(available.y, 1.0f));
        result.measured = available.x > 0.0f && available.y > 0.0f;
        result.focused = ImGui::IsWindowFocused();
        if (result.measured) {
            const bool outlineReady = context.outlineTarget.width() == context.renderer.width() &&
                                      context.outlineTarget.height() == context.renderer.height();
            ImGui::Image(rojoRHI::metal4::imguiTextureID(context.showOutline && outlineReady &&
                                                                 context.selection.subject ==
                                                                     EditorSubject::Object
                                                             ? context.outlineTarget
                                                             : context.renderer.colorTarget()),
                         imageSize);
            result.hovered = ImGui::IsItemHovered();
            const auto origin = ImGui::GetItemRectMin();
            const auto chip = drawLegendChip(context, origin, imageSize);
            if (chip.hovered)
                result.hovered = false;
            drawOcclusionOverlay(context, origin, imageSize, chip.bottom);
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
