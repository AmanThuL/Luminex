//----------------------------------------------------------------------------------------------------------------------
/// @file LightDebugStage.h
/// @brief Declares post-display froxel diagnostics over the scene's actual depth and light lists.
//----------------------------------------------------------------------------------------------------------------------
#pragma once
#include "Engine/Lights/LocalLight.h"
#include "Render/Graph/RenderGraph.h"
#include "Render/Passes/LocalLights/LightClusters.h"

namespace lmx::render {
/// Borrowed graph inputs; injectable list handles let fixtures test missing and truncated entries.
struct LightDebugInputs {
    engine::LightDebugView mode = engine::LightDebugView::Off; ///< Non-Off diagnostic to draw.
    GraphTexture depth;   ///< Actual source-space reversed-Z scene depth; zero denotes sky.
    GraphTexture display; ///< Display-transformed source, used to dim the overflow background.
    GraphTexture output;  ///< Separate opaque BGRA8 display target to write.
    GraphBuffer lights;   ///< Addressable light rows, including tombstones.
    GraphBuffer grid;     ///< Ascending-list ranges indexed by source pixel and loaded depth.
    GraphBuffer indices;  ///< Ascending light row indices.
    LightClusterParams clusters; ///< Same active extent and slice table as this frame's clustering.
    glm::mat4 inverseViewProjection{1.0f}; ///< Inverse of this frame's jittered raster transform.
    uint32_t outputWidth = 0; ///< Full display width; source lookup uses integer nearest expansion.
    uint32_t outputHeight = 0; ///< Full display height.
};

/// Owns the lazily requested diagnostic shader; leaves HDR and temporal history untouched.
class LightDebugStage {
public:
    /// Loads the ordinary fast-math diagnostic shader and creates its opaque display pipeline.
    static rojoRHI::Result<std::unique_ptr<LightDebugStage>> create(rojoRHI::Device& device);
    /// Declares one post-display raster pass and returns the produced output version.
    GraphTexture declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                         const LightDebugInputs& inputs);

private:
    LightDebugStage() = default;

    std::unique_ptr<rojoRHI::ShaderLibrary> m_library;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_pipeline;
};
} // namespace lmx::render
