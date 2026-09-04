//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphPanel.cpp
/// @brief Implements the Render Graph panel over one exact retired compiled frame.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/RenderGraphPanel.h"

#include "App/GraphInspectorModel.h"
#include "Core/Log.h"
#include "Render/GraphDump.h"

#include <imgui.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace lmx::app {

namespace {

//======================================================================================================================
std::string_view passKindLabel(render::PassKind kind) {
    switch (kind) {
    case render::PassKind::Raster:
        return "raster";
    case render::PassKind::Compute:
        return "compute";
    case render::PassKind::Copy:
        return "copy";
    }
    return "raster";
}

//======================================================================================================================
std::string_view cullReasonLabel(render::CullReason reason) {
    switch (reason) {
    case render::CullReason::ProducesNothing:
        return "produces nothing";
    case render::CullReason::NoSinkReachesIt:
        return "no sink reaches it";
    }
    return "no sink reaches it";
}

//======================================================================================================================
// Shared by the scheduled and culled sections below, so a pass reads identically in both and only
// the reason and schedule position differ.
void drawPassRow(const GraphInspectorPassRow& pass, std::optional<uint32_t> scheduleOrder) {
    std::string header =
        std::format("p{} {} \"{}\"", pass.index, passKindLabel(pass.kind), pass.label);
    if (scheduleOrder) {
        header = std::format("#{} {}", *scheduleOrder, header);
    }
    if (pass.gpuMilliseconds) {
        header += std::format(" -- {:.3f} ms", *pass.gpuMilliseconds);
    }
    if (pass.cullReason) {
        header += std::format(" -- culled: {}", cullReasonLabel(*pass.cullReason));
    }
    // TreeNode with no explicit ID derives one from the whole label, so a label that changes every
    // frame (the GPU time above) would reopen a fresh, always-collapsed node each frame. "###"
    // tells ImGui to hash only what follows it for the ID while still displaying everything before
    // it, so the visible text can keep changing while the node's open/closed state stays put.
    header += std::format("###p{}", pass.index);
    ImGui::PushID(static_cast<int>(pass.index));
    if (ImGui::TreeNode(header.c_str())) {
        for (const GraphInspectorUseRow& use : pass.uses) {
            std::string line = std::format("{} r{} \"{}\" v{}", render::roleName(use.role),
                                           use.resource, use.resourceName, use.version);
            if (!use.rangeText.empty()) {
                line += std::format(" {}", use.rangeText);
            }
            ImGui::TextUnformatted(line.c_str());
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

} // namespace

//======================================================================================================================
void drawRenderGraphPanel(bool& open, const FrameRecordRing& frameRecords) {
    if (!ImGui::Begin(kRenderGraphPanelWindowName, &open)) {
        ImGui::End();
        return;
    }

    const RetainedFrame* newest = frameRecords.newestTimedFrame();
    if (newest == nullptr) {
        // Nothing has retired yet -- true for the first few frames of a run, and not an error.
        ImGui::TextUnformatted("no retired frame yet");
        ImGui::End();
        return;
    }

    const GraphInspectorModel model = buildGraphInspectorModel(newest->record, newest->timings);

    ImGui::Text("frame %llu -- pooling %s", static_cast<unsigned long long>(model.frameId),
                model.poolingEnabled ? "on" : "off");
    ImGui::Text("transients: requested %llu B, high-water %llu B, saved %llu B",
                static_cast<unsigned long long>(model.memory.requested),
                static_cast<unsigned long long>(model.memory.highWater),
                static_cast<unsigned long long>(model.memory.aliasSavings));

    if (ImGui::Button("Dump frame")) {
        const std::string filename = std::format("graph-dump-frame-{}.txt", model.frameId);
        std::ofstream file(filename, std::ios::binary | std::ios::trunc);
        if (file) {
            file << render::dumpCompiledFrame(newest->record);
            LMX_LOG_INFO("render-graph frame {} dumped to '{}'", model.frameId,
                         std::filesystem::absolute(filename).string());
        } else {
            LMX_LOG_ERROR("Render Graph panel: cannot open '{}' for writing", filename);
        }
    }

    if (ImGui::CollapsingHeader("Resources", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const GraphInspectorResourceRow& resource : model.resources) {
            const std::string line =
                resource.kind == render::GraphResourceKind::Texture
                    ? std::format("r{} texture \"{}\" {}", resource.index, resource.name,
                                  render::formatName(resource.format))
                    : std::format("r{} buffer \"{}\"", resource.index, resource.name);
            ImGui::TextUnformatted(line.c_str());
        }
    }

    if (ImGui::CollapsingHeader("Schedule", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (uint32_t order = 0; order < model.schedule.size(); ++order) {
            drawPassRow(model.passes[model.schedule[order]], order);
        }
    }

    if (ImGui::CollapsingHeader("Culled passes")) {
        for (const GraphInspectorPassRow& pass : model.passes) {
            if (pass.cullReason) {
                drawPassRow(pass, std::nullopt);
            }
        }
    }

    if (ImGui::CollapsingHeader("Transitions")) {
        for (const GraphInspectorTransitionRow& transition : model.transitions) {
            if (transition.aliasedFrom) {
                ImGui::Text("before p%u %s alias-of r%u", transition.beforePass,
                            transition.description.c_str(), *transition.aliasedFrom);
            } else {
                ImGui::Text("before p%u %s", transition.beforePass, transition.description.c_str());
            }
        }
    }

    if (ImGui::CollapsingHeader("Transients")) {
        for (const GraphInspectorTransientRow& transient : model.transients) {
            if (!transient.used) {
                ImGui::Text("r%u \"%s\" unused", transient.resource,
                            transient.resourceName.c_str());
                continue;
            }
            ImGui::Text("r%u \"%s\" passes p%u..p%u offset %llu size %llu align %llu%s",
                        transient.resource, transient.resourceName.c_str(), transient.firstPass,
                        transient.lastPass, static_cast<unsigned long long>(transient.offset),
                        static_cast<unsigned long long>(transient.size),
                        static_cast<unsigned long long>(transient.alignment),
                        transient.aliases ? " aliased" : "");
        }
    }

    ImGui::End();
}

} // namespace lmx::app
