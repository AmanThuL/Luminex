//----------------------------------------------------------------------------------------------------------------------
/// @file RendererStandIns.h
/// @brief Declares neutral frame inputs while temporal and exposure stages are unavailable.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/Renderer/Renderer.h"

namespace lmx::render {

// Allocates output-sized depth and the constant applied/previous exposure pair.
lmx::Result<RendererStandIns> createRendererStandIns(rojoRHI::Device& device, uint32_t width,
                                                     uint32_t height);

} // namespace lmx::render
