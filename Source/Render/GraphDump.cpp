//----------------------------------------------------------------------------------------------------------------------
/// @file GraphDump.cpp
/// @brief Implements the deterministic text dump of a compiled render-graph frame.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/GraphDump.h"

#include "Core/Log.h"

#include <cstdlib>
#include <format>
#include <fstream>
#include <string>
#include <string_view>

namespace lmx::render {
namespace {

//======================================================================================================================
std::string_view passKindName(PassKind kind) {
    switch (kind) {
    case PassKind::Raster:
        return "raster";
    case PassKind::Compute:
        return "compute";
    case PassKind::Copy:
        return "copy";
    case PassKind::External:
        return "external";
    }
    return "raster";
}

//======================================================================================================================
std::string_view sinkKindName(SinkKind kind) {
    switch (kind) {
    case SinkKind::Export:
        return "export";
    case SinkKind::Present:
        return "present";
    case SinkKind::Readback:
        return "readback";
    }
    return "export";
}

//======================================================================================================================
// Kebab-case rather than the enumerator, because a reason is read as a phrase in a line of prose
// while a format or a use is read as the API name it came from.
std::string_view cullReasonName(CullReason reason) {
    switch (reason) {
    case CullReason::ProducesNothing:
        return "produces-nothing";
    case CullReason::NoSinkReachesIt:
        return "no-sink-reaches-it";
    }
    return "no-sink-reaches-it";
}

//======================================================================================================================
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
    case rhi::TextureUse::ExternalRead:
        return "ExternalRead";
    case rhi::TextureUse::ExternalWrite:
        return "ExternalWrite";
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
// One pass's header line plus its uses, shared by the scheduled and the culled sections so the two
// describe a pass identically and only the reason distinguishes them.
void appendPass(std::string& out, const CompiledFrameDebug& debug, uint32_t index,
                bool withReason) {
    const DebugPass& pass = debug.passes[index];
    out += std::format("  p{} {} \"{}\"", index, passKindName(pass.kind), pass.label);
    if (withReason && pass.cullReason) {
        out += std::format(" {}", cullReasonName(*pass.cullReason));
    }
    out += '\n';
    // Colour attachments are numbered from the second one on: the primary keeps the unadorned name
    // it has always printed, and each extra says which attachment index it binds to.
    uint32_t colorAttachments = 0;
    for (const DebugUse& use : pass.uses) {
        std::string role{roleName(use.role)};
        if (use.role == UseRole::ColorAttachment) {
            if (colorAttachments > 0) {
                role += std::format("[{}]", colorAttachments);
            }
            ++colorAttachments;
        }
        out += std::format("    {} r{} v{}", role, use.resource, use.version);
        if (debug.resources[use.resource].kind == GraphResourceKind::Texture) {
            out += std::format(" {}", describeRange(use.range));
        }
        out += '\n';
    }
    // Only a pass that asked for a sub-rectangle says anything: a pass rendering the whole
    // attachment prints exactly the lines it always has.
    if (pass.renderAreaWidth != 0 || pass.renderAreaHeight != 0) {
        out += std::format("    render area {}x{}\n", pass.renderAreaWidth, pass.renderAreaHeight);
    }
}

} // namespace

//======================================================================================================================
std::string dumpCompiledFrame(const CompiledFrameRecord& record) {
    const CompiledFrameDebug& debug = record.debug;
    std::string out = std::format("render-graph frame {}\n", record.frameId);

    // Every section is written even when it is empty, so the shape of a dump does not depend on
    // what the frame happened to contain.
    out += "resources\n";
    for (uint32_t index = 0; index < debug.resources.size(); ++index) {
        const DebugResource& resource = debug.resources[index];
        if (resource.kind == GraphResourceKind::Texture) {
            out += std::format("  r{} texture \"{}\" {}\n", index, resource.name,
                               formatName(resource.format));
        } else {
            out += std::format("  r{} buffer \"{}\"\n", index, resource.name);
        }
    }

    out += "sinks\n";
    for (const DebugSink& sink : debug.sinks) {
        out += std::format("  {} r{} v{}\n", sinkKindName(sink.kind), sink.resource, sink.version);
    }

    out += "passes\n";
    for (const uint32_t index : debug.schedule.passes) {
        appendPass(out, debug, index, false);
    }

    out += "culled\n";
    for (uint32_t index = 0; index < debug.passes.size(); ++index) {
        if (debug.passes[index].cullReason) {
            appendPass(out, debug, index, true);
        }
    }

    out += "transitions\n";
    for (const DebugTransition& transition : debug.transitions) {
        if (transition.kind == GraphResourceKind::Texture) {
            out += std::format("  before p{} texture r{} {} {} -> {}", transition.beforePass,
                               transition.resource, describeRange(transition.range),
                               textureUseName(transition.textureFrom),
                               textureUseName(transition.textureTo));
        } else {
            out += std::format("  before p{} buffer r{} {} -> {}", transition.beforePass,
                               transition.resource, bufferUseName(transition.bufferFrom),
                               bufferUseName(transition.bufferTo));
        }
        // Present only on a reuse boundary, so an ordinary read-after-write line reads exactly as
        // it did before transients existed.
        if (transition.aliasedFrom) {
            out += std::format(" alias-of r{}", *transition.aliasedFrom);
        }
        out += '\n';
    }

    // A transient the frame did not need has no lifetime and no bytes, and says so rather than
    // reporting an interval and an offset that mean nothing.
    out += "transients\n";
    for (const DebugTransient& transient : debug.transients) {
        if (!transient.used) {
            out += std::format("  r{} unused\n", transient.resource);
            continue;
        }
        out +=
            std::format("  r{} passes p{}..p{} offset {} size {} align {}{}\n", transient.resource,
                        transient.firstPass, transient.lastPass, transient.offset, transient.size,
                        transient.alignment, transient.aliases ? " aliased" : "");
    }

    out += "memory\n";
    out += std::format("  pooling {} requested {} high-water {} saved {}\n",
                       debug.poolingEnabled ? "on" : "off", debug.memory.requested,
                       debug.memory.highWater, debug.memory.aliasSavings);
    return out;
}

//======================================================================================================================
void dumpCompiledFrameIfRequested(const CompiledFrameRecord& record) {
    // Read once and latched: the variable cannot change within a run, and the latch is what makes
    // this the *first* frame's dump rather than whichever frame the process happened to end on.
    static const char* const path = std::getenv("LMX_GRAPH_DUMP");
    static bool written = false;
    if (path == nullptr || written) {
        return;
    }
    written = true;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        LMX_LOG_ERROR("LMX_GRAPH_DUMP: cannot open '{}' for writing", path);
        return;
    }
    file << dumpCompiledFrame(record);
    LMX_LOG_INFO("render-graph frame {} dumped to '{}'", record.frameId, path);
}

} // namespace lmx::render
