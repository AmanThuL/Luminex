//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphDetails.cpp
/// @brief Draws selection details and stage expansion controls for the Render Graph panel.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Graph/RenderGraphPanelInternal.h"

#include "App/Model/Graph/GraphInspectorModel.h"
#include "App/Panels/EditorStyle.h"

#include <imgui.h>

#include <algorithm>
#include <format>

namespace lmx::app::graph_panel {

//======================================================================================================================
std::string_view passKindLabel(render::PassKind kind) {
    switch (kind) {
    case render::PassKind::Raster:
        return "raster";
    case render::PassKind::Compute:
        return "compute";
    case render::PassKind::Copy:
        return "copy";
    case render::PassKind::External:
        return "external";
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
void toggleGroupExpansion(GraphLayoutOptions& options, const std::string& key) {
    const auto found = std::ranges::find(options.expandedGroups, key);
    if (found == options.expandedGroups.end()) {
        options.expandedGroups.push_back(key);
    } else {
        options.expandedGroups.erase(found);
    }
}

namespace {

//======================================================================================================================
void drawUseRows(const std::vector<GraphInspectorUseRow>& uses) {
    if (uses.empty()) {
        ImGui::TextDisabled("none");
        return;
    }
    for (size_t index = 0; index < uses.size(); ++index) {
        const auto& use = uses[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::TextWrapped("%s", use.resourceName.c_str());
        ImGui::Text("Resource r%u | version %u | %s", use.resource, use.version,
                    render::roleName(use.role).data());
        if (!use.rangeText.empty()) {
            ImGui::TextWrapped("%s", use.rangeText.c_str());
        }
        if (ImGui::SmallButton("Copy resource")) {
            const auto value = std::format("r{} \"{}\" v{} {} {}", use.resource, use.resourceName,
                                           use.version, render::roleName(use.role), use.rangeText);
            ImGui::SetClipboardText(value.c_str());
        }
        ImGui::Separator();
        ImGui::PopID();
    }
}

//======================================================================================================================
void drawTransientTotals(const render::TransientMemory& memory) {
    ImGui::Text("requested %llu B", static_cast<unsigned long long>(memory.requested));
    ImGui::Text("high-water %llu B", static_cast<unsigned long long>(memory.highWater));
    ImGui::Text("saved %llu B", static_cast<unsigned long long>(memory.aliasSavings));
}

//======================================================================================================================
void drawSinkDetails(const GraphNodeModel& model, const GraphNode& node) {
    ImGui::SeparatorText("Sink");
    ImGui::TextUnformatted(std::format("s{} {}", node.index, node.label).c_str());
    if (!node.inputs.empty()) {
        const GraphNodePin& rooted = node.inputs.front();
        ImGui::TextUnformatted(std::format("roots r{} \"{}\" v{}", rooted.resource,
                                           rooted.resourceName, rooted.version)
                                   .c_str());
    }
    ImGui::SeparatorText("Produced by");
    if (!node.producerPass || *node.producerPass >= model.nodes.size()) {
        ImGui::TextDisabled("none");
        return;
    }
    const GraphNode& producer = model.nodes[*node.producerPass];
    std::string line =
        producer.scheduleOrder
            ? std::format("#{} p{} {} \"{}\"", *producer.scheduleOrder, producer.index,
                          passKindLabel(producer.passKind), producer.label)
            : std::format("p{} {} \"{}\"", producer.index, passKindLabel(producer.passKind),
                          producer.label);
    ImGui::TextUnformatted(line.c_str());
}

//======================================================================================================================
void drawPassDetails(const GraphNode& node) {
    if (node.cullReason) {
        ImGui::SeparatorText("Culled pass");
        ImGui::TextUnformatted(
            std::format("p{} {} \"{}\"", node.index, passKindLabel(node.passKind), node.label)
                .c_str());
        ImGui::TextDisabled(
            "%s",
            std::format("culled: {} -- never executed", cullReasonLabel(*node.cullReason)).c_str());
        ImGui::SeparatorText("Declared uses");
        drawUseRows(node.uses);
        return;
    }

    ImGui::SeparatorText("Pass");
    std::string header =
        node.scheduleOrder
            ? std::format("#{} p{} {} \"{}\"", *node.scheduleOrder, node.index,
                          passKindLabel(node.passKind), node.label)
            : std::format("p{} {} \"{}\"", node.index, passKindLabel(node.passKind), node.label);
    ImGui::TextUnformatted(header.c_str());
    if (node.gpuMilliseconds) {
        ImGui::Text("GPU %.3f ms", *node.gpuMilliseconds);
    } else {
        ImGui::TextDisabled("GPU N/A (no matched timing)");
    }

    ImGui::SeparatorText("Uses");
    drawUseRows(node.uses);

    ImGui::SeparatorText("Barriers before");
    if (node.barriersBefore.empty()) {
        ImGui::TextDisabled("none");
    }
    for (const GraphInspectorTransitionRow& barrier : node.barriersBefore) {
        if (barrier.aliasedFrom) {
            ImGui::Text("%s alias-of r%u", barrier.description.c_str(), *barrier.aliasedFrom);
        } else {
            ImGui::TextUnformatted(barrier.description.c_str());
        }
    }

    ImGui::SeparatorText("Transients alive");
    if (node.transientsAlive.empty()) {
        ImGui::TextDisabled("none");
    }
    for (const GraphNodeTransientSpan& transient : node.transientsAlive) {
        ImGui::Text("r%u \"%s\" offset %llu size %llu align %llu%s", transient.resource,
                    transient.resourceName.c_str(),
                    static_cast<unsigned long long>(transient.offset),
                    static_cast<unsigned long long>(transient.size),
                    static_cast<unsigned long long>(transient.alignment),
                    transient.aliases ? " aliased" : "");
    }
}

//======================================================================================================================
// A stage answers for its members: what it is keyed by, what it cost, and which passes it folded
// away. The button is the same act as a double-click on the canvas, for a reader who is here.
void drawGroupDetails(const GraphNodeModel& model, const GraphLayoutGroup& group,
                      GraphLayoutOptions& options) {
    ImGui::SeparatorText("Stage group");
    ImGui::TextUnformatted(group.key.c_str());
    ImGui::Text("%zu passes", group.members.size());
    if (group.gpuMillisecondsSum) {
        ImGui::Text("GPU %.3f ms over %u measured", *group.gpuMillisecondsSum,
                    group.measuredMembers);
    } else {
        ImGui::TextDisabled("GPU N/A (no matched timing)");
    }
    // A group item is drawn only while its stage is folded -- an open stage draws its members
    // instead -- so the only act this pane can offer here is opening it.
    if (ImGui::Button("Expand")) {
        toggleGroupExpansion(options, group.key);
    }
    editorTooltip("Show every pass in this stage. Double-clicking its graph card does the same.");

    ImGui::SeparatorText("Members");
    if (!ImGui::BeginTable("members", 3,
                           ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        return;
    }
    ImGui::TableSetupColumn(group.culled ? "culled" : "#");
    ImGui::TableSetupColumn("pass");
    ImGui::TableSetupColumn("GPU ms");
    ImGui::TableHeadersRow();
    for (const uint32_t member : group.members) {
        const GraphNode& node = model.nodes[member];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (node.cullReason) {
            const std::string_view reason = cullReasonLabel(*node.cullReason);
            ImGui::TextDisabled("%.*s", static_cast<int>(reason.size()), reason.data());
        } else if (node.scheduleOrder) {
            ImGui::Text("#%u", *node.scheduleOrder);
        } else {
            ImGui::TextDisabled("--");
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(node.label.c_str());
        ImGui::TableNextColumn();
        if (node.gpuMilliseconds) {
            ImGui::Text("%.3f", *node.gpuMilliseconds);
        } else {
            ImGui::TextDisabled("--");
        }
    }
    ImGui::EndTable();
}

//======================================================================================================================
// The open stage a node belongs to, or null when it belongs to none. An expanded group draws its
// members rather than itself, so this is the only place its identity still reaches the reader.
const GraphLayoutGroup* expandedGroupOfNode(const GraphLayout& layout, uint32_t nodeIndex) {
    for (const GraphLayoutGroup& group : layout.groups) {
        if (group.expanded && std::ranges::contains(group.members, nodeIndex)) {
            return &group;
        }
    }
    return nullptr;
}

//======================================================================================================================
// An open stage has no group box left on the canvas to fold it back up with, so a member's details
// carry the fold. It is the same act as double-clicking that member, offered to a reader who is
// looking at this pane rather than at the canvas.
void drawMemberStageSection(const GraphLayoutGroup& group, GraphLayoutOptions& options) {
    ImGui::SeparatorText("Stage");
    ImGui::TextUnformatted(group.key.c_str());
    ImGui::Text("%zu passes, expanded", group.members.size());
    if (ImGui::Button(std::format("Collapse {}", group.stage).c_str())) {
        toggleGroupExpansion(options, group.key);
    }
    editorTooltip(
        "Fold this stage into one card while retaining its boundary resource connections.");
}

} // namespace

//======================================================================================================================
void drawDetails(const GraphNodeModel& model, const GraphLayout& layout,
                 RenderGraphPanelState& state) {
    if (!state.selectedItem || *state.selectedItem >= layout.items.size()) {
        ImGui::TextDisabled("nothing selected -- click a node or a stage to inspect it");
        ImGui::SeparatorText("Transients");
        drawTransientTotals(model.memory);
        return;
    }
    const GraphLayoutItem& item = layout.items[*state.selectedItem];
    if (item.kind == GraphLayoutItemKind::Group) {
        drawGroupDetails(model, layout.groups[item.index], state.layoutOptions);
        return;
    }
    const GraphNode& node = model.nodes[item.index];
    if (const GraphLayoutGroup* stage = expandedGroupOfNode(layout, item.index)) {
        drawMemberStageSection(*stage, state.layoutOptions);
    }
    if (node.kind == GraphNodeKind::Sink) {
        drawSinkDetails(model, node);
    } else {
        drawPassDetails(node);
    }
}

} // namespace lmx::app::graph_panel
