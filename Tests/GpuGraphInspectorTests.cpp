#include "GpuTestSupport.h"

#include "App/FrameRecordRing.h"
#include "App/GraphInspectorModel.h"
#include "App/GraphLayout.h"
#include "App/GraphNodeModel.h"
#include "Render/GraphDump.h"
#include "Render/RenderGraph.h"
#include "Render/TransientPool.h"

#include <algorithm>
#include <format>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

//======================================================================================================================
// Pulls the pass labels out of one section of a text dump, in the order they appear -- the same
// ordering promise GraphDump.h documents for that section. Only header lines ("  pN kind
// \"label\"") count; a use line is indented two spaces further and is skipped.
std::vector<std::string> passLabelsInSection(const std::string& dump, const std::string& header,
                                             const std::string& nextHeader) {
    const size_t start = dump.find(header + "\n");
    REQUIRE(start != std::string::npos);
    const size_t contentStart = start + header.size() + 1;
    const size_t end = dump.find(nextHeader + "\n", contentStart);
    REQUIRE(end != std::string::npos);

    std::vector<std::string> labels;
    std::istringstream section(dump.substr(contentStart, end - contentStart));
    std::string line;
    while (std::getline(section, line)) {
        if (line.size() < 3 || line[0] != ' ' || line[1] != ' ' || line[2] != 'p') {
            continue; // A use line, indented four spaces rather than a pass header's two.
        }
        const size_t quoteStart = line.find('"');
        const size_t quoteEnd = line.find('"', quoteStart + 1);
        REQUIRE(quoteStart != std::string::npos);
        REQUIRE(quoteEnd != std::string::npos);
        labels.push_back(line.substr(quoteStart + 1, quoteEnd - quoteStart - 1));
    }
    return labels;
}

} // namespace

//======================================================================================================================
// Stage 5's exit condition, driven by a real device: the same compiled frame is inspectable through
// the dump and through the model the panel renders from, and the two agree on what ran, what was
// culled, and what the transients cost -- without ImGui in the loop at all.
TEST_CASE("the inspector model agrees with the dump for the same frame", "[gpu]") {
    using namespace lmx::rhi;
    using namespace lmx::render;

    constexpr uint64_t kBytes = 256;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto target = (*device)->createBuffer(
        {.size = kBytes, .storageRead = true, .label = "lmx.test.inspector.target"}, nullptr);
    INFO(errorOf(target));
    REQUIRE(target.has_value());

    auto orphan = (*device)->createBuffer(
        {.size = kBytes, .storageRead = true, .label = "lmx.test.inspector.orphan"}, nullptr);
    INFO(errorOf(orphan));
    REQUIRE(orphan.has_value());

    TransientPool pool(**device);
    lmx::app::FrameRecordRing records;

    CommandList& commands = (*device)->beginFrame();
    pool.beginFrame();

    RenderGraph graph(pool);
    const GraphBuffer staging = graph.createBuffer({.size = kBytes}, "lmx.test.inspector.staging");
    const GraphBuffer targetHandle = graph.importBuffer(**target, "lmx.test.inspector.target");
    const GraphBuffer orphanHandle = graph.importBuffer(**orphan, "lmx.test.inspector.orphan");

    CopyPassDesc fillStaging;
    fillStaging.bufferDestinations.push_back(staging);
    graph.addCopyPass("lmx.test.inspector.fillStaging", fillStaging,
                      [&](const PassResources& resources) {
                          const GraphResult<Buffer*> buffer = resources.buffer(staging);
                          REQUIRE(buffer.has_value());
                          commands.fillBuffer(**buffer, 0, kBytes, 0);
                      });

    CopyPassDesc moveToTarget;
    moveToTarget.bufferSources.push_back(nextVersion(staging));
    moveToTarget.bufferDestinations.push_back(targetHandle);
    graph.addCopyPass(
        "lmx.test.inspector.moveToTarget", moveToTarget, [&](const PassResources& resources) {
            const GraphResult<Buffer*> source = resources.buffer(nextVersion(staging));
            REQUIRE(source.has_value());
            const GraphResult<Buffer*> destination = resources.buffer(targetHandle);
            REQUIRE(destination.has_value());
            commands.copyBuffer(**source, 0, **destination, 0, kBytes);
        });

    // Declared, writes, but nothing reads or exports it: culled with NoSinkReachesIt, never run.
    CopyPassDesc orphanFill;
    orphanFill.bufferDestinations.push_back(orphanHandle);
    graph.addCopyPass("lmx.test.inspector.orphanFill", orphanFill,
                      [&](const PassResources&) { FAIL("a culled pass must never execute"); });

    graph.exportBuffer(nextVersion(targetHandle));

    records.retain(graph.execute(commands, (*device)->frameNumber()));
    records.joinTimings((*device)->passTimingsFrame(), (*device)->passTimings());
    (*device)->endFrame(nullptr);

    // The one sequence passTimings() documents as publishing a specific frame: drain, then open one
    // more frame, which publishes the frame that just retired.
    (*device)->waitIdle();
    (*device)->beginFrame();
    const uint64_t measured = (*device)->passTimingsFrame();
    REQUIRE(records.joinTimings(measured, (*device)->passTimings()));
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    const lmx::app::RetainedFrame* newest = records.newestTimedFrame();
    REQUIRE(newest != nullptr);
    REQUIRE(newest->record.frameId == measured);
    REQUIRE(newest->timed);

    const std::string dump = dumpCompiledFrame(newest->record);
    const lmx::app::GraphInspectorModel model =
        lmx::app::buildGraphInspectorModel(newest->record, newest->timings);

    // Same passes, same order.
    std::vector<std::string> modelScheduled;
    for (const uint32_t index : model.schedule) {
        modelScheduled.push_back(model.passes[index].label);
    }
    REQUIRE(modelScheduled == passLabelsInSection(dump, "passes", "culled"));
    REQUIRE(modelScheduled == std::vector<std::string>{"lmx.test.inspector.fillStaging",
                                                       "lmx.test.inspector.moveToTarget"});

    // Same culled set.
    std::vector<std::string> modelCulled;
    for (const lmx::app::GraphInspectorPassRow& pass : model.passes) {
        if (pass.cullReason) {
            modelCulled.push_back(pass.label);
        }
    }
    REQUIRE(modelCulled == passLabelsInSection(dump, "culled", "transitions"));
    REQUIRE(modelCulled == std::vector<std::string>{"lmx.test.inspector.orphanFill"});

    // Same transient totals -- one placed transient, so the frame requests and holds the same
    // bytes, and the dump's own memory line names the same numbers.
    REQUIRE(model.transients.size() == 1);
    REQUIRE(model.transients[0].used);
    REQUIRE(model.memory.requested == newest->record.debug.memory.requested);
    REQUIRE(model.memory.highWater == newest->record.debug.memory.highWater);
    REQUIRE(model.memory.aliasSavings == newest->record.debug.memory.aliasSavings);
    REQUIRE(model.memory.requested > 0);
    const std::string memoryLine = std::format(
        "pooling {} requested {} high-water {} saved {}", model.poolingEnabled ? "on" : "off",
        model.memory.requested, model.memory.highWater, model.memory.aliasSavings);
    REQUIRE(dump.find(memoryLine) != std::string::npos);

    // The pass that actually ran carries a real GPU time; the culled one carries none.
    REQUIRE(modelScheduled.size() == 2);
    for (const uint32_t index : model.schedule) {
        INFO("pass '" + model.passes[index].label + "'");
        REQUIRE(model.passes[index].gpuMilliseconds.has_value());
        REQUIRE(*model.passes[index].gpuMilliseconds >= 0.0);
    }
    for (const lmx::app::GraphInspectorPassRow& pass : model.passes) {
        if (pass.cullReason) {
            REQUIRE_FALSE(pass.gpuMilliseconds.has_value());
        }
    }

    // The node canvas is shaped from the same retained frame and must agree with both the dump and
    // the list model above -- frame identity, scheduled order, the culled set, and that no edge
    // names a culled pass.
    const lmx::app::GraphNodeModel nodeModel =
        lmx::app::buildGraphNodeModel(newest->record, newest->timings);
    REQUIRE(nodeModel.frameId == newest->record.frameId);

    std::vector<std::pair<uint32_t, std::string>> scheduledNodes;
    for (const lmx::app::GraphNode& node : nodeModel.nodes) {
        if (node.kind == lmx::app::GraphNodeKind::Pass && node.scheduleOrder.has_value()) {
            scheduledNodes.emplace_back(*node.scheduleOrder, node.label);
        }
    }
    std::sort(scheduledNodes.begin(), scheduledNodes.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::string> nodeModelScheduled;
    for (const auto& [order, label] : scheduledNodes) {
        nodeModelScheduled.push_back(label);
    }
    REQUIRE(nodeModelScheduled == modelScheduled);

    std::vector<std::string> nodeModelCulled;
    for (const lmx::app::GraphNode& node : nodeModel.nodes) {
        if (node.kind == lmx::app::GraphNodeKind::Pass && node.cullReason.has_value()) {
            nodeModelCulled.push_back(node.label);
        }
    }
    std::sort(nodeModelCulled.begin(), nodeModelCulled.end());
    std::vector<std::string> sortedModelCulled = modelCulled;
    std::sort(sortedModelCulled.begin(), sortedModelCulled.end());
    REQUIRE(nodeModelCulled == sortedModelCulled);
    // Anchored, not just equal-to-each-other: this frame demonstrably culls one pass, so an
    // implementation that culled nothing would still satisfy the equality above vacuously.
    REQUIRE(nodeModelCulled == std::vector<std::string>{"lmx.test.inspector.orphanFill"});

    REQUIRE_FALSE(nodeModel.edges.empty());
    for (const lmx::app::GraphNodeEdge& edge : nodeModel.edges) {
        const lmx::app::GraphNode& from = nodeModel.nodes[edge.fromNode];
        INFO("edge from '" + from.label + "'");
        REQUIRE(from.kind == lmx::app::GraphNodeKind::Pass);
        REQUIRE(from.scheduleOrder.has_value());

        const lmx::app::GraphNode& to = nodeModel.nodes[edge.toNode];
        INFO("edge to '" + to.label + "'");
        if (to.kind == lmx::app::GraphNodeKind::Pass) {
            REQUIRE(to.scheduleOrder.has_value());
        }
    }

    // The layout groups the two scheduled passes under their shared "lmx.test.inspector" prefix
    // and leaves the lone culled pass ungrouped: fillStaging and moveToTarget share that prefix
    // (their four-segment labels clear the stage-key threshold) and are both scheduled, so they
    // form a two-member group, while orphanFill shares the prefix too but was culled -- its own
    // would-be group has one member and never reaches the two-member minimum, so it stays a plain,
    // ungrouped item.
    const lmx::app::GraphLayout collapsed = lmx::app::layoutGraph(
        nodeModel, lmx::app::GraphLayoutOptions{.columnsPerRow = 0, .expandedGroups = {}});

    REQUIRE(collapsed.groups.size() == 1);
    REQUIRE(collapsed.groups[0].key == "lmx.test.inspector");
    REQUIRE_FALSE(collapsed.groups[0].culled);
    REQUIRE(collapsed.groups[0].members.size() == 2);

    uint32_t collapsedGroupMemberTotal = 0;
    for (const lmx::app::GraphLayoutGroup& group : collapsed.groups) {
        collapsedGroupMemberTotal += static_cast<uint32_t>(group.members.size());
    }
    REQUIRE(collapsed.items.size() ==
            nodeModel.nodes.size() - collapsedGroupMemberTotal + collapsed.groups.size());
    // Anchored: 4 nodes (2 scheduled passes, 1 culled pass, 1 sink), one 2-member group folded to
    // one box, so 3 items -- an implementation that formed no groups would still satisfy the
    // arithmetic above vacuously.
    REQUIRE(collapsed.items.size() == 3);

    std::vector<std::string> expandedKeys;
    for (const lmx::app::GraphLayoutGroup& group : collapsed.groups) {
        expandedKeys.push_back(group.key);
    }
    const lmx::app::GraphLayout expanded = lmx::app::layoutGraph(
        nodeModel,
        lmx::app::GraphLayoutOptions{.columnsPerRow = 0, .expandedGroups = expandedKeys});
    // Expanding the only group restores the M5.4 item-per-node picture.
    REQUIRE(expanded.items.size() == nodeModel.nodes.size());

    // What collapsing owes the reader: an edge may not point at a pass the picture folded away,
    // and it may not be a box pointing at itself. Both endpoints are checked against the actual
    // membership of every folded group rather than against the item count, which any edge index
    // satisfies by construction.
    std::vector<uint32_t> hiddenNodes;
    for (const lmx::app::GraphLayoutGroup& group : collapsed.groups) {
        if (group.expanded) {
            continue;
        }
        hiddenNodes.insert(hiddenNodes.end(), group.members.begin(), group.members.end());
    }
    REQUIRE_FALSE(hiddenNodes.empty());

    REQUIRE_FALSE(collapsed.edges.empty());
    for (const lmx::app::GraphLayoutEdge& edge : collapsed.edges) {
        for (const uint32_t endpoint : {edge.fromItem, edge.toItem}) {
            const lmx::app::GraphLayoutItem& item = collapsed.items[endpoint];
            if (item.kind != lmx::app::GraphLayoutItemKind::Node) {
                continue;
            }
            INFO("edge endpoint draws node '" + nodeModel.nodes[item.index].label + "'");
            REQUIRE(std::find(hiddenNodes.begin(), hiddenNodes.end(), item.index) ==
                    hiddenNodes.end());
        }
        REQUIRE(edge.fromItem != edge.toItem);
    }

    // Expanding hides nothing, so the only property left to hold is that no box points at itself.
    for (const lmx::app::GraphLayoutEdge& edge : expanded.edges) {
        REQUIRE(edge.fromItem != edge.toItem);
    }
}
