//----------------------------------------------------------------------------------------------------------------------
/// @file SceneStage.h
/// @brief Declares the concrete scene geometry and sky draw stage.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/Graph/RenderGraph.h"
#include "Render/Passes/Scene/DrawSubmission.h"
#include "Render/Passes/Temporal/TemporalHistory.h"
#include "Render/Renderer/SceneView.h"
#include <rojoRHI/RHI.h>

#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace lmx::render {
namespace scene_detail {
struct PassUniforms;
struct LocalLightParams;
} // namespace scene_detail

/// Frame values and Renderer-owned resources borrowed by the scene and sky pass.
/// Graph handles belong to this frame; every pointer is non-null and survives graph execution.
struct SceneStageInputs {
    DrawList draws;                        ///< Prepared commands and paced buffers for this view.
    GraphBuffer drawRows;                  ///< Read-only visible-row import.
    GraphBuffer drawArguments;             ///< Indirect-argument import.
    std::vector<GraphBuffer> sceneBuffers; ///< Five read-only scene pool/table imports.
    /// Read-only `lmx.scene.lights` import, present exactly when the frame has a live local light.
    /// Absent leaves the pass declaring nothing new and binding the stage's own fallback rows.
    std::optional<GraphBuffer> lights;
    std::optional<GraphBuffer> lightGrid; ///< Cluster records when clustered shading is selected.
    std::optional<GraphBuffer> lightIndices; ///< Ascending light row indices paired with lightGrid.
    GraphTexture sceneColor;                 ///< Scene-linear color attachment version to write.
    GraphTexture sceneDepth; ///< Reversed D32Float depth attachment version to write.
    GraphTexture shadowRead; ///< Written shadow-map version to sample.
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
    rojoRHI::Texture* whiteTexture;  ///< Neutral material-factor fallback.
    rojoRHI::Texture* flatNormalTexture; ///< Tangent-space positive-Z normal fallback.
    rojoRHI::Texture* blackCubeTexture;  ///< Zero-radiance environment fallback.
    rojoRHI::Texture* zeroDfgTexture;    ///< Zero split-sum reconstruction fallback.
    rojoRHI::Sampler* linearSampler;     ///< Material sampler.
    rojoRHI::Sampler* shadowSampler;     ///< Reversed-depth comparison sampler.
    rojoRHI::Sampler* iblSampler;        ///< Clamped environment sampler.
};

/// Owns scene and sky pipeline variants; Renderer keeps it alive through graph execution.
class SceneStage {
public:
    /// Creates all variants for the scene color format; propagates GPU creation errors.
    static rojoRHI::Result<std::unique_ptr<SceneStage>> create(rojoRHI::Device& device,
                                                               rojoRHI::Format sceneColorFormat);

    /// Registers draw and shared table layouts independently to retain the capture schema's
    /// serialized ordering.
    static void registerSceneTableLayoutsForCapture();
    /// Registers pass and sky layouts idempotently, without a device.
    static void registerPassLayoutsForCapture();

    /// Declares scene geometry followed by sky and returns the written color version. Copies
    /// frame values; borrows commands and view resources through execution on those commands.
    GraphTexture declare(RenderGraph& graph, rojoRHI::CommandList& commands, const SceneView& view,
                         const SceneStageInputs& inputs);

private:
    SceneStage() = default;

    void draw(rojoRHI::CommandList& commands, const SceneView& view, const SceneStageInputs& inputs,
              const scene_detail::PassUniforms& passUniforms,
              const scene_detail::LocalLightParams& localLightParams, GraphTexture shadowRead,
              GraphBuffer exposureCurrent, bool temporalEnabled,
              const CameraFrameState& cameraState, const CameraFrameState& previousCamera,
              glm::vec2 jitterNdc, const PassResources& resources);

    // Replay needs valid bindings even for unused slots. Immutable fallbacks stay outside the
    // graph so zero-light declarations retain their resource and pass topology.
    std::unique_ptr<rojoRHI::Buffer> m_fallbackLightRows;
    std::unique_ptr<rojoRHI::Buffer> m_fallbackClusterGrid;
    std::unique_ptr<rojoRHI::Buffer> m_fallbackClusterIndices;
    std::unique_ptr<rojoRHI::ShaderLibrary> m_sceneLibrary;
    std::unique_ptr<rojoRHI::ShaderLibrary> m_sceneAutoLibrary;
    std::array<std::unique_ptr<rojoRHI::ShaderLibrary>, 2> m_maskSceneLibraries;
    std::array<std::unique_ptr<rojoRHI::GraphicsPipeline>, 16> m_maskScenePipelines;
    std::unique_ptr<rojoRHI::ShaderLibrary> m_skyLibrary;
    std::unique_ptr<rojoRHI::ShaderLibrary> m_skyAutoLibrary;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_scenePipeline;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_sceneWireframePipeline;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_scenePipelineAuto;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_sceneWireframePipelineAuto;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_skyPipeline;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_skyPipelineAuto;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_scenePipelineMotion;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_sceneWireframePipelineMotion;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_scenePipelineAutoMotion;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_sceneWireframePipelineAutoMotion;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_skyPipelineMotion;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_skyPipelineAutoMotion;
};

} // namespace lmx::render
