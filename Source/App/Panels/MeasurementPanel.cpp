//----------------------------------------------------------------------------------------------------------------------
/// @file MeasurementPanel.cpp
/// @brief Implements responsive controls for live unscored measurement runs.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Panels/MeasurementPanel.h"
#include "App/Panels/EditorStyle.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <imgui.h>
namespace lmx::app {
//======================================================================================================================
void drawMeasurementSection(MeasurementPanelContext& context) {
    if (context.reveal)
        ImGui::SetNextItemOpen(true);
    const bool expanded = ImGui::CollapsingHeader("Measure");
    if (context.reveal) {
        ImGui::SetScrollHereY(0.0f);
        context.reveal = false;
    }
    if (!expanded)
        return;
    const auto& run = context.run;
    editor_style::message("Interactive / unscored. Live viewport, UI and presentation; each frame "
                          "waits for retirement.");
    editor_style::message("Choose Measure in the top toolbar, then Play. Stop cancels the run. "
                          "Closing this window keeps measurement running.");
    ImGui::BeginDisabled(run.active());
    if (editor_style::beginFields("measurePlan")) {
        int warmup = static_cast<int>(context.warmup);
        int frames = static_cast<int>(context.frames);
        editor_style::field("Warmup frames");
        if (ImGui::InputInt("##measureWarmup", &warmup))
            context.warmup = std::clamp(warmup, 0, 10000);
        editor_style::field("Measured frames");
        if (ImGui::InputInt("##measureFrames", &frames))
            context.frames = std::clamp(frames, 1, 10000);
        editor_style::endFields();
    }
    ImGui::EndDisabled();
    const std::array<const char*, 6> names{"Idle",     "Warmup",   "Measuring",
                                           "Draining", "Complete", "Cancelled"};
    ImGui::TextWrapped("%s | %zu / %u measured frames", names[static_cast<size_t>(run.state())],
                       run.samples().size(), run.plan().measuredFrames);
    if (!run.failure().empty())
        editor_style::message(run.failure().c_str(), true);
    if (run.state() == MeasurementState::Complete) {
        double cpu = 0, gpu = 0;
        for (const auto& sample : run.samples()) {
            cpu += sample.cpu.encodeMs;
            for (const auto& pass : sample.passes)
                gpu += pass.gpuMilliseconds;
        }
        const auto summary = std::format("Mean encode {:.3f} ms | timed GPU sum {:.3f} ms",
                                         cpu / run.samples().size(), gpu / run.samples().size());
        ImGui::TextWrapped("%s", summary.c_str());
    }
    if (run.state() == MeasurementState::Complete || run.state() == MeasurementState::Cancelled) {
        std::array<char, 1024> path{};
        context.exportPath.copy(path.data(), path.size() - 1);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##measurementExport", path.data(), path.size()))
            context.exportPath = path.data();
        if (ImGui::Button("Export measurement JSON"))
            context.action = MeasurementAction::Export;
    }
    if (!context.feedback.empty())
        editor_style::message(context.feedback.c_str());
}
} // namespace lmx::app
