//----------------------------------------------------------------------------------------------------------------------
/// @file RendererStandIns.h
/// @brief Declares neutral depth while the temporal stage is unavailable.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/Renderer/Renderer.h"

namespace lmx::render {

// Allocates output-sized depth while the temporal owner is unavailable.
lmx::Result<RendererStandIns> createRendererStandIns(rojoRHI::Device& device, uint32_t width,
                                                     uint32_t height);

} // namespace lmx::render
