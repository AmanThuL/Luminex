//----------------------------------------------------------------------------------------------------------------------
/// @file SceneStagePipelines.cpp
/// @brief Creates scene and sky pipeline variants and immutable light fallbacks.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Scene/SceneStage.h"

#include "Render/Passes/LocalLights/LightClusters.h"
#include "Render/Passes/Temporal/TemporalResolve.h"

#include <format>
#include <utility>

namespace lmx::render {

//======================================================================================================================
rojoRHI::Result<std::unique_ptr<SceneStage>> SceneStage::create(rojoRHI::Device& device,
                                                                rojoRHI::Format sceneColorFormat) {
    std::unique_ptr<SceneStage> self(new SceneStage);

    // Minimal immutable storage keeps every declared slot valid when its selection path is unused.
    {
        const engine::LightRow freeRow;
        auto rows = device.createBuffer(
            {.size = sizeof(freeRow), .label = "lmx.render.localLightFallbackRows"}, &freeRow);
        if (!rows) {
            return std::unexpected(rows.error());
        }
        self->m_fallbackLightRows = std::move(*rows);

        const ClusterRecord emptyRecord;
        auto grid = device.createBuffer(
            {.size = sizeof(emptyRecord), .label = "lmx.render.localLightFallbackGrid"},
            &emptyRecord);
        if (!grid) {
            return std::unexpected(grid.error());
        }
        self->m_fallbackClusterGrid = std::move(*grid);

        constexpr uint32_t kEmptyIndex = 0;
        auto indices = device.createBuffer(
            {.size = sizeof(kEmptyIndex), .label = "lmx.render.localLightFallbackIndices"},
            &kEmptyIndex);
        if (!indices) {
            return std::unexpected(indices.error());
        }
        self->m_fallbackClusterIndices = std::move(*indices);
    }

    if (auto library = device.loadShaderLibrary("Shaders/ScenePass"); library) {
        self->m_sceneLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    if (auto library = device.loadShaderLibrary("Shaders/ScenePassAuto"); library) {
        self->m_sceneAutoLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    if (auto library = device.loadShaderLibrary("Shaders/Sky"); library) {
        self->m_skyLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    if (auto library = device.loadShaderLibrary("Shaders/SkyAuto"); library) {
        self->m_skyAutoLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    const auto makeScenePipeline = [&](rojoRHI::ShaderLibrary* library, rojoRHI::FillMode fill,
                                       const char* label) {
        return device.createGraphicsPipeline({.library = library,
                                              .vertexEntry = "vertexMain",
                                              .fragmentEntry = "fragmentMain",
                                              .colorFormat = sceneColorFormat,
                                              .depthFormat = rojoRHI::Format::D32Float,
                                              .depthTestEnable = true,
                                              .depthWriteEnable = true,
                                              .fillMode = fill,
                                              .cullMode = rojoRHI::CullMode::Back,
                                              // Reversed depth: the pass clears to 0 and the
                                              // nearer fragment is the larger one.
                                              .depthCompare = rojoRHI::DepthCompare::Greater,
                                              .label = label});
    };
    // The motion twin of makeScenePipeline: the motion entry points, and the motion target as a
    // second colour attachment. Compiled up front rather than on the frame temporal is first
    // enabled, because a pipeline compile in the middle of a frame is a hitch a toggle should not
    // cost.
    const auto makeSceneMotionPipeline = [&](rojoRHI::ShaderLibrary* library,
                                             rojoRHI::FillMode fill, const char* label) {
        return device.createGraphicsPipeline(
            {.library = library,
             .vertexEntry = "vertexMainMotion",
             .fragmentEntry = "fragmentMainMotion",
             .colorFormat = sceneColorFormat,
             .extraColorFormats = {kMotionFormat, kReactiveFormat, rojoRHI::Format::Unknown},
             .extraColorCount = 2,
             .depthFormat = rojoRHI::Format::D32Float,
             .depthTestEnable = true,
             .depthWriteEnable = true,
             .fillMode = fill,
             .cullMode = rojoRHI::CullMode::Back,
             .depthCompare = rojoRHI::DepthCompare::Greater,
             .label = label});
    };
    if (auto pipeline = makeScenePipeline(self->m_sceneLibrary.get(), rojoRHI::FillMode::Solid,
                                          "lmx.render.scenePipeline");
        pipeline) {
        self->m_scenePipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    // Fill mode is baked into Metal pipeline state; compile both variants once.
    if (auto pipeline = makeScenePipeline(self->m_sceneLibrary.get(), rojoRHI::FillMode::Wireframe,
                                          "lmx.render.sceneWireframePipeline");
        pipeline) {
        self->m_sceneWireframePipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    // ScenePassAuto.slang's compiled twin, bound instead of the pipelines above whenever
    // auto-exposure is on (spec 9) -- see ScenePassAuto.slang's header for why this is a separate
    // pipeline rather than a branch inside the ones above.
    if (auto pipeline = makeScenePipeline(self->m_sceneAutoLibrary.get(), rojoRHI::FillMode::Solid,
                                          "lmx.render.scenePipelineAuto");
        pipeline) {
        self->m_scenePipelineAuto = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline =
            makeScenePipeline(self->m_sceneAutoLibrary.get(), rojoRHI::FillMode::Wireframe,
                              "lmx.render.sceneWireframePipelineAuto");
        pipeline) {
        self->m_sceneWireframePipelineAuto = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }

    if (auto pipeline = makeSceneMotionPipeline(
            self->m_sceneLibrary.get(), rojoRHI::FillMode::Solid, "lmx.render.scenePipelineMotion");
        pipeline) {
        self->m_scenePipelineMotion = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline =
            makeSceneMotionPipeline(self->m_sceneLibrary.get(), rojoRHI::FillMode::Wireframe,
                                    "lmx.render.sceneWireframePipelineMotion");
        pipeline) {
        self->m_sceneWireframePipelineMotion = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline =
            makeSceneMotionPipeline(self->m_sceneAutoLibrary.get(), rojoRHI::FillMode::Solid,
                                    "lmx.render.scenePipelineAutoMotion");
        pipeline) {
        self->m_scenePipelineAutoMotion = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline =
            makeSceneMotionPipeline(self->m_sceneAutoLibrary.get(), rojoRHI::FillMode::Wireframe,
                                    "lmx.render.sceneWireframePipelineAutoMotion");
        pipeline) {
        self->m_sceneWireframePipelineAutoMotion = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }

    // Sky vertices force z == 0, the reversed far plane: use GreaterEqual so they survive the
    // pass's own 0 clear, render inside faces, and avoid rewriting the unchanged depth value.
    const auto makeSkyPipeline = [&](rojoRHI::ShaderLibrary* library, const char* label) {
        return device.createGraphicsPipeline({.library = library,
                                              .vertexEntry = "vertexMain",
                                              .fragmentEntry = "fragmentMain",
                                              .colorFormat = sceneColorFormat,
                                              .depthFormat = rojoRHI::Format::D32Float,
                                              .depthTestEnable = true,
                                              .depthWriteEnable = false,
                                              .cullMode = rojoRHI::CullMode::None,
                                              .depthCompare = rojoRHI::DepthCompare::GreaterEqual,
                                              .label = label});
    };
    if (auto pipeline = makeSkyPipeline(self->m_skyLibrary.get(), "lmx.render.skyPipeline");
        pipeline) {
        self->m_skyPipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    // SkyAuto.slang's compiled twin, bound instead of the pipeline above whenever auto-exposure is
    // on (spec 9) -- see ScenePassAuto.slang's header for why this is a separate pipeline.
    if (auto pipeline = makeSkyPipeline(self->m_skyAutoLibrary.get(), "lmx.render.skyPipelineAuto");
        pipeline) {
        self->m_skyPipelineAuto = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }

    const auto makeSkyMotionPipeline = [&](rojoRHI::ShaderLibrary* library, const char* label) {
        return device.createGraphicsPipeline(
            {.library = library,
             .vertexEntry = "vertexMainMotion",
             .fragmentEntry = "fragmentMainMotion",
             .colorFormat = sceneColorFormat,
             .extraColorFormats = {kMotionFormat, kReactiveFormat, rojoRHI::Format::Unknown},
             .extraColorCount = 2,
             .depthFormat = rojoRHI::Format::D32Float,
             .depthTestEnable = true,
             .depthWriteEnable = false,
             .cullMode = rojoRHI::CullMode::None,
             .depthCompare = rojoRHI::DepthCompare::GreaterEqual,
             .label = label});
    };
    if (auto pipeline =
            makeSkyMotionPipeline(self->m_skyLibrary.get(), "lmx.render.skyPipelineMotion");
        pipeline) {
        self->m_skyPipelineMotion = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline =
            makeSkyMotionPipeline(self->m_skyAutoLibrary.get(), "lmx.render.skyPipelineAutoMotion");
        pipeline) {
        self->m_skyPipelineAutoMotion = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }

    for (uint32_t automatic = 0; automatic < 2; ++automatic) {
        auto library = device.loadShaderLibrary(automatic ? "Shaders/ScenePassAutoMask"
                                                          : "Shaders/ScenePassMask");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_maskSceneLibraries[automatic] = std::move(*library);
        for (uint32_t doubleSided = 0; doubleSided < 2; ++doubleSided) {
            for (uint32_t motion = 0; motion < 2; ++motion) {
                for (uint32_t wireframe = 0; wireframe < 2; ++wireframe) {
                    const uint32_t index = doubleSided * 8 + automatic * 4 + motion * 2 + wireframe;
                    const auto label = std::format("lmx.render.maskScenePipeline.{}", index);
                    auto pipeline = device.createGraphicsPipeline(
                        {.library = self->m_maskSceneLibraries[automatic].get(),
                         .vertexEntry = motion ? "vertexMainMotion" : "vertexMain",
                         .fragmentEntry = motion ? "fragmentMainMotion" : "fragmentMain",
                         .colorFormat = sceneColorFormat,
                         .extraColorFormats = {motion ? kMotionFormat : rojoRHI::Format::Unknown,
                                               motion ? kReactiveFormat : rojoRHI::Format::Unknown,
                                               rojoRHI::Format::Unknown},
                         .extraColorCount = motion ? 2u : 0u,
                         .depthFormat = rojoRHI::Format::D32Float,
                         .depthTestEnable = true,
                         .depthWriteEnable = true,
                         .fillMode =
                             wireframe ? rojoRHI::FillMode::Wireframe : rojoRHI::FillMode::Solid,
                         .cullMode =
                             doubleSided ? rojoRHI::CullMode::None : rojoRHI::CullMode::Back,
                         .depthCompare = rojoRHI::DepthCompare::Greater,
                         .label = label});
                    if (!pipeline) {
                        return std::unexpected(pipeline.error());
                    }
                    self->m_maskScenePipelines[index] = std::move(*pipeline);
                }
            }
        }
    }
    return self;
}

} // namespace lmx::render
