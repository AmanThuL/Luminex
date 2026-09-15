//----------------------------------------------------------------------------------------------------------------------
/// @file SceneStage.h
/// @brief Declares the concrete scene geometry and sky draw stage.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "RHI/RHI.h"
#include "Render/RenderGraph.h"
#include "Render/SceneView.h"
#include "Render/TemporalHistory.h"

#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <vector>

namespace lmx::render {

/// Frame values and Renderer-owned resources borrowed by the scene and sky pass.
/// Graph handles belong to this frame; every pointer is non-null and survives graph execution.
struct SceneStageInputs {
    std::vector<GraphBuffer> sceneBuffers; ///< Five read-only scene pool/table imports.
    GraphTexture sceneColor;               ///< Scene-linear color attachment version to write.
    GraphTexture sceneDepth;               ///< Reversed D32Float depth attachment version to write.
    GraphTexture shadowRead;               ///< Written shadow-map version to sample.
    GraphTexture motion;     ///< Motion attachment; required only with temporal enabled.
    GraphTexture reactive;   ///< Reactive attachment; required only with temporal enabled.
    GraphBuffer exposure;    ///< Applied/previous pair; sampled by auto-exposure shading only.
    CameraFrameState camera; ///< Current camera, with jittered and unjittered matrices.
    CameraFrameState previousCamera; ///< Previous declared camera, or current on the first frame.
    glm::vec3 eyePosition;           ///< World-space eye position.
    glm::mat4 shadowTransform;       ///< World-to-shadow-texture transform.
    FrameExtents extents;            ///< Active render rectangle and backing output extent.
    glm::vec2 jitterPixels;          ///< Raster jitter in active render pixels.
    std::array<float, 4> clearColor; ///< Authored sRGB color; alpha is passed through.
    float timeSeconds;               ///< Frame clock uploaded to PassUniforms.time.
    bool temporalEnabled;            ///< Selects motion pipelines and extra attachments.
    rhi::Texture* whiteTexture;      ///< Neutral material-factor fallback.
    rhi::Texture* flatNormalTexture; ///< Tangent-space positive-Z normal fallback.
    rhi::Texture* blackCubeTexture;  ///< Zero-radiance environment fallback.
    rhi::Texture* zeroDfgTexture;    ///< Zero split-sum reconstruction fallback.
    rhi::Sampler* linearSampler;     ///< Material sampler.
    rhi::Sampler* shadowSampler;     ///< Reversed-depth comparison sampler.
    rhi::Sampler* iblSampler;        ///< Clamped environment sampler.
};

/// Owns scene and sky pipeline variants; Renderer keeps it alive through graph execution.
class SceneStage {
public:
    /// Creates all variants for the scene color format; propagates GPU creation errors.
    static rhi::Result<std::unique_ptr<SceneStage>> create(rhi::Device& device,
                                                           rhi::Format sceneColorFormat);

    /// Registers draw and shared table layouts independently to retain the capture schema's
    /// serialized ordering.
    static void registerSceneTableLayoutsForCapture();
    /// Registers pass and sky layouts idempotently, without a device.
    static void registerPassLayoutsForCapture();

    /// Declares scene geometry followed by sky and returns the written color version. Copies
    /// frame values; borrows commands and view resources through execution on those commands.
    GraphTexture declare(RenderGraph& graph, rhi::CommandList& commands, const SceneView& view,
                         const SceneStageInputs& inputs);

private:
    SceneStage() = default;
    std::unique_ptr<rhi::ShaderLibrary> m_sceneLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_sceneAutoLibrary;
    std::array<std::unique_ptr<rhi::ShaderLibrary>, 2> m_maskSceneLibraries;
    std::array<std::unique_ptr<rhi::GraphicsPipeline>, 16> m_maskScenePipelines;
    std::unique_ptr<rhi::ShaderLibrary> m_skyLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_skyAutoLibrary;
    std::unique_ptr<rhi::GraphicsPipeline> m_scenePipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_sceneWireframePipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_scenePipelineAuto;
    std::unique_ptr<rhi::GraphicsPipeline> m_sceneWireframePipelineAuto;
    std::unique_ptr<rhi::GraphicsPipeline> m_skyPipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_skyPipelineAuto;
    std::unique_ptr<rhi::GraphicsPipeline> m_scenePipelineMotion;
    std::unique_ptr<rhi::GraphicsPipeline> m_sceneWireframePipelineMotion;
    std::unique_ptr<rhi::GraphicsPipeline> m_scenePipelineAutoMotion;
    std::unique_ptr<rhi::GraphicsPipeline> m_sceneWireframePipelineAutoMotion;
    std::unique_ptr<rhi::GraphicsPipeline> m_skyPipelineMotion;
    std::unique_ptr<rhi::GraphicsPipeline> m_skyPipelineAutoMotion;
};

} // namespace lmx::render
