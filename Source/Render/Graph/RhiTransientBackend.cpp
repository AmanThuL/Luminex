//----------------------------------------------------------------------------------------------------------------------
/// @file RhiTransientBackend.cpp
/// @brief Implements placed GPU objects with retained views and addresses.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Graph/RhiTransientBackend.h"
#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/CaptureSchema.h"
#include "Engine/Gpu/FormatInfo.h"
#include "Engine/Gpu/GpuContext.h"
#include "Engine/Gpu/GpuError.h"
#include <algorithm>
#include <format>
#include <utility>

namespace lmx::render {
namespace {

//======================================================================================================================
rojoRHI::TextureDesc textureDescOf(const TransientTextureDesc& desc, std::string_view label) {
    LMX_ASSERT(desc.kind == rojoRHI::TextureType::Tex2D || desc.kind == rojoRHI::TextureType::Cube,
               "transient textures must be 2D textures or cubes");
    rojoRHI::TextureUsage usage = rojoRHI::TextureUsage::None;
    if (desc.sampled) {
        usage = usage | rojoRHI::TextureUsage::Sampled;
    }
    if (desc.storageRead || desc.storageWrite) {
        usage = usage | rojoRHI::TextureUsage::Storage;
    }
    if (desc.renderTarget) {
        usage = usage | (engine::isDepthFormat(desc.format) ? rojoRHI::TextureUsage::DepthStencil
                                                            : rojoRHI::TextureUsage::RenderTarget);
    }
    return {.type = desc.kind,
            .format = desc.format,
            .extent = {desc.width, desc.height, 1},
            .mips = desc.mipLevels,
            .layers = desc.kind == rojoRHI::TextureType::Cube ? 6u : 1u,
            .usage = usage,
            .label = label};
}

//======================================================================================================================
rojoRHI::BufferDesc bufferDescOf(const TransientBufferDesc& desc, bool indirectArgs,
                                 std::string_view label) {
    rojoRHI::BufferUsage usage =
        desc.storageWrite ? rojoRHI::BufferUsage::ShaderWrite : rojoRHI::BufferUsage::None;
    if (indirectArgs) {
        usage = usage | rojoRHI::BufferUsage::IndirectArgs;
    }
    return {.size = desc.size, .memory = rojoRHI::Memory::Gpu, .usage = usage, .label = label};
}
} // namespace

//======================================================================================================================
RhiTransientBackend::~RhiTransientBackend() {
    while (!m_heaps.empty()) {
        const auto id = m_heaps.back().id;
        releasePlaced(id);
        destroyHeap(id);
    }
}

//======================================================================================================================
uint64_t RhiTransientBackend::frameNumber() const {
    return m_gpu.frameNumber();
}

//======================================================================================================================
TransientFootprint RhiTransientBackend::footprint(const TransientTextureDesc& desc) const {
    const auto result = m_gpu.device().placementRequirements(textureDescOf(desc, {}));
    return {result.size, result.alignment};
}

//======================================================================================================================
TransientFootprint RhiTransientBackend::footprint(const TransientBufferDesc& desc,
                                                  bool indirectArgs) const {
    const auto result = m_gpu.device().placementRequirements(bufferDescOf(desc, indirectArgs, {}));
    return {result.size, result.alignment};
}

//======================================================================================================================
lmx::Result<TransientHeapId> RhiTransientBackend::createHeap(uint64_t bytes,
                                                             std::string_view label) {
    auto heap = m_gpu.device().createHeap({bytes, rojoRHI::Memory::Gpu, label});
    if (!heap) {
        return std::unexpected(engine::gpuError(heap.error()));
    }
    const TransientHeapId id{m_nextHeap++};
    m_heaps.push_back({.id = id, .heap = *heap});
    return id;
}

//======================================================================================================================
void RhiTransientBackend::destroyHeap(TransientHeapId heap) {
    auto& entry = entryOf(heap);
    LMX_ASSERT(entry.textures.empty() && entry.buffers.empty(),
               std::format("RhiTransientBackend::destroyHeap: heap {} still holds placed resources "
                           "-- releasePlaced() it first",
                           heap.value));
    m_gpu.device().destroy(entry.heap);
    std::erase_if(m_heaps, [heap](const auto& candidate) { return candidate.id == heap; });
}

//======================================================================================================================
lmx::Result<engine::TextureRef> RhiTransientBackend::placeTexture(TransientHeapId heap,
                                                                  uint64_t offset,
                                                                  const TransientTextureDesc& desc,
                                                                  std::string_view label) {
    const auto native = textureDescOf(desc, label);
    auto texture = m_gpu.device().createTexture(native, {}, {entryOf(heap).heap, offset});
    if (!texture) {
        return std::unexpected(engine::gpuError(texture.error()));
    }
    TextureEntry entry{.ref = {*texture,
                               engine::kNoView,
                               {desc.width, desc.height},
                               desc.format,
                               native.mips,
                               native.layers,
                               rojoRHI::Layout::Undefined}};
    const auto cleanup = [&] {
        for (auto view : entry.storage) {
            m_gpu.device().destroy(view);
        }
        if (entry.ref.view != engine::kNoView) {
            m_gpu.device().destroy(entry.ref.view);
        }
        m_gpu.device().destroy(*texture);
    };
    if (desc.sampled) {
        auto view =
            m_gpu.device().createView(*texture, {.type = native.type,
                                                 .aspect = engine::isDepthFormat(desc.format)
                                                               ? rojoRHI::TextureAspect::Depth
                                                               : rojoRHI::TextureAspect::Color});
        if (!view) {
            cleanup();
            return std::unexpected(engine::gpuError(view.error()));
        }
        entry.ref.view = *view;
    }
    if (desc.storageRead || desc.storageWrite) {
        for (uint32_t mip = 0; mip < native.mips; ++mip) {
            auto view =
                m_gpu.device().createView(*texture, {.type = native.type,
                                                     .range = {mip, 1, 0, rojoRHI::kAllLayers},
                                                     .usage = rojoRHI::TextureUsage::Storage});
            if (!view) {
                cleanup();
                return std::unexpected(engine::gpuError(view.error()));
            }
            entry.storage.push_back(*view);
        }
    }
    const auto ref = entry.ref;
    entryOf(heap).textures.push_back(std::move(entry));
    engine::registerCaptureResource(*texture, native);
    return ref;
}

//======================================================================================================================
lmx::Result<rojoRHI::Buffer> RhiTransientBackend::placeBuffer(TransientHeapId heap, uint64_t offset,
                                                              const TransientBufferDesc& desc,
                                                              std::string_view label,
                                                              bool indirectArgs) {
    const auto native = bufferDescOf(desc, indirectArgs, label);
    auto buffer = m_gpu.device().createBuffer(native, {}, {entryOf(heap).heap, offset});
    if (!buffer) {
        return std::unexpected(engine::gpuError(buffer.error()));
    }
    entryOf(heap).buffers.push_back({*buffer, m_gpu.device().address(*buffer)});
    engine::registerCaptureResource(*buffer, native);
    return *buffer;
}

//======================================================================================================================
rojoRHI::ViewIndex RhiTransientBackend::storageView(TransientHeapId heap, rojoRHI::Texture texture,
                                                    uint32_t mip) const {
    const auto& textures = entryOf(heap).textures;
    const auto found = std::ranges::find_if(
        textures, [texture](const auto& entry) { return entry.ref.texture == texture; });
    LMX_ASSERT(found != textures.end() && mip < found->storage.size(),
               "placed texture has no storage view at the requested mip");
    return found->storage[mip];
}

//======================================================================================================================
rojoRHI::Addr RhiTransientBackend::address(TransientHeapId heap, rojoRHI::Buffer buffer) const {
    const auto& buffers = entryOf(heap).buffers;
    const auto found = std::ranges::find(buffers, buffer, &BufferEntry::buffer);
    LMX_ASSERT(found != buffers.end(), "placed buffer is not held by this heap");
    return found->address;
}

//======================================================================================================================
void RhiTransientBackend::releasePlaced(TransientHeapId heap) {
    auto& entry = entryOf(heap);
    for (const auto& texture : entry.textures) {
        engine::unregisterCaptureResource(texture.ref.texture);
        for (auto view : texture.storage) {
            m_gpu.device().destroy(view);
        }
        if (texture.ref.view != engine::kNoView) {
            m_gpu.device().destroy(texture.ref.view);
        }
        m_gpu.device().destroy(texture.ref.texture);
    }
    for (const auto& buffer : entry.buffers) {
        engine::unregisterCaptureResource(buffer.buffer);
        m_gpu.device().destroy(buffer.buffer);
    }
    entry.textures.clear();
    entry.buffers.clear();
}

//======================================================================================================================
RhiTransientBackend::HeapEntry& RhiTransientBackend::entryOf(TransientHeapId heap) {
    return const_cast<HeapEntry&>(std::as_const(*this).entryOf(heap));
}

//======================================================================================================================
const RhiTransientBackend::HeapEntry& RhiTransientBackend::entryOf(TransientHeapId heap) const {
    const auto found = std::ranges::find(m_heaps, heap, &HeapEntry::id);
    LMX_ASSERT(
        found != m_heaps.end(),
        std::format("RhiTransientBackend: heap {} is not one this backend holds", heap.value));
    return *found;
}
} // namespace lmx::render
