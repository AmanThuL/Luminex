//----------------------------------------------------------------------------------------------------------------------
/// @file GraphResources.h
/// @brief Unwraps declared graph resources with the graph error preserved on failure.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Core/Diagnostics/Assert.h"
#include "Render/Graph/RenderGraph.h"

namespace lmx::render {

// The graph owns the lookup contract; keep its diagnostic when a callback names an invalid handle.
inline rojoRHI::Texture& texture(const PassResources& resources, GraphTexture handle) {
    auto result = resources.texture(handle);
    LMX_ASSERT(result.has_value(), result.error().message);
    return **result;
}
inline rojoRHI::Buffer& buffer(const PassResources& resources, GraphBuffer handle) {
    auto result = resources.buffer(handle);
    LMX_ASSERT(result.has_value(), result.error().message);
    return **result;
}

} // namespace lmx::render
