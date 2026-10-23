//----------------------------------------------------------------------------------------------------------------------
/// @file GraphResources.h
/// @brief Resolves declared graph resources from immutable pass snapshots.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Core/Diagnostics/Assert.h"
#include "Render/Graph/RenderGraph.h"

namespace lmx::render {

/// Returns the texture identity, view and creation metadata for a declared graph version.
inline engine::TextureRef texture(const PassResources& resources, GraphTexture handle) {
    return resources.texture(handle);
}
/// Returns the buffer identity for a declared graph version.
inline rojoRHI::Buffer buffer(const PassResources& resources, GraphBuffer handle) {
    return resources.buffer(handle);
}
/// Returns the sampled view for a declared graph version; an absent view is misuse.
inline rojoRHI::ViewIndex view(const PassResources& resources, GraphTexture handle) {
    return resources.view(handle);
}
/// Returns a created storage view for the declared version and mip.
inline rojoRHI::ViewIndex storageView(const PassResources& resources, GraphTexture handle,
                                      uint32_t mip) {
    return resources.storageView(handle, mip);
}
/// Returns the cached GPU address at an in-bounds byte offset of the declared buffer.
inline rojoRHI::Addr address(const PassResources& resources, GraphBuffer handle,
                             uint64_t offset = 0) {
    return resources.address(handle, offset);
}

} // namespace lmx::render
