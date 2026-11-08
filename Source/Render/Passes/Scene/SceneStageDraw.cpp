//----------------------------------------------------------------------------------------------------------------------
/// @file SceneStageDraw.cpp
/// @brief Encodes scene geometry and sky commands with address-first roots.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Common/DrawEncoding.h"
#include "Render/Common/FrameData.h"
#include "Render/Common/GraphResources.h"
#include "Render/Passes/Scene/SceneStage.h"

#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/CaptureSchema.h"
#include "Render/Passes/Scene/SceneRoots.h"
#include "Render/Passes/Scene/SceneStageInternal.h"

#include <array>
#include <cstring>
#include <optional>
#include <span>

namespace lmx::render {
using namespace scene_detail;
namespace {
#ifndef NDEBUG
//======================================================================================================================
// GPU-private hand-built material tables expose no flags to the consumer tracker. Their normal
// read remains unobserved, like pointed frame-arena payloads; shading and graph state are
// unchanged.
std::optional<bool> readsNormal(const SceneView& view, const engine::DrawItem& item) {
    LMX_ASSERT(item.instanceRow < view.tables.instanceCount &&
                   item.instanceRow < view.tables.instanceRows.size(),
               "normal access requires the current instance row");
    const uint32_t materialRow = view.tables.instanceRows[item.instanceRow].materialRow;
    LMX_ASSERT(materialRow < view.tables.materialCount, "normal access requires a material row");
    const auto& materials = *view.tables.materials;
    LMX_ASSERT(view.tables.instanceRows.size() >= view.tables.instanceCount &&
                   view.tables.materialCount <= materials.desc().size / sizeof(engine::MaterialRow),
               "prepared table high-water counts exceed their storage");
    const uint64_t offset = uint64_t{materialRow} * sizeof(engine::MaterialRow);
    LMX_ASSERT(offset <= materials.desc().size &&
                   sizeof(engine::MaterialRow) <= materials.desc().size - offset,
               "material row exceeds its prepared buffer");
    if (materials.desc().memory != rojoRHI::Memory::Upload) {
        return std::nullopt;
    }
    uint32_t flags = 0;
    std::memcpy(&flags, materials.cpuPointer() + offset + offsetof(engine::MaterialRow, flags),
                sizeof(flags));
    return (flags & engine::kMaterialHasNormalMap) != 0;
}

//======================================================================================================================
void declareTextureRead(rojoRHI::CommandList& commands, const engine::TextureRef& texture) {
    const std::array<rojoRHI::ResourceAccess, 1> access{
        {{.texture = texture.texture,
          .range = {0, texture.mips, 0, texture.layers},
          .stage = rojoRHI::Stage::FragmentShader,
          .access = rojoRHI::Access::ShaderRead}}};
    commands.declareAccess(access);
}

//======================================================================================================================
// Direct loads rows [0, rowCount). Clustered loads each fragment's froxel record and, through it,
// index-list entries naming rows inside [0, rowCount); a record's range is a GPU value.
void declareLocalLightReads(rojoRHI::CommandList& commands, const PassResources& resources,
                            const SceneStageInputs& inputs, const LocalLightParams& params) {
    const auto read = [&](GraphBuffer handle, uint64_t size) {
        return rojoRHI::ResourceAccess{.buffer = resources.buffer(handle),
                                       .size = size,
                                       .stage = rojoRHI::Stage::FragmentShader,
                                       .access = rojoRHI::Access::ShaderRead};
    };
    std::array<rojoRHI::ResourceAccess, 3> accesses{};
    uint32_t count = 0;
    if (params.rowCount != 0) {
        accesses[count++] =
            read(*inputs.lights, uint64_t{params.rowCount} * sizeof(engine::LightRow));
    }
    if (static_cast<engine::LocalLightMode>(params.mode) == engine::LocalLightMode::Clustered) {
        accesses[count++] =
            read(*inputs.lightGrid, uint64_t{kClusterCount} * sizeof(ClusterRecord));
        accesses[count++] = read(*inputs.lightIndices, rojoRHI::kWholeSize);
    }
    commands.declareAccess(std::span{accesses}.first(count));
}
#endif
} // namespace

//======================================================================================================================
void SceneStage::draw(rojoRHI::CommandList& commands, const SceneView& view,
                      const SceneStageInputs& inputs, const PassUniforms& passUniforms,
                      const LocalLightParams& localLightParams, GraphTexture shadowRead,
                      GraphBuffer exposureCurrent, bool temporalEnabled,
                      const CameraFrameState& cameraState, const CameraFrameState& previousCamera,
                      glm::vec2 jitterNdc, const PassResources& resources) {
    const bool hasSky =
        view.skySphere.has_value() && view.skyCubemap != nullptr && view.skySphere->indexCount != 0;
    if (inputs.draws.runs.empty() && !hasSky) {
        return;
    }
    rojoRHI::Pipeline opaquePipeline;
    if (temporalEnabled) {
        opaquePipeline = view.autoExposureEnabled
                             ? (view.wireframe ? m_sceneWireframePipelineAutoMotion.get()
                                               : m_scenePipelineAutoMotion.get())
                             : (view.wireframe ? m_sceneWireframePipelineMotion.get()
                                               : m_scenePipelineMotion.get());
    } else {
        opaquePipeline =
            view.autoExposureEnabled
                ? (view.wireframe ? m_sceneWireframePipelineAuto.get() : m_scenePipelineAuto.get())
                : (view.wireframe ? m_sceneWireframePipeline.get() : m_scenePipeline.get());
    }
    const auto* irradiance = view.irradiance ? view.irradiance : inputs.blackCubeTexture;
    const auto* prefiltered = view.prefilteredEnv ? view.prefilteredEnv : inputs.blackCubeTexture;
    const auto* dfg = view.dfgLut ? view.dfgLut : inputs.zeroDfgTexture;
    const auto exposure =
        view.autoExposureEnabled ? resources.address(exposureCurrent) : rojoRHI::Addr{};
    if (!inputs.draws.runs.empty()) {
        commands.setPipeline(opaquePipeline);
        const auto uniformsAddress = frameData(commands, passUniforms);
        engine::registerFrameRoot("PassUniforms", uniformsAddress);
        // Every path loads the parameters; Direct also loads the light rows and Clustered the rows,
        // the grid and the index lists. An address a path never loads stays zero.
        const auto localLightMode = static_cast<engine::LocalLightMode>(localLightParams.mode);
        const bool shadesLocalLights = localLightMode != engine::LocalLightMode::Off;
        const bool clustered = localLightMode == engine::LocalLightMode::Clustered;
        LMX_ASSERT(!shadesLocalLights || inputs.lights.has_value(),
                   "local-light shading needs the imported light rows");
        LMX_ASSERT(!clustered || inputs.lightGrid.has_value(),
                   "clustered shading needs the light grid and index lists");
        const auto localLightParamsAddress = frameData(commands, localLightParams);
        engine::registerFrameRoot("LocalLightParams", localLightParamsAddress);
        const ScenePassRoot pass{
            .vertices = view.tables.vertices->address(),
            .visibleRows = resources.address(inputs.drawRows),
            .instances = view.tables.instances->address(),
            .materials = view.tables.materials->address(),
            .passUniforms = uniformsAddress,
            .exposure = exposure,
            .lights =
                shadesLocalLights ? resources.address(inputs.lights.value()) : rojoRHI::Addr{},
            .lightGrid = clustered ? resources.address(inputs.lightGrid.value()) : rojoRHI::Addr{},
            .lightIndices =
                clustered ? resources.address(inputs.lightIndices.value()) : rojoRHI::Addr{},
            .localLightParams = localLightParamsAddress,
            .shadow = static_cast<uint32_t>(resources.view(shadowRead)),
            .irradiance = static_cast<uint32_t>(irradiance->view()),
            .prefilteredEnv = static_cast<uint32_t>(prefiltered->view()),
            .dfg = static_cast<uint32_t>(dfg->view()),
            .linearSampler = static_cast<uint32_t>(inputs.linearSampler),
            .shadowSampler = static_cast<uint32_t>(inputs.shadowSampler),
            .iblSampler = static_cast<uint32_t>(inputs.iblSampler)};
        const auto passAddress = frameData(commands, pass);
        engine::registerFrameRoot("ScenePassRoot", passAddress);
        // Indirect and batched runs share one root and read drawId; a direct run's root carries
        // its firstEntry, because a vertex entry cannot read the base instance (RojoRHI ADR 0024).
        const bool direct = inputs.draws.mode == SubmissionMode::Direct;
        if (!direct) {
            engine::registerFrameRoot("SceneDrawRoot", commands.pushRoot(SceneDrawRoot{
                                                           .pass = passAddress, .indirect = 1u}));
        }
#ifndef NDEBUG
        LMX_ASSERT(view.tables.whiteTexture && view.tables.flatNormalTexture,
                   "scene draw declarations need the tables' material fallbacks");
#endif
        encodeDrawRuns(
            commands, {inputs.draws, view.tables.indices, opaquePipeline},
            [&](const DrawRun& run, const auto& bindPipeline) -> const engine::DrawItem& {
                const engine::DrawItem& item = view.items[run.itemIndex];
                const bool masked = item.alphaMode == engine::AlphaMode::Mask;
                const uint32_t maskIndex = (item.doubleSided ? 8u : 0u) +
                                           (view.autoExposureEnabled ? 4u : 0u) +
                                           (temporalEnabled ? 2u : 0u) + (view.wireframe ? 1u : 0u);
                bindPipeline(masked ? m_maskScenePipelines[maskIndex].get() : opaquePipeline);
                LMX_ASSERT(item.instanceRow < view.tables.instanceCount,
                           "draw instance must name a current table row");
                if (direct) {
                    engine::registerFrameRoot(
                        "SceneDrawRoot", commands.pushRoot(SceneDrawRoot{
                                             .pass = passAddress, .firstEntry = run.firstEntry}));
                }
#ifndef NDEBUG
                // The views the material row names: the item's textures, else the fallbacks.
                const auto* white = view.tables.whiteTexture;
                const auto* diffuse = item.diffuse ? item.diffuse : white;
                const auto* normal =
                    item.normalMap ? item.normalMap : view.tables.flatNormalTexture;
                const auto* metalRough = item.metallicRoughness ? item.metallicRoughness : white;
                const auto* occlusion = item.occlusion ? item.occlusion : white;
                const auto* emissive = item.emissiveMap ? item.emissiveMap : white;
                if (run.instanceCount != 0 && item.mesh.indexCount != 0) {
                    const std::array<rojoRHI::ResourceAccess, 6> buffers{
                        {{.buffer = view.tables.vertices->rhi(),
                          .size = view.tables.vertices->desc().size,
                          .stage = rojoRHI::Stage::VertexShader,
                          .access = rojoRHI::Access::ShaderRead},
                         {.buffer = inputs.draws.rows->rhi(),
                          .offset = uint64_t{run.firstEntry} * 4,
                          .size = uint64_t{run.instanceCount} * 4,
                          .stage = rojoRHI::Stage::VertexShader,
                          .access = rojoRHI::Access::ShaderRead},
                         {.buffer = view.tables.instances->rhi(),
                          .size = uint64_t{view.tables.instanceCount} * sizeof(engine::InstanceRow),
                          .stage = rojoRHI::Stage::VertexShader,
                          .access = rojoRHI::Access::ShaderRead},
                         {.buffer = view.tables.materials->rhi(),
                          .size = uint64_t{view.tables.materialCount} * sizeof(engine::MaterialRow),
                          .stage = rojoRHI::Stage::VertexShader,
                          .access = rojoRHI::Access::ShaderRead},
                         {.buffer = view.tables.instances->rhi(),
                          .size = uint64_t{view.tables.instanceCount} * sizeof(engine::InstanceRow),
                          .stage = rojoRHI::Stage::FragmentShader,
                          .access = rojoRHI::Access::ShaderRead},
                         {.buffer = view.tables.materials->rhi(),
                          .size = uint64_t{view.tables.materialCount} * sizeof(engine::MaterialRow),
                          .stage = rojoRHI::Stage::FragmentShader,
                          .access = rojoRHI::Access::ShaderRead}}};
                    commands.declareAccess(buffers);
                    for (const auto* texture :
                         {diffuse, metalRough, occlusion, emissive, irradiance, prefiltered, dfg}) {
                        declareTextureRead(commands, texture->ref());
                    }
                    if (readsNormal(view, item).value_or(false)) {
                        declareTextureRead(commands, normal->ref());
                    }
                    declareTextureRead(commands, resources.texture(shadowRead));
                    if (view.autoExposureEnabled) {
                        const std::array<rojoRHI::ResourceAccess, 1> access{
                            {{.buffer = resources.buffer(exposureCurrent),
                              .size = sizeof(float),
                              .stage = rojoRHI::Stage::FragmentShader,
                              .access = rojoRHI::Access::ShaderRead}}};
                        commands.declareAccess(access);
                    }
                    if (shadesLocalLights) {
                        declareLocalLightReads(commands, resources, inputs, localLightParams);
                    }
                }
#endif
                return item;
            });
    }

    // Draw the solid sky last so geometry rejects covered fragments at the reversed-depth clear.
    if (hasSky) {
        const SkyUniforms sky = buildSkyUniforms({.viewProj = cameraState.viewProjection,
                                                  .eyePos = passUniforms.eyePos,
                                                  .preExposure = passUniforms.preExposure,
                                                  .jitterNdc = jitterNdc,
                                                  .previousViewProj = previousCamera.viewProjection,
                                                  .previousEyePos = previousCamera.position});
        if (temporalEnabled) {
            commands.setPipeline(view.autoExposureEnabled ? m_skyPipelineAutoMotion.get()
                                                          : m_skyPipelineMotion.get());
        } else {
            commands.setPipeline(view.autoExposureEnabled ? m_skyPipelineAuto.get()
                                                          : m_skyPipeline.get());
        }
        const auto uniformsAddress = frameData(commands, sky);
        engine::registerFrameRoot("SkyUniforms", uniformsAddress);
        // Scene finalization already rebases sky indices into this whole vertex pool.
        const SkyRoot root{.vertices = view.tables.vertices->address(),
                           .uniforms = uniformsAddress,
                           .exposure = exposure,
                           .sky = static_cast<uint32_t>(view.skyCubemap->view()),
                           .sampler = static_cast<uint32_t>(inputs.linearSampler)};
        engine::registerFrameRoot("SkyRoot", commands.pushRoot(root));
#ifndef NDEBUG
        const std::array<rojoRHI::ResourceAccess, 1> vertices{
            {{.buffer = view.tables.vertices->rhi(),
              .size = view.tables.vertices->desc().size,
              .stage = rojoRHI::Stage::VertexShader,
              .access = rojoRHI::Access::ShaderRead}}};
        commands.declareAccess(vertices);
        declareTextureRead(commands, view.skyCubemap->ref());
        if (view.autoExposureEnabled) {
            const std::array<rojoRHI::ResourceAccess, 1> access{
                {{.buffer = resources.buffer(exposureCurrent),
                  .size = sizeof(float),
                  .stage = rojoRHI::Stage::FragmentShader,
                  .access = rojoRHI::Access::ShaderRead}}};
            commands.declareAccess(access);
        }
#endif
        commands.drawIndexed(view.tables.indices->rhi(), rojoRHI::IndexType::UInt32,
                             view.skySphere->indexCount, 1, view.skySphere->firstIndex, 0, 0);
    }
}

} // namespace lmx::render
