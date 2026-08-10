//----------------------------------------------------------------------------------------------------------------------
/// @file GraphInspectorModel.cpp
/// @brief Implements the row shaping behind the Render Graph inspector panel.
//----------------------------------------------------------------------------------------------------------------------

#include "App/GraphInspectorModel.h"

#include <format>

namespace lmx::app {
namespace {

using render::CompiledFrameDebug;
using render::CompiledFrameRecord;

//======================================================================================================================
// Mirrors GraphDump.cpp's private naming, which the dump has no reason to export: the two clients
// read the same enumerators and should say the same words for them, but neither owns the other's
// vocabulary. Source/Render is out of scope for this panel, so the words are duplicated rather than
// shared.
std::string_view textureUseName(rhi::TextureUse use) {
    switch (use) {
    case rhi::TextureUse::RenderTarget:
        return "RenderTarget";
    case rhi::TextureUse::ShaderRead:
        return "ShaderRead";
    case rhi::TextureUse::StorageRead:
        return "StorageRead";
    case rhi::TextureUse::StorageWrite:
        return "StorageWrite";
    case rhi::TextureUse::CopySource:
        return "CopySource";
    case rhi::TextureUse::CopyDestination:
        return "CopyDestination";
    }
    return "ShaderRead";
}

//======================================================================================================================
std::string_view bufferUseName(rhi::BufferUse use) {
    switch (use) {
    case rhi::BufferUse::ShaderRead:
        return "ShaderRead";
    case rhi::BufferUse::StorageRead:
        return "StorageRead";
    case rhi::BufferUse::StorageWrite:
        return "StorageWrite";
    case rhi::BufferUse::CopySource:
        return "CopySource";
    case rhi::BufferUse::CopyDestination:
        return "CopyDestination";
    case rhi::BufferUse::IndirectArgument:
        return "IndirectArgument";
    }
    return "ShaderRead";
}

//======================================================================================================================
const std::string& resourceName(const CompiledFrameDebug& debug, uint32_t index) {
    return debug.resources[index].name;
}

//======================================================================================================================
GraphInspectorUseRow shapeUse(const CompiledFrameDebug& debug, const render::DebugUse& use) {
    GraphInspectorUseRow row;
    row.resource = use.resource;
    row.resourceName = resourceName(debug, use.resource);
    row.version = use.version;
    row.role = use.role;
    if (debug.resources[use.resource].kind == render::GraphResourceKind::Texture) {
        row.rangeText = render::describeRange(use.range);
    }
    return row;
}

//======================================================================================================================
// The GPU time the RHI measured under this pass's own label, or empty when nothing matched -- a
// culled pass never ran and so never appears in `timings`, and this must not invent a number for
// it.
std::optional<double> matchTiming(std::span<const rhi::PassTiming> timings,
                                  const std::string& label) {
    for (const rhi::PassTiming& timing : timings) {
        if (timing.label == label) {
            return timing.gpuMilliseconds;
        }
    }
    return std::nullopt;
}

//======================================================================================================================
GraphInspectorTransitionRow shapeTransition(const CompiledFrameDebug& debug,
                                            const render::DebugTransition& transition) {
    GraphInspectorTransitionRow row;
    row.beforePass = transition.beforePass;
    row.resource = transition.resource;
    row.resourceName = resourceName(debug, transition.resource);
    row.aliasedFrom = transition.aliasedFrom;
    if (transition.kind == render::GraphResourceKind::Texture) {
        row.description = std::format(
            "texture r{} {} {} -> {}", transition.resource, render::describeRange(transition.range),
            textureUseName(transition.textureFrom), textureUseName(transition.textureTo));
    } else {
        row.description =
            std::format("buffer r{} {} -> {}", transition.resource,
                        bufferUseName(transition.bufferFrom), bufferUseName(transition.bufferTo));
    }
    return row;
}

//======================================================================================================================
GraphInspectorTransientRow shapeTransient(const CompiledFrameDebug& debug,
                                          const render::DebugTransient& transient) {
    GraphInspectorTransientRow row;
    row.resource = transient.resource;
    row.resourceName = resourceName(debug, transient.resource);
    row.used = transient.used;
    row.firstPass = transient.firstPass;
    row.lastPass = transient.lastPass;
    row.offset = transient.offset;
    row.size = transient.size;
    row.alignment = transient.alignment;
    row.aliases = transient.aliases;
    return row;
}

} // namespace

//======================================================================================================================
GraphInspectorModel buildGraphInspectorModel(const CompiledFrameRecord& record,
                                             std::span<const rhi::PassTiming> timings) {
    const CompiledFrameDebug& debug = record.debug;
    GraphInspectorModel model;
    model.frameId = record.frameId;
    model.poolingEnabled = debug.poolingEnabled;
    model.memory = debug.memory;

    model.resources.reserve(debug.resources.size());
    for (uint32_t index = 0; index < debug.resources.size(); ++index) {
        const render::DebugResource& resource = debug.resources[index];
        model.resources.push_back({.index = index,
                                   .kind = resource.kind,
                                   .name = resource.name,
                                   .format = resource.format});
    }

    // Declaration order, scheduled and culled passes alike -- the same population the dump's two
    // sections draw from, just not yet split into them.
    model.passes.reserve(debug.passes.size());
    for (uint32_t index = 0; index < debug.passes.size(); ++index) {
        const render::DebugPass& pass = debug.passes[index];
        GraphInspectorPassRow row;
        row.index = index;
        row.kind = pass.kind;
        row.label = pass.label;
        row.cullReason = pass.cullReason;
        row.gpuMilliseconds = matchTiming(timings, pass.label);
        row.uses.reserve(pass.uses.size());
        for (const render::DebugUse& use : pass.uses) {
            row.uses.push_back(shapeUse(debug, use));
        }
        model.passes.push_back(std::move(row));
    }

    model.schedule = debug.schedule.passes;

    model.transitions.reserve(debug.transitions.size());
    for (const render::DebugTransition& transition : debug.transitions) {
        model.transitions.push_back(shapeTransition(debug, transition));
    }

    model.transients.reserve(debug.transients.size());
    for (const render::DebugTransient& transient : debug.transients) {
        model.transients.push_back(shapeTransient(debug, transient));
    }

    return model;
}

} // namespace lmx::app
