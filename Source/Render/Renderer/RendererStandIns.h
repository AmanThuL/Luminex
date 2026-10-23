//----------------------------------------------------------------------------------------------------------------------
/// @file RendererStandIns.h
/// @brief Declares neutral frame inputs and the scene clear used before stage construction.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/Renderer/Renderer.h"

namespace lmx::render {

// Declares a scene-linear colour clear and reversed-depth clear, with no draw commands.
GraphTexture declareSceneClear(RenderGraph& graph, GraphTexture sceneColor, GraphTexture depth,
                               const float clearColor[4]);

// Allocates output-sized depth and the constant applied/previous exposure pair.
lmx::Result<RendererStandIns> createRendererStandIns(rojoRHI::Device& device, uint32_t width,
                                                     uint32_t height);

} // namespace lmx::render
