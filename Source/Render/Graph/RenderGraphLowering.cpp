//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphLowering.cpp
/// @brief Maps graph vocabulary to stage, access and layout endpoints.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Graph/RenderGraphLowering.h"
#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/FormatInfo.h"
#include "Render/Graph/RenderGraph.h"
#include "Render/Graph/RenderGraphInternal.h"
#include <algorithm>
#include <format>
#include <optional>
#include <utility>
#include <vector>

namespace lmx::render {
namespace {

//======================================================================================================================
rojoRHI::Stage shaderStage(PassKind kind) {
    return kind == PassKind::Raster ? rojoRHI::Stage::VertexShader | rojoRHI::Stage::FragmentShader
                                    : rojoRHI::Stage::ComputeShader;
}
} // namespace

//======================================================================================================================
SyncPoint textureSync(TextureUse use, PassKind kind, bool depthAspect) {
    using enum rojoRHI::Stage;
    using A = rojoRHI::Access;
    using L = rojoRHI::Layout;
    switch (use) {
    case TextureUse::RenderTarget:
        return depthAspect
                   ? SyncPoint{DepthStencil, A::DepthStencilRead | A::DepthStencilWrite,
                               L::DepthStencilWrite}
                   : SyncPoint{ColorAttachment, A::ColorAttachmentRead | A::ColorAttachmentWrite,
                               L::ColorAttachment};
    case TextureUse::ShaderRead:
        return {shaderStage(kind), A::ShaderRead,
                depthAspect ? L::DepthStencilRead : L::ShaderRead};
    case TextureUse::StorageRead:
        return {shaderStage(kind), A::ShaderRead, L::ShaderWrite};
    case TextureUse::StorageWrite:
        return {shaderStage(kind), A::ShaderWrite, L::ShaderWrite};
    case TextureUse::CopySource:
        return {Copy, A::CopyRead, L::CopySrc};
    case TextureUse::CopyDestination:
        return {Copy, A::CopyWrite, L::CopyDst};
    case TextureUse::ExternalRead:
        return {ComputeShader, A::ShaderRead, L::ShaderRead};
    case TextureUse::ExternalWrite:
        return {ComputeShader, A::ShaderWrite, L::ShaderWrite};
    }
    LMX_ASSERT(false, "unknown graph texture use");
    return {None, A::None, L::Undefined};
}

//======================================================================================================================
SyncPoint bufferSync(BufferUse use, PassKind kind) {
    using S = rojoRHI::Stage;
    using A = rojoRHI::Access;
    using L = rojoRHI::Layout;
    switch (use) {
    case BufferUse::ShaderRead:
    case BufferUse::StorageRead:
        return {shaderStage(kind), A::ShaderRead, L::Undefined};
    case BufferUse::StorageWrite:
        return {shaderStage(kind), A::ShaderWrite, L::Undefined};
    case BufferUse::CopySource:
        return {S::Copy, A::CopyRead, L::Undefined};
    case BufferUse::CopyDestination:
        return {S::Copy, A::CopyWrite, L::Undefined};
    case BufferUse::IndirectArgument:
        return {S::IndirectArgs, A::IndirectArgs, L::Undefined};
    }
    LMX_ASSERT(false, "unknown graph buffer use");
    return {S::None, A::None, L::Undefined};
}

namespace {

//======================================================================================================================
bool writes(rojoRHI::Access access) {
    constexpr auto mask = rojoRHI::Access::ShaderWrite | rojoRHI::Access::ColorAttachmentWrite |
                          rojoRHI::Access::DepthStencilWrite | rojoRHI::Access::CopyWrite |
                          rojoRHI::Access::Resolve;
    return (access & mask) != rojoRHI::Access::None;
}

//======================================================================================================================
bool needsDependency(const TextureState& from, const TextureState& to) {
    return from.layout != to.layout || (from.stage != rojoRHI::Stage::None &&
                                        (writes(from.access) || writes(to.access) ||
                                         (to.stage & ~from.stage) != rojoRHI::Stage::None));
}

//======================================================================================================================
TextureState nextFrontier(const TextureState& from, const TextureState& to) {
    if (from.layout != to.layout || writes(from.access) || writes(to.access)) {
        return to;
    }
    return {from.stage | to.stage, from.access | to.access, to.layout};
}

//======================================================================================================================
void appendTextureBarrier(BarriersOp& batch, LoweredTextureBarrier barrier) {
    // Only equal-state adjacent rectangles may merge; no bounding union can bridge a hole.
    if (!batch.barriers.empty()) {
        if (auto* previous = std::get_if<LoweredTextureBarrier>(&batch.barriers.back());
            previous && previous->resource == barrier.resource && previous->from == barrier.from &&
            previous->to == barrier.to && previous->alias == barrier.alias) {
            auto& a = previous->range;
            const auto& b = barrier.range;
            if (a.baseLayer == b.baseLayer && a.layerCount == b.layerCount &&
                a.baseMip + a.mipCount == b.baseMip) {
                a.mipCount += b.mipCount;
                return;
            }
            if (a.baseMip == b.baseMip && a.mipCount == b.mipCount &&
                a.baseLayer + a.layerCount == b.baseLayer) {
                a.layerCount += b.layerCount;
                return;
            }
        }
    }
    batch.barriers.emplace_back(std::move(barrier));
}

//======================================================================================================================
BufferUse physicalBufferUse(PassKind kind, UseRole role) {
    switch (role) {
    case UseRole::Read:
        return kind == PassKind::Compute ? BufferUse::StorageRead : BufferUse::ShaderRead;
    case UseRole::ShaderRead:
        return BufferUse::ShaderRead;
    case UseRole::IndirectArgument:
        return BufferUse::IndirectArgument;
    case UseRole::CopySource:
        return BufferUse::CopySource;
    case UseRole::CopyDestination:
        return BufferUse::CopyDestination;
    case UseRole::Write:
    case UseRole::ColorAttachment:
    case UseRole::DepthAttachment:
        return BufferUse::StorageWrite;
    }
    LMX_ASSERT(false, "unknown buffer declaration role");
    return BufferUse::ShaderRead;
}
} // namespace

//======================================================================================================================
GraphResult<void> RenderGraph::lowerPhysical(LoweredFrame& frame, const AliasPlan& plan) const {
    using S = rojoRHI::Stage;
    using A = rojoRHI::Access;
    using L = rojoRHI::Layout;
    struct Frontier {
        std::vector<TextureState> cells;
        TextureState buffer;
        bool touched = false;
        S aliasStage = S::None;
        A aliasAccess = A::None;
        bool alias = false;
    };
    std::vector<Frontier> frontiers(m_resources.size());
    for (size_t index = 0; index < m_resources.size(); ++index) {
        const auto& resource = m_resources[index];
        if (resource.finalUse && std::ranges::any_of(m_sinks, [index](const Sink& sink) {
                return sink.kind == SinkKind::Present && sink.resource == index;
            })) {
            return graph_detail::fail(
                std::format("texture '{}' requests final use {} and presentation, which require "
                            "different terminal states",
                            resource.name, textureUseName(*resource.finalUse)));
        }
        auto& current = frontiers[index];
        if (resource.kind == ResourceKind::Texture) {
            const auto initial = resource.tracked ? resource.texture.initialLayout : L::Undefined;
            current.cells.resize(static_cast<size_t>(resource.mipLevels) * resource.arrayLayers,
                                 TextureState{.layout = initial});
            for (const auto& known : resource.priorPhysical) {
                LMX_ASSERT(
                    known.range.baseMip < resource.mipLevels && known.range.mipCount > 0 &&
                        known.range.mipCount <= resource.mipLevels - known.range.baseMip &&
                        known.range.baseLayer < resource.arrayLayers &&
                        known.range.layerCount > 0 &&
                        known.range.layerCount <= resource.arrayLayers - known.range.baseLayer,
                    "persistent texture metadata disagrees with its physical state dimensions");
                const auto range = graph_detail::resolveRange(known.range, resource.mipLevels,
                                                              resource.arrayLayers);
                for (uint32_t layer = range.layers.first;
                     layer < (range.layers.first + range.layers.count); ++layer) {
                    for (uint32_t mip = range.mips.first;
                         mip < (range.mips.first + range.mips.count); ++mip) {
                        current.cells[static_cast<size_t>(layer) * resource.mipLevels + mip] =
                            known.state;
                    }
                }
            }
        } else if (resource.priorBufferAccess) {
            auto sync = bufferSync(*resource.priorBufferAccess, PassKind::Compute);
            // A prior nominal shader use does not carry its stage provenance.
            if (sync.stage == S::ComputeShader) {
                sync.stage = S::VertexShader | S::FragmentShader | S::ComputeShader;
            }
            current.buffer = {sync.stage, sync.access, L::Undefined};
        }
    }
    frame.ops.emplace_back(BeginScopeOp{.label = "lmx.frame"});
    const auto& schedule = frame.record.debug.schedule;
    for (uint32_t position = 0; position < schedule.passes.size(); ++position) {
        const uint32_t passIndex = schedule.passes[position];
        const auto& pass = m_passes[passIndex];
        for (const auto& entry : plan.transients) {
            if (!entry.used || entry.firstPosition != position || !entry.aliases) {
                continue;
            }
            auto& target = frontiers[entry.resource];
            target.alias = true;
            if (entry.aliasedFrom) {
                const auto& prior = frontiers[*entry.aliasedFrom];
                target.aliasStage = prior.buffer.stage;
                target.aliasAccess = prior.buffer.access;
                for (const auto& cell : prior.cells) {
                    target.aliasStage = target.aliasStage | cell.stage;
                    target.aliasAccess = target.aliasAccess | cell.access;
                }
            }
            if (target.aliasStage == S::None) {
                target.aliasStage = S::All;
            }
        }
        std::vector<std::vector<std::optional<TextureState>>> textures(m_resources.size());
        std::vector<std::optional<TextureState>> buffers(m_resources.size());
        for (const auto& declaration : pass.declarations) {
            const auto index = declaration.resource;
            const auto& resource = m_resources[index];
            if (resource.kind == ResourceKind::Buffer) {
                const auto sync =
                    bufferSync(physicalBufferUse(pass.kind, declaration.role), pass.kind);
                auto& combined = buffers[index];
                if (!combined) {
                    combined = TextureState{sync.stage, sync.access, L::Undefined};
                } else {
                    combined->stage = combined->stage | sync.stage;
                    combined->access = combined->access | sync.access;
                }
                continue;
            }
            if (textures[index].empty()) {
                textures[index].resize(frontiers[index].cells.size());
            }
            const auto sync = textureSync(graph_detail::textureUseOf(pass.kind, declaration.role),
                                          pass.kind, engine::isDepthFormat(resource.format));
            const TextureState required{sync.stage, sync.access, sync.layout};
            const auto range = graph_detail::resolveRange(declaration.range, resource.mipLevels,
                                                          resource.arrayLayers);
            for (uint32_t layer = range.layers.first;
                 layer < (range.layers.first + range.layers.count); ++layer) {
                for (uint32_t mip = range.mips.first; mip < (range.mips.first + range.mips.count);
                     ++mip) {
                    auto& combined =
                        textures[index][static_cast<size_t>(layer) * resource.mipLevels + mip];
                    if (!combined) {
                        combined = required;
                    } else if (combined->layout != required.layout) {
                        return graph_detail::fail(std::format(
                            "pass '{}' declares incompatible simultaneous layouts for texture '{}'",
                            pass.label, resource.name));
                    } else {
                        combined->stage = combined->stage | required.stage;
                        combined->access = combined->access | required.access;
                    }
                }
            }
        }
        BarriersOp batch;
        for (uint32_t index = 0; index < m_resources.size(); ++index) {
            const auto& resource = m_resources[index];
            auto& current = frontiers[index];
            if (buffers[index]) {
                const auto next = *buffers[index];
                if (current.alias) {
                    batch.barriers.emplace_back(LoweredBufferBarrier{
                        index,
                        {current.aliasStage, current.aliasAccess, L::Undefined},
                        {next.stage, next.access, L::Undefined},
                        true});
                    current.buffer = next;
                    current.alias = false;
                } else {
                    if (needsDependency(current.buffer, next)) {
                        batch.barriers.emplace_back(LoweredBufferBarrier{
                            index,
                            {current.buffer.stage, current.buffer.access, L::Undefined},
                            {next.stage, next.access, L::Undefined},
                            false});
                    }
                    current.buffer = nextFrontier(current.buffer, next);
                }
                current.touched = true;
            }
            if (textures[index].empty()) {
                continue;
            }
            for (uint32_t layer = 0; layer < resource.arrayLayers; ++layer) {
                for (uint32_t mip = 0; mip < resource.mipLevels; ++mip) {
                    const size_t cellIndex = static_cast<size_t>(layer) * resource.mipLevels + mip;
                    if (!textures[index][cellIndex]) {
                        continue;
                    }
                    const auto next = *textures[index][cellIndex];
                    auto& prior = current.cells[cellIndex];
                    const bool discard =
                        current.alias && prior.layout == L::Undefined && prior.stage == S::None;
                    if (discard || needsDependency(prior, next)) {
                        appendTextureBarrier(batch, {index,
                                                     {mip, 1, layer, 1},
                                                     discard ? TextureState{current.aliasStage,
                                                                            A::None, L::Undefined}
                                                             : prior,
                                                     next,
                                                     discard});
                    }
                    prior = nextFrontier(prior, next);
                    current.touched = true;
                }
            }
        }
        if (!batch.barriers.empty()) {
            frame.ops.emplace_back(std::move(batch));
        }
        const RunOp run{passIndex, pass.kind, pass.label};
        switch (pass.kind) {
        case PassKind::Raster:
            frame.ops.emplace_back(beginRenderPassOf(pass));
            frame.ops.emplace_back(run);
            frame.ops.emplace_back(EndRenderPassOp{});
            break;
        case PassKind::Compute:
        case PassKind::Copy:
            frame.ops.emplace_back(BeginScopeOp{pass.label});
            frame.ops.emplace_back(run);
            frame.ops.emplace_back(EndScopeOp{});
            break;
        case PassKind::External:
            frame.ops.emplace_back(run);
            break;
        }
    }
    BarriersOp exports;
    for (uint32_t index = 0; index < m_resources.size(); ++index) {
        const auto& resource = m_resources[index];
        if (resource.kind != ResourceKind::Texture) {
            continue;
        }
        auto& current = frontiers[index];
        const bool present = std::ranges::any_of(m_sinks, [index](const Sink& sink) {
            return sink.kind == SinkKind::Present && sink.resource == index;
        });
        if (resource.finalUse || present) {
            auto sync = resource.finalUse ? textureSync(*resource.finalUse, PassKind::Raster,
                                                        engine::isDepthFormat(resource.format))
                                          : SyncPoint{S::None, A::None, L::Present};
            if (resource.finalUse == TextureUse::ShaderRead) {
                sync.stage = sync.stage | S::ComputeShader;
            }
            const TextureState next{sync.stage, sync.access, sync.layout};
            for (uint32_t layer = 0; layer < resource.arrayLayers; ++layer) {
                for (uint32_t mip = 0; mip < resource.mipLevels; ++mip) {
                    auto& prior =
                        current.cells[static_cast<size_t>(layer) * resource.mipLevels + mip];
                    if (needsDependency(prior, next)) {
                        appendTextureBarrier(exports,
                                             {index, {mip, 1, layer, 1}, prior, next, false});
                    }
                    prior = nextFrontier(prior, next);
                }
            }
            current.touched = true;
        }
        if (resource.tracked && current.touched) {
            for (uint32_t layer = 0; layer < resource.arrayLayers; ++layer) {
                for (uint32_t mip = 0; mip < resource.mipLevels; ++mip) {
                    frame.physicalStates.push_back(
                        {index,
                         {{mip, 1, layer, 1},
                          current.cells[static_cast<size_t>(layer) * resource.mipLevels + mip]}});
                }
            }
        }
    }
    if (!exports.barriers.empty()) {
        frame.ops.emplace_back(std::move(exports));
    }
    frame.ops.emplace_back(EndScopeOp{});
    return {};
}
} // namespace lmx::render
