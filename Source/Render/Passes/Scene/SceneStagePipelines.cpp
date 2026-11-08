//----------------------------------------------------------------------------------------------------------------------
/// @file SceneStagePipelines.cpp
/// @brief Creates scene and sky pipeline variants with explicit raster states.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Scene/SceneStage.h"

#include "Engine/Gpu/GpuOwned.h"

#include "Render/Passes/Temporal/TemporalResolve.h"

#include <array>

#include <format>
#include <utility>

namespace lmx::render {

//======================================================================================================================
lmx::Result<std::unique_ptr<SceneStage>> SceneStage::create(engine::GpuContext& gpu,
                                                            rojoRHI::Format sceneColorFormat) {
    rojoRHI::Device& device = gpu.device();
    std::unique_ptr<SceneStage> self(new SceneStage);

    {
        auto library = gpu.loadShaderLibrary("ScenePass");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_sceneLibrary = std::move(*library);
    }
    {
        auto library = gpu.loadShaderLibrary("ScenePassAuto");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_sceneAutoLibrary = std::move(*library);
    }
    {
        auto library = gpu.loadShaderLibrary("Sky");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_skyLibrary = std::move(*library);
    }
    {
        auto library = gpu.loadShaderLibrary("SkyAuto");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_skyAutoLibrary = std::move(*library);
    }
    const std::array<rojoRHI::ColorTarget, 1> colors{{{.format = sceneColorFormat}}};
    const std::array<rojoRHI::ColorTarget, 3> motionColors{
        {{.format = sceneColorFormat}, {.format = kMotionFormat}, {.format = kReactiveFormat}}};
    const auto makeScenePipeline = [&](rojoRHI::ShaderLibrary library, rojoRHI::FillMode fill,
                                       const char* label) {
        return engine::adoptGpu(
            device,
            device.createPipeline({.vertex = {library, "vertexMain"},
                                   .fragment = {library, "fragmentMain"},
                                   .raster = {.cull = rojoRHI::CullMode::Back, .fill = fill},
                                   .depthStencil = {.depthTest = true,
                                                    .depthWrite = true,
                                                    .depthCompare = rojoRHI::CompareOp::Greater},
                                   .colors = colors,
                                   .depthFormat = rojoRHI::Format::D32Float,
                                   .label = label}));
    };
    const auto makeSceneMotionPipeline = [&](rojoRHI::ShaderLibrary library, rojoRHI::FillMode fill,
                                             const char* label) {
        return engine::adoptGpu(
            device,
            device.createPipeline({.vertex = {library, "vertexMainMotion"},
                                   .fragment = {library, "fragmentMainMotion"},
                                   .raster = {.cull = rojoRHI::CullMode::Back, .fill = fill},
                                   .depthStencil = {.depthTest = true,
                                                    .depthWrite = true,
                                                    .depthCompare = rojoRHI::CompareOp::Greater},
                                   .colors = motionColors,
                                   .depthFormat = rojoRHI::Format::D32Float,
                                   .label = label}));
    };
    {
        auto pipeline = makeScenePipeline(self->m_sceneLibrary.get(), rojoRHI::FillMode::Solid,
                                          "lmx.render.scenePipeline");
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_scenePipeline = std::move(*pipeline);
    }
    // Fill mode is baked into Metal pipeline state; compile both variants once.
    {
        auto pipeline = makeScenePipeline(self->m_sceneLibrary.get(), rojoRHI::FillMode::Wireframe,
                                          "lmx.render.sceneWireframePipeline");
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_sceneWireframePipeline = std::move(*pipeline);
    }
    // ScenePassAuto.slang's compiled twin, bound instead of the pipelines above whenever
    // auto-exposure is on (spec 9) -- see ScenePassAuto.slang's header for why this is a separate
    // pipeline rather than a branch inside the ones above.
    {
        auto pipeline = makeScenePipeline(self->m_sceneAutoLibrary.get(), rojoRHI::FillMode::Solid,
                                          "lmx.render.scenePipelineAuto");
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_scenePipelineAuto = std::move(*pipeline);
    }
    {
        auto pipeline =
            makeScenePipeline(self->m_sceneAutoLibrary.get(), rojoRHI::FillMode::Wireframe,
                              "lmx.render.sceneWireframePipelineAuto");
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_sceneWireframePipelineAuto = std::move(*pipeline);
    }

    {
        auto pipeline = makeSceneMotionPipeline(
            self->m_sceneLibrary.get(), rojoRHI::FillMode::Solid, "lmx.render.scenePipelineMotion");
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_scenePipelineMotion = std::move(*pipeline);
    }
    {
        auto pipeline =
            makeSceneMotionPipeline(self->m_sceneLibrary.get(), rojoRHI::FillMode::Wireframe,
                                    "lmx.render.sceneWireframePipelineMotion");
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_sceneWireframePipelineMotion = std::move(*pipeline);
    }
    {
        auto pipeline =
            makeSceneMotionPipeline(self->m_sceneAutoLibrary.get(), rojoRHI::FillMode::Solid,
                                    "lmx.render.scenePipelineAutoMotion");
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_scenePipelineAutoMotion = std::move(*pipeline);
    }
    {
        auto pipeline =
            makeSceneMotionPipeline(self->m_sceneAutoLibrary.get(), rojoRHI::FillMode::Wireframe,
                                    "lmx.render.sceneWireframePipelineAutoMotion");
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_sceneWireframePipelineAutoMotion = std::move(*pipeline);
    }

    // Sky vertices force z == 0, the reversed far plane: use GreaterEqual so they survive the
    // pass's own 0 clear, render inside faces, and avoid rewriting the unchanged depth value.
    const auto makeSkyPipeline = [&](rojoRHI::ShaderLibrary library, const char* label) {
        return engine::adoptGpu(
            device, device.createPipeline(
                        {.vertex = {library, "vertexMain"},
                         .fragment = {library, "fragmentMain"},
                         .raster = {.cull = rojoRHI::CullMode::None},
                         .depthStencil = {.depthTest = true,
                                          .depthWrite = false,
                                          .depthCompare = rojoRHI::CompareOp::GreaterOrEqual},
                         .colors = colors,
                         .depthFormat = rojoRHI::Format::D32Float,
                         .label = label}));
    };
    {
        auto pipeline = makeSkyPipeline(self->m_skyLibrary.get(), "lmx.render.skyPipeline");
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_skyPipeline = std::move(*pipeline);
    }
    // SkyAuto.slang's compiled twin, bound instead of the pipeline above whenever auto-exposure is
    // on (spec 9) -- see ScenePassAuto.slang's header for why this is a separate pipeline.
    {
        auto pipeline = makeSkyPipeline(self->m_skyAutoLibrary.get(), "lmx.render.skyPipelineAuto");
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_skyPipelineAuto = std::move(*pipeline);
    }

    const auto makeSkyMotionPipeline = [&](rojoRHI::ShaderLibrary library, const char* label) {
        return engine::adoptGpu(
            device, device.createPipeline(
                        {.vertex = {library, "vertexMainMotion"},
                         .fragment = {library, "fragmentMainMotion"},
                         .raster = {.cull = rojoRHI::CullMode::None},
                         .depthStencil = {.depthTest = true,
                                          .depthWrite = false,
                                          .depthCompare = rojoRHI::CompareOp::GreaterOrEqual},
                         .colors = motionColors,
                         .depthFormat = rojoRHI::Format::D32Float,
                         .label = label}));
    };
    {
        auto pipeline =
            makeSkyMotionPipeline(self->m_skyLibrary.get(), "lmx.render.skyPipelineMotion");
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_skyPipelineMotion = std::move(*pipeline);
    }
    {
        auto pipeline =
            makeSkyMotionPipeline(self->m_skyAutoLibrary.get(), "lmx.render.skyPipelineAutoMotion");
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_skyPipelineAutoMotion = std::move(*pipeline);
    }

    for (uint32_t automatic = 0; automatic < 2; ++automatic) {
        auto library = gpu.loadShaderLibrary(automatic ? "ScenePassAutoMask" : "ScenePassMask");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_maskSceneLibraries[automatic] = std::move(*library);
        for (uint32_t doubleSided = 0; doubleSided < 2; ++doubleSided) {
            for (uint32_t motion = 0; motion < 2; ++motion) {
                for (uint32_t wireframe = 0; wireframe < 2; ++wireframe) {
                    const uint32_t index = doubleSided * 8 + automatic * 4 + motion * 2 + wireframe;
                    const auto label = std::format("lmx.render.maskScenePipeline.{}", index);
                    auto pipeline = engine::adoptGpu(
                        device,
                        device.createPipeline(
                            {.vertex = {self->m_maskSceneLibraries[automatic].get(),
                                        motion ? "vertexMainMotion" : "vertexMain"},
                             .fragment = {self->m_maskSceneLibraries[automatic].get(),
                                          motion ? "fragmentMainMotion" : "fragmentMain"},
                             .raster = {.cull = doubleSided ? rojoRHI::CullMode::None
                                                            : rojoRHI::CullMode::Back,
                                        .fill = wireframe ? rojoRHI::FillMode::Wireframe
                                                          : rojoRHI::FillMode::Solid},
                             .depthStencil = {.depthTest = true,
                                              .depthWrite = true,
                                              .depthCompare = rojoRHI::CompareOp::Greater},
                             .colors = motion ? std::span<const rojoRHI::ColorTarget>{motionColors}
                                              : std::span<const rojoRHI::ColorTarget>{colors},
                             .depthFormat = rojoRHI::Format::D32Float,
                             .label = label}));
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
