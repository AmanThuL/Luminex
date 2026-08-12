//----------------------------------------------------------------------------------------------------------------------
/// @file PerformancePanel.cpp
/// @brief Implements the Performance panel's frame-time plot and rolling GPU pass table.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/PerformancePanel.h"

#include <imgui.h>

namespace lmx::app {

namespace {

// A fixed ceiling keeps the frame-time graph comparable over time.
constexpr float kFrameTimePlotCeilingMs = 33.3f;

} // namespace

//======================================================================================================================
void drawPerformancePanel(bool& open, const PerformancePanelContext& context) {
    if (ImGui::Begin(kPerformancePanelWindowName, &open)) {
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("%.1f FPS (%.2f ms)", static_cast<double>(io.Framerate),
                    io.Framerate > 0.0f ? 1000.0 / static_cast<double>(io.Framerate) : 0.0);
        ImGui::Text("viewport %u x %u px%s%s", context.viewportWidth, context.viewportHeight,
                    context.viewportHovered ? "  hovered" : "",
                    context.viewportFocused ? "  focused" : "");
        ImGui::Text("scene target %u x %u px", context.sceneTargetWidth, context.sceneTargetHeight);
        ImGui::PlotLines("##frameTimes", context.frameTimesMs.data(),
                         static_cast<int>(context.frameTimesMs.size()),
                         static_cast<int>(context.frameTimeCursor), "frame time (ms)", 0.0f,
                         kFrameTimePlotCeilingMs, ImVec2(0.0f, 60.0f));
        ImGui::Checkbox("Pause GPU timings", &context.passTimingsPaused);
        ImGui::TextDisabled("60-frame average -- updates 4x/s");
        // Schedule changes reset every series together, so these rows never average timings
        // from unlike graph shapes. The exact newest frame remains available in Render Graph.
        if (context.passTimings.empty()) {
            ImGui::TextDisabled("waiting for retired GPU timings");
        }
        for (const PassTimingSummary& timing : context.passTimings) {
            ImGui::Text("%s: %.3f ms", timing.label.c_str(), timing.averageGpuMilliseconds);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("latest %.3f ms\nrange %.3f..%.3f ms\n%zu samples",
                                  timing.latestGpuMilliseconds, timing.minimumGpuMilliseconds,
                                  timing.maximumGpuMilliseconds, timing.sampleCount);
            }
        }
    }
    ImGui::End();
}

} // namespace lmx::app
