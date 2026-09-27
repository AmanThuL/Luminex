//----------------------------------------------------------------------------------------------------------------------
/// @file RenderingPanel.cpp
/// @brief Implements the dockable Rendering panel and scoped topic reset actions.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Panels/Rendering/RenderingPanel.h"
#include "App/Model/Rendering/Lighting/LightingHistory.h"
#include "App/Model/Rendering/Settings/EditorRenderDefaults.h"
#include "App/Model/Rendering/Visibility/VisibilityDiagnostics.h"
#include "App/Panels/Rendering/RenderingInternal.h"
#include "App/Panels/Shared/EditorStyle.h"
#include <algorithm>
#include <array>
#include <optional>
#include <string>
namespace lmx::app {
namespace {
//======================================================================================================================
std::optional<EditorRenderGroup> resetScope(RenderingCategory topic) {
    switch (topic) {
    case RenderingCategory::Reconstruction:
        return EditorRenderGroup::Reconstruction;
    case RenderingCategory::Resolution:
        return EditorRenderGroup::Resolution;
    case RenderingCategory::Lighting:
        return EditorRenderGroup::Lighting;
    case RenderingCategory::Exposure:
        return EditorRenderGroup::Exposure;
    case RenderingCategory::Bloom:
        return EditorRenderGroup::Bloom;
    case RenderingCategory::Shadows:
        return EditorRenderGroup::Shadows;
    case RenderingCategory::Display:
        return EditorRenderGroup::Display;
    default:
        return std::nullopt;
    }
}
//======================================================================================================================
void resetTopic(const InspectorPanelContext& context, EditorRenderGroup scope) {
    const auto previousMode = context.settings.localLightMode;
    const bool pileChanged =
        scope == EditorRenderGroup::Lighting && context.session.lightLabPileCount() > 0;
    resetRenderingGroup(context.settings, scope);
    if (scope == EditorRenderGroup::Exposure) {
        setAutoExposureEnabled(context.settings, context.exposureContext,
                               context.exposureResetPending, context.settings.autoExposureEnabled);
    }
    if (scope == EditorRenderGroup::Display) {
        constexpr std::array kClear{0.05f, 0.07f, 0.10f, 1.0f};
        std::copy(kClear.begin(), kClear.end(), context.renderer.clearColor);
    }
    if (scope == EditorRenderGroup::Lighting) {
        if (context.session.lightLabPileAvailable()) {
            context.session.setLightLabPile(0);
            ImGui::PushID(&context.session.scene());
            ImGui::GetStateStorage()->SetInt(ImGui::GetID("pileDraft"), 0);
            ImGui::PopID();
        }
        if (lightingChangeNeedsHistoryReset(previousMode, context.settings.localLightMode,
                                            context.session.scene().enabledLightCount(),
                                            pileChanged))
            requestCameraCut(context.temporalState);
    }
}
} // namespace
//======================================================================================================================
void drawPerformanceDetails(const InspectorPanelContext& context) {
    if (editor_style::iconButton("performanceDetails", EditorIcon::Details,
                                 bool(context.openPerformance),
                                 "Open Performance for pass costs and timing details."))
        context.openPerformance();
}
//======================================================================================================================
void drawRenderingPanel(bool& open, const InspectorPanelContext& context) {
    ImGui::SetNextWindowSize(ImVec2(360.0f, 600.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(kRenderingPanelWindowName, &open)) {
        const auto& latestVisibility = context.visibilityDisplay
                                           ? context.visibilityDisplay->status()
                                           : context.renderer.visibilityStatus();
        if (latestVisibility.frameNumber != 0 &&
            latestVisibility.sceneGeneration == context.temporalState.sceneGeneration &&
            (latestVisibility.classifyMode == render::ClassifyMode::Cpu ||
             latestVisibility.isRetired)) {
            const auto failure = visibilityFailure(latestVisibility);
            if (!failure.empty())
                editor_style::message(failure.c_str(), true);
        }

        for (size_t index = 1; index < static_cast<size_t>(RenderingCategory::Count); ++index) {
            const auto topic = static_cast<RenderingCategory>(index);
            ImGui::PushID(static_cast<int>(topic));
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_AllowOverlap;
            if (topic == RenderingCategory::Reconstruction)
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            const bool expanded =
                ImGui::CollapsingHeader(renderingCategoryLabel(topic).data(), flags);
            if (const auto scope = resetScope(topic)) {
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x -
                                editor_style::iconButtonWidth(EditorIcon::Reset));
                std::string tooltip =
                    "Restore " + std::string(renderingCategoryLabel(topic)) +
                    " defaults; other topics, camera and playback stay unchanged.";
                if (*scope == EditorRenderGroup::Lighting)
                    tooltip +=
                        " Clears the LightLab overflow pile; individual lights retain their edits.";
                if (*scope == EditorRenderGroup::Display)
                    tooltip += " Also restores the clear color.";
                if (editor_style::iconButton("resetTopic", EditorIcon::Reset, true,
                                             tooltip.c_str()))
                    resetTopic(context, *scope);
            }
            if (expanded)
                drawRenderingTopic(context, topic);
            ImGui::PopID();
        }
    }
    ImGui::End();
}
} // namespace lmx::app
