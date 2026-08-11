//----------------------------------------------------------------------------------------------------------------------
/// @file GraphInspectorModel.h
/// @brief Declares the ImGui-free row shaping behind the Render Graph inspector panel.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "RHI/RHI.h"
#include "Render/RenderGraph.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace lmx::app {

/// One imported or transient resource, named for display.
struct GraphInspectorResourceRow {
    uint32_t index = 0; ///< Index into CompiledFrameDebug::resources.
    render::GraphResourceKind kind = render::GraphResourceKind::Texture; ///< Texture or buffer.
    std::string name;                          ///< The name it was declared under.
    rhi::Format format = rhi::Format::Unknown; ///< Declared format; Unknown for a buffer.
};

/// One declared use of one resource version by one pass, with the resource's name resolved so the
/// panel need not look it up.
struct GraphInspectorUseRow {
    uint32_t resource = 0;    ///< Index into GraphInspectorModel::resources.
    std::string resourceName; ///< The resource's name, copied for direct display.
    uint32_t version = 0;     ///< The version the pass named.
    render::UseRole role = render::UseRole::Read; ///< What the pass does with it.
    std::string rangeText;                        ///< describeRange() text; empty for a buffer use.
};

/// One declared pass, in declaration order -- scheduled or culled alike, exactly as the compiled
/// frame's DebugPass list carries them. `gpuMilliseconds` is set only when the joined timings name
/// the same schedule position and whose label matches this one's; a culled pass, which never ran,
/// is never matched.
struct GraphInspectorPassRow {
    uint32_t index = 0; ///< Declaration index (CompiledFrameDebug::passes).
    render::PassKind kind = render::PassKind::Raster; ///< Which declaration path declared it.
    std::string label;                                ///< The label it was declared with.
    std::optional<render::CullReason>
        cullReason;                         ///< Why it was culled, or empty if it is scheduled.
    std::optional<double> gpuMilliseconds;  ///< Joined GPU time, or empty if unmeasured.
    std::vector<GraphInspectorUseRow> uses; ///< Every resource version it named.
};

/// One derived barrier, positioned by the pass it precedes and described the way the dump reads it.
struct GraphInspectorTransitionRow {
    uint32_t beforePass = 0;  ///< Index of the pass the barrier precedes.
    uint32_t resource = 0;    ///< Index into GraphInspectorModel::resources.
    std::string resourceName; ///< The resource's name, copied for direct display.
    std::string description;  ///< "texture rN range From -> To" (or the buffer form).
    std::optional<uint32_t>
        aliasedFrom; ///< Set on a transient reuse boundary; names the prior owner.
};

/// One graph-created transient's lifetime and placement, on DebugTransient's terms.
struct GraphInspectorTransientRow {
    uint32_t resource = 0;    ///< Index into GraphInspectorModel::resources.
    std::string resourceName; ///< The resource's name, copied for direct display.
    bool used = false;      ///< Whether any scheduled pass touches it, and so whether it is placed.
    uint32_t firstPass = 0; ///< First scheduled pass that touches it; meaningless unless `used`.
    uint32_t lastPass = 0;  ///< Last scheduled pass that touches it; meaningless unless `used`.
    uint64_t offset = 0;    ///< Byte offset of its placement within the frame's transient heap.
    uint64_t size = 0;      ///< Bytes the RHI reports the descriptor occupies in a heap.
    uint64_t alignment = 0; ///< Alignment the RHI reports its heap offset must satisfy.
    bool aliases = false;   ///< Whether it took memory an earlier transient's lifetime had freed.
};

/// A display-ready shaping of one compiled frame plus its joined GPU timings.
///
/// It is a pure function of its inputs -- the same record and timings always shape to the same
/// rows -- and carries no ImGui type, so it is unit-testable without a UI and reusable by anything
/// that wants to present a compiled frame (the panel today; potentially a future exporter).
struct GraphInspectorModel {
    uint64_t frameId = 0;                                 ///< The device frame number.
    std::vector<GraphInspectorResourceRow> resources;     ///< Declared resources, import order.
    std::vector<GraphInspectorPassRow> passes;            ///< Declared passes, declaration order.
    std::vector<uint32_t> schedule;                       ///< Pass indices in execution order.
    std::vector<GraphInspectorTransitionRow> transitions; ///< Derived barriers, emission order.
    std::vector<GraphInspectorTransientRow> transients;   ///< Transients, declaration order.
    render::TransientMemory memory; ///< What transients cost, what aliasing saved.
    bool poolingEnabled = false;    ///< Whether aliasing was allowed this frame.
};

/// Shapes a compiled frame record and its joined GPU timings into display-ready rows.
///
/// `timings` need not name every pass, or any: entries join positionally against the proved
/// schedule and only when the label at that same position agrees. This makes duplicate diagnostic
/// labels unambiguous and guarantees a culled pass -- which has no schedule position -- remains
/// unmeasured. Nothing here mutates `record`; the model borrows nothing from it once built.
GraphInspectorModel buildGraphInspectorModel(const render::CompiledFrameRecord& record,
                                             std::span<const rhi::PassTiming> timings);

} // namespace lmx::app
