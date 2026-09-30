//----------------------------------------------------------------------------------------------------------------------
/// @file MeasurementPanel.cpp
/// @brief Implements responsive controls for live unscored measurement runs.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Panels/Performance/MeasurementPanel.h"
#include "App/Panels/Shared/EditorStyle.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <imgui.h>
namespace lmx::app {
//======================================================================================================================
void drawMeasurementSection(MeasurementPanelContext& context) {
    const auto& run = context.run;
    editor_style::beginHeaderRow();
    const std::string startHelp = run.active()
                                      ? "A measurement is already running. Stop cancels it."
                                  : !context.startDisabledReason.empty()
                                      ? std::string(context.startDisabledReason)
                                      : "Start the fixed measurement plan. Interactive / unscored: "
                                        "live viewport, UI and presentation; each frame waits for "
                                        "retirement. Closing this window keeps the run active.";
    // A labelled button: the shared Play glyph's text fallback would read "Play" here.
    ImGui::BeginDisabled(run.active() || !context.startDisabledReason.empty());
    if (editor_style::primaryButton("Start measurement##StartMeasurement"))
        context.action = MeasurementAction::Start;
    ImGui::EndDisabled();
    editorTooltip(startHelp.c_str());
    ImGui::SameLine();
    if (editor_style::iconButton(
            "StopMeasurement", EditorIcon::Stop, run.active(),
            "Cancel measurement and restore its starting scene and camera state."))
        context.action = MeasurementAction::Stop;
    editor_style::endHeaderRow();
    ImGui::BeginDisabled(run.active());
    if (editor_style::beginPropertyGrid("measurePlan")) {
        int warmup = static_cast<int>(context.warmup);
        int frames = static_cast<int>(context.frames);
        editor_style::field("Warmup frames");
        ImGui::SetNextItemWidth(
            std::min(ImGui::GetContentRegionAvail().x, editor_style::scaled(140.0f)));
        if (ImGui::InputInt("##measureWarmup", &warmup))
            context.warmup = std::clamp(warmup, 0, 10000);
        editor_style::field("Measured frames");
        ImGui::SetNextItemWidth(
            std::min(ImGui::GetContentRegionAvail().x, editor_style::scaled(140.0f)));
        if (ImGui::InputInt("##measureFrames", &frames))
            context.frames = std::clamp(frames, 1, 10000);
        editor_style::endFields();
    }
    ImGui::EndDisabled();
    const std::array<const char*, 6> names{"Idle",     "Warmup",   "Measuring",
                                           "Draining", "Complete", "Cancelled"};
    if (run.state() != MeasurementState::Idle)
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
        {
            const editor_style::ScopedType type(TypeRole::MonoCaption);
            if (ImGui::InputText("##measurementExport", path.data(), path.size()))
                context.exportPath = path.data();
        }
        if (ImGui::Button("Export measurement JSON"))
            context.action = MeasurementAction::Export;
    }
    if (!context.feedback.empty())
        editor_style::message(context.feedback.c_str());
}
} // namespace lmx::app
