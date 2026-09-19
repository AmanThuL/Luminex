//----------------------------------------------------------------------------------------------------------------------
/// @file SceneStage.cpp
/// @brief Implements scene and sky variants, uniforms and draw declaration.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/SceneStage.h"

#include "Core/Assert.h"
#include "Core/Color.h"
#include "RHI/CaptureSchema.h"
#include "Render/LightClusters.h"
#include "Render/TemporalResolve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <iterator>
#include <utility>
#include <vector>

namespace lmx::render {
namespace {

// Mirrors Shaders/Modules/Lighting.slang's DirLight.
struct DirLightUniform {
    glm::vec3 strength;     // 0
    float strengthPadding;  // 12
    glm::vec3 direction;    // 16
    float directionPadding; // 28
};
static_assert(sizeof(DirLightUniform) == 32, "must match Lighting.slang's DirLight");

// Mirrors Shaders/ScenePass.slang's PassUniforms.
struct PassUniforms {
    glm::mat4 viewProj;        // 0
    glm::mat4 shadowTransform; // 64
    glm::vec3 eyePos;          // 128
    float eyePadding;          // 140 -- the float3's tail
    float time;                // 144
    float preExposure;         // 148
    float alignmentPadding[2]; // 152 -- lights[] carries float3s and realigns to 16
    DirLightUniform lights[3]; // 160
    int32_t shadowFilter;      // 256
    int32_t tailPadding[3];    // 260
    // The motion pair, unjittered: rasterisation carries the jitter in PassUniforms.viewProj, and
    // motion must not, or a still scene would move by the jitter delta every frame.
    glm::mat4 viewProjUnjittered;         // 272
    glm::mat4 previousViewProjUnjittered; // 336
};
static_assert(sizeof(PassUniforms) == 400, "must match ScenePass.slang's PassUniforms");

// Mirrors Shaders/Sky.slang's SkyUniforms.
struct SkyUniforms {
    glm::mat4 viewProj;  // 0 -- unjittered on the temporal path; jitterNdc offsets the raster
    glm::vec3 eyePos;    // 64
    float eyePadding;    // 76 -- the float3's tail
    float preExposure;   // 80
    float jitterNdcX;    // 84
    float jitterNdcY;    // 88
    float jitterPadding; // 92 -- rounds the pair up to the matrix's 16-byte alignment
    // The previous frame's camera. Drawing the sphere at the previous eye is what leaves the sky's
    // motion carrying the camera's rotation and nothing else.
    glm::mat4 previousViewProj;  // 96
    glm::vec3 previousEyePos;    // 160
    float previousEyePosPadding; // 172 -- the float3's tail, rounds the struct to 176
};
static_assert(sizeof(SkyUniforms) == 176, "must match Sky.slang's SkyUniforms");

// Mirrors the scalar-packed LocalLightParams at b11 in LocalLights.slang.
struct LocalLightParams {
    uint32_t mode;                                // 0 -- LocalLightMode
    uint32_t rowCount;                            // 4
    uint32_t gridX;                               // 8
    uint32_t gridY;                               // 12
    uint32_t gridZ;                               // 16
    uint32_t activeOriginX;                       // 20
    uint32_t activeOriginY;                       // 24
    uint32_t activeWidth;                         // 28
    uint32_t activeHeight;                        // 32
    float sliceDepth[kClusterSliceBoundaryCount]; // 36
};
static_assert(sizeof(LocalLightParams) == 136, "must match LocalLights.slang's LocalLightParams");
static_assert(offsetof(LocalLightParams, rowCount) == 4);
static_assert(offsetof(LocalLightParams, gridX) == 8);
static_assert(offsetof(LocalLightParams, activeWidth) == 28);
static_assert(offsetof(LocalLightParams, sliceDepth) == 36);

// Shaders/Modules/Shadow.slang's kShadowFilterPcf / kShadowFilterPcss.
constexpr int32_t kShadowFilterPcf = 0;
constexpr int32_t kShadowFilterPcss = 1;

constexpr uint32_t kVertexBufferSlot = 0;
constexpr uint32_t kPassUniformsSlot = 2;
// The persistent exposure buffer (spec 9), read by ScenePassAuto.slang/SkyAuto.slang's fragment
// shaders -- the pipelines SceneStage selects only while auto-exposure is on. Bound via
// bindBuffer (a plain buffer read, not a storage binding) so a *raster* pass may read it --
// bindStorageBuffer is compute-pass-only.
constexpr uint32_t kExposureOverrideSlot = 3;
// The scene pass's texture slot map, which Shaders/ScenePass.slang's header documents in full.
// Two groups share one index space: a per-draw material set rebound for every DrawItem, and a
// per-pass shared set bound once before the draw loop.
//
// Slot 2 is the sky cubemap. Only Shaders/Sky.slang reads it, and the sky draws at the end of this
// same render pass, so it is bound beside that draw rather than with the shared set -- the scene
// fragment stopped sampling the sky when the prefiltered environment (slot 8) replaced its ad-hoc
// mirror reflection.
constexpr uint32_t kDiffuseTextureSlot = 0;
constexpr uint32_t kNormalTextureSlot = 1;
constexpr uint32_t kSkyTextureSlot = 2;
constexpr uint32_t kShadowTextureSlot = 3;
constexpr uint32_t kMetallicRoughnessTextureSlot = 4;
constexpr uint32_t kOcclusionTextureSlot = 5;
constexpr uint32_t kEmissiveTextureSlot = 6;
constexpr uint32_t kIrradianceTextureSlot = 7;
constexpr uint32_t kPrefilteredEnvTextureSlot = 8;
constexpr uint32_t kDfgLutTextureSlot = 9;
constexpr uint32_t kLinearSamplerSlot = 0;
constexpr uint32_t kShadowSamplerSlot = 1;
constexpr uint32_t kIblSamplerSlot = 2;

//======================================================================================================================
// A zero-light frame must not index even a fallback grid.
LocalLightMode resolveLocalLightMode(LocalLightMode requested, uint32_t liveLightCount,
                                     bool hasClusters) {
    if (requested == LocalLightMode::Off || liveLightCount == 0)
        return LocalLightMode::Off;
    LMX_ASSERT(requested != LocalLightMode::Clustered || hasClusters,
               "clustered shading requires this frame's grid and list");
    return requested;
}

//======================================================================================================================
DirLightUniform toUniform(const DirectionalLight& light) {
    return {.strength = light.strength,
            .strengthPadding = 0.0f,
            .direction = light.direction,
            .directionPadding = 0.0f};
}

} // namespace

//======================================================================================================================
void SceneStage::registerSceneTableLayoutsForCapture() {
    using rhi::debug::CaptureSchema;
    CaptureSchema& schema = CaptureSchema::instance();

    schema.registerUniformStruct(
        {.name = "DrawUniforms",
         .slot = kDrawUniformsSlot,
         .sizeBytes = sizeof(DrawUniforms),
         .fields = {{"firstEntry", offsetof(DrawUniforms, firstEntry), "uint"}}});
    schema.registerUniformStruct(
        {.name = "InstanceRow",
         .slot = kSceneInstancesSlot,
         .sizeBytes = sizeof(InstanceRow),
         .fields = {{"model", offsetof(InstanceRow, model), "float4x4"},
                    {"previousModel", offsetof(InstanceRow, previousModel), "float4x4"},
                    {"normalMatrix", offsetof(InstanceRow, normalMatrix), "float4x4"},
                    {"meshRow", offsetof(InstanceRow, meshRow), "uint"},
                    {"materialRow", offsetof(InstanceRow, materialRow), "uint"},
                    {"flags", offsetof(InstanceRow, flags), "uint"},
                    {"emissiveScale", offsetof(InstanceRow, emissiveScale), "float"},
                    {"worldBoundsMin", offsetof(InstanceRow, worldBoundsMin), "float3"},
                    {"worldBoundsMax", offsetof(InstanceRow, worldBoundsMax), "float3"}}});
    schema.registerUniformStruct(
        {.name = "MaterialRow",
         .slot = kSceneMaterialsSlot,
         .sizeBytes = sizeof(MaterialRow),
         .fields = {{"uvTransform", offsetof(MaterialRow, uvTransform), "float4x4"},
                    {"albedo", offsetof(MaterialRow, albedo), "float4"},
                    {"emissive", offsetof(MaterialRow, emissive), "float3"},
                    {"roughness", offsetof(MaterialRow, roughness), "float"},
                    {"metallic", offsetof(MaterialRow, metallic), "float"},
                    {"occlusionStrength", offsetof(MaterialRow, occlusionStrength), "float"},
                    {"alphaCutoff", offsetof(MaterialRow, alphaCutoff), "float"},
                    {"flags", offsetof(MaterialRow, flags), "uint"}}});
    schema.registerUniformStruct(
        {.name = "MeshRow",
         .slot = kSceneMeshesSlot,
         .sizeBytes = sizeof(MeshRow),
         .fields = {{"firstIndex", offsetof(MeshRow, firstIndex), "uint"},
                    {"indexCount", offsetof(MeshRow, indexCount), "uint"},
                    {"firstVertex", offsetof(MeshRow, firstVertex), "uint"},
                    {"vertexCount", offsetof(MeshRow, vertexCount), "uint"},
                    {"boundsMin", offsetof(MeshRow, boundsMin), "float3"},
                    {"boundsMax", offsetof(MeshRow, boundsMax), "float3"}}});
    schema.registerUniformStruct(
        {.name = "LightRow",
         .slot = kSceneLightsSlot,
         .sizeBytes = sizeof(LightRow),
         .fields = {{"position", offsetof(LightRow, position), "float3"},
                    {"range", offsetof(LightRow, range), "float"},
                    {"strength", offsetof(LightRow, strength), "float3"},
                    {"spotScale", offsetof(LightRow, spotScale), "float"},
                    {"direction", offsetof(LightRow, direction), "float3"},
                    {"spotOffset", offsetof(LightRow, spotOffset), "float"},
                    {"boundCentre", offsetof(LightRow, boundCentre), "float3"},
                    {"boundRadius", offsetof(LightRow, boundRadius), "float"}}});
    schema.registerUniformStruct(
        {.name = "LocalLightParams",
         .slot = kLocalLightParamsSlot,
         .sizeBytes = sizeof(LocalLightParams),
         .fields = {{"mode", offsetof(LocalLightParams, mode), "uint"},
                    {"rowCount", offsetof(LocalLightParams, rowCount), "uint"},
                    {"gridX", offsetof(LocalLightParams, gridX), "uint"},
                    {"gridY", offsetof(LocalLightParams, gridY), "uint"},
                    {"gridZ", offsetof(LocalLightParams, gridZ), "uint"},
                    {"activeOriginX", offsetof(LocalLightParams, activeOriginX), "uint"},
                    {"activeOriginY", offsetof(LocalLightParams, activeOriginY), "uint"},
                    {"activeWidth", offsetof(LocalLightParams, activeWidth), "uint"},
                    {"activeHeight", offsetof(LocalLightParams, activeHeight), "uint"},
                    {"sliceDepth", offsetof(LocalLightParams, sliceDepth), "float[25]"}}});
}

//======================================================================================================================
void SceneStage::registerPassLayoutsForCapture() {
    using rhi::debug::CaptureSchema;
    CaptureSchema& schema = CaptureSchema::instance();

    using rhi::debug::SchemaUniformField;

    // Derive array offsets from the element stride to avoid duplicated layout literals.
    constexpr uint32_t kLightCount = sizeof(PassUniforms::lights) / sizeof(DirLightUniform);
    std::vector<SchemaUniformField> passFields{
        {"viewProj", offsetof(PassUniforms, viewProj), "float4x4"},
        {"shadowTransform", offsetof(PassUniforms, shadowTransform), "float4x4"},
        {"eyePos", offsetof(PassUniforms, eyePos), "float3"},
        {"time", offsetof(PassUniforms, time), "float"},
        {"preExposure", offsetof(PassUniforms, preExposure), "float"}};
    for (uint32_t light = 0; light < kLightCount; ++light) {
        const uint32_t base =
            uint32_t{offsetof(PassUniforms, lights)} + light * uint32_t{sizeof(DirLightUniform)};
        passFields.push_back({std::format("lights[{}].strength", light),
                              base + uint32_t{offsetof(DirLightUniform, strength)}, "float3"});
        passFields.push_back({std::format("lights[{}].direction", light),
                              base + uint32_t{offsetof(DirLightUniform, direction)}, "float3"});
    }
    // Preserve offset order for comparison with raw capture bytes.
    passFields.push_back(
        {"shadowFilter", offsetof(PassUniforms, shadowFilter), "int"}); // kShadowFilterPcf/Pcss
    passFields.push_back(
        {"viewProjUnjittered", offsetof(PassUniforms, viewProjUnjittered), "float4x4"});
    passFields.push_back({"previousViewProjUnjittered",
                          offsetof(PassUniforms, previousViewProjUnjittered), "float4x4"});
    schema.registerUniformStruct({.name = "PassUniforms",
                                  .slot = kPassUniformsSlot,
                                  .sizeBytes = sizeof(PassUniforms),
                                  .fields = std::move(passFields)});

    schema.registerUniformStruct(
        {.name = "SkyUniforms",
         .slot = kPassUniformsSlot,
         .sizeBytes = sizeof(SkyUniforms),
         .fields = {{"viewProj", offsetof(SkyUniforms, viewProj), "float4x4"},
                    {"eyePos", offsetof(SkyUniforms, eyePos), "float3"},
                    {"preExposure", offsetof(SkyUniforms, preExposure), "float"},
                    {"jitterNdcX", offsetof(SkyUniforms, jitterNdcX), "float"},
                    {"jitterNdcY", offsetof(SkyUniforms, jitterNdcY), "float"},
                    {"previousViewProj", offsetof(SkyUniforms, previousViewProj), "float4x4"},
                    {"previousEyePos", offsetof(SkyUniforms, previousEyePos), "float3"}}});
}

//======================================================================================================================
rhi::Result<std::unique_ptr<SceneStage>> SceneStage::create(rhi::Device& device,
                                                            rhi::Format sceneColorFormat) {
    std::unique_ptr<SceneStage> self(new SceneStage);

    // Minimal immutable storage keeps every declared slot valid when its selection path is unused.
    {
        const LightRow freeRow;
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
    const auto makeScenePipeline = [&](rhi::ShaderLibrary* library, rhi::FillMode fill,
                                       const char* label) {
        return device.createGraphicsPipeline({.library = library,
                                              .vertexEntry = "vertexMain",
                                              .fragmentEntry = "fragmentMain",
                                              .colorFormat = sceneColorFormat,
                                              .depthFormat = rhi::Format::D32Float,
                                              .depthTestEnable = true,
                                              .depthWriteEnable = true,
                                              .fillMode = fill,
                                              .cullMode = rhi::CullMode::Back,
                                              // Reversed depth: the pass clears to 0 and the
                                              // nearer fragment is the larger one.
                                              .depthCompare = rhi::DepthCompare::Greater,
                                              .label = label});
    };
    // The motion twin of makeScenePipeline: the motion entry points, and the motion target as a
    // second colour attachment. Compiled up front rather than on the frame temporal is first
    // enabled, because a pipeline compile in the middle of a frame is a hitch a toggle should not
    // cost.
    const auto makeSceneMotionPipeline = [&](rhi::ShaderLibrary* library, rhi::FillMode fill,
                                             const char* label) {
        return device.createGraphicsPipeline(
            {.library = library,
             .vertexEntry = "vertexMainMotion",
             .fragmentEntry = "fragmentMainMotion",
             .colorFormat = sceneColorFormat,
             .extraColorFormats = {kMotionFormat, kReactiveFormat, rhi::Format::Unknown},
             .extraColorCount = 2,
             .depthFormat = rhi::Format::D32Float,
             .depthTestEnable = true,
             .depthWriteEnable = true,
             .fillMode = fill,
             .cullMode = rhi::CullMode::Back,
             .depthCompare = rhi::DepthCompare::Greater,
             .label = label});
    };
    if (auto pipeline = makeScenePipeline(self->m_sceneLibrary.get(), rhi::FillMode::Solid,
                                          "lmx.render.scenePipeline");
        pipeline) {
        self->m_scenePipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    // Fill mode is baked into Metal pipeline state; compile both variants once.
    if (auto pipeline = makeScenePipeline(self->m_sceneLibrary.get(), rhi::FillMode::Wireframe,
                                          "lmx.render.sceneWireframePipeline");
        pipeline) {
        self->m_sceneWireframePipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    // ScenePassAuto.slang's compiled twin, bound instead of the pipelines above whenever
    // auto-exposure is on (spec 9) -- see ScenePassAuto.slang's header for why this is a separate
    // pipeline rather than a branch inside the ones above.
    if (auto pipeline = makeScenePipeline(self->m_sceneAutoLibrary.get(), rhi::FillMode::Solid,
                                          "lmx.render.scenePipelineAuto");
        pipeline) {
        self->m_scenePipelineAuto = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline = makeScenePipeline(self->m_sceneAutoLibrary.get(), rhi::FillMode::Wireframe,
                                          "lmx.render.sceneWireframePipelineAuto");
        pipeline) {
        self->m_sceneWireframePipelineAuto = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }

    if (auto pipeline = makeSceneMotionPipeline(self->m_sceneLibrary.get(), rhi::FillMode::Solid,
                                                "lmx.render.scenePipelineMotion");
        pipeline) {
        self->m_scenePipelineMotion = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline =
            makeSceneMotionPipeline(self->m_sceneLibrary.get(), rhi::FillMode::Wireframe,
                                    "lmx.render.sceneWireframePipelineMotion");
        pipeline) {
        self->m_sceneWireframePipelineMotion = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline =
            makeSceneMotionPipeline(self->m_sceneAutoLibrary.get(), rhi::FillMode::Solid,
                                    "lmx.render.scenePipelineAutoMotion");
        pipeline) {
        self->m_scenePipelineAutoMotion = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline =
            makeSceneMotionPipeline(self->m_sceneAutoLibrary.get(), rhi::FillMode::Wireframe,
                                    "lmx.render.sceneWireframePipelineAutoMotion");
        pipeline) {
        self->m_sceneWireframePipelineAutoMotion = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }

    // Sky vertices force z == 0, the reversed far plane: use GreaterEqual so they survive the
    // pass's own 0 clear, render inside faces, and avoid rewriting the unchanged depth value.
    const auto makeSkyPipeline = [&](rhi::ShaderLibrary* library, const char* label) {
        return device.createGraphicsPipeline({.library = library,
                                              .vertexEntry = "vertexMain",
                                              .fragmentEntry = "fragmentMain",
                                              .colorFormat = sceneColorFormat,
                                              .depthFormat = rhi::Format::D32Float,
                                              .depthTestEnable = true,
                                              .depthWriteEnable = false,
                                              .cullMode = rhi::CullMode::None,
                                              .depthCompare = rhi::DepthCompare::GreaterEqual,
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

    const auto makeSkyMotionPipeline = [&](rhi::ShaderLibrary* library, const char* label) {
        return device.createGraphicsPipeline(
            {.library = library,
             .vertexEntry = "vertexMainMotion",
             .fragmentEntry = "fragmentMainMotion",
             .colorFormat = sceneColorFormat,
             .extraColorFormats = {kMotionFormat, kReactiveFormat, rhi::Format::Unknown},
             .extraColorCount = 2,
             .depthFormat = rhi::Format::D32Float,
             .depthTestEnable = true,
             .depthWriteEnable = false,
             .cullMode = rhi::CullMode::None,
             .depthCompare = rhi::DepthCompare::GreaterEqual,
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
                         .extraColorFormats = {motion ? kMotionFormat : rhi::Format::Unknown,
                                               motion ? kReactiveFormat : rhi::Format::Unknown,
                                               rhi::Format::Unknown},
                         .extraColorCount = motion ? 2u : 0u,
                         .depthFormat = rhi::Format::D32Float,
                         .depthTestEnable = true,
                         .depthWriteEnable = true,
                         .fillMode = wireframe ? rhi::FillMode::Wireframe : rhi::FillMode::Solid,
                         .cullMode = doubleSided ? rhi::CullMode::None : rhi::CullMode::Back,
                         .depthCompare = rhi::DepthCompare::Greater,
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

//======================================================================================================================
GraphTexture SceneStage::declare(RenderGraph& graph, rhi::CommandList& commands,
                                 const SceneView& view, const SceneStageInputs& inputs) {
    const bool temporalEnabled = inputs.temporalEnabled;
    const auto& cameraState = inputs.camera;
    const auto& previousCamera = inputs.previousCamera;
    const auto& extents = inputs.extents;
    const auto& jitterPixels = inputs.jitterPixels;
    const auto& clearColor = inputs.clearColor;
    const bool upscaled =
        extents.renderWidth != extents.outputWidth || extents.renderHeight != extents.outputHeight;
    const GraphTexture shadowRead = inputs.shadowRead;
    const GraphTexture sceneColor = inputs.sceneColor;
    const GraphTexture sceneDepth = inputs.sceneDepth;
    const GraphTexture motionTargetHandle = inputs.motion;
    const GraphTexture reactiveTargetHandle = inputs.reactive;
    const GraphBuffer exposureCurrent = inputs.exposure;

    // Rasterisation takes the jitter; motion never does. With temporal off the two are the same
    // matrix, derived exactly as this frame's projection * view was before jitter existed.
    const glm::mat4 viewProj =
        temporalEnabled ? cameraState.viewProjectionJittered : cameraState.viewProjection;

    // One stop is one doubling, so the slider's unit becomes a multiply here. This is what every
    // fragment applies in manual mode -- ScenePass.slang/Sky.slang, unchanged from before auto-
    // exposure existed, which is what parity with pre-M5 output when auto is off rests on. Auto
    // mode binds the ScenePassAuto.slang/SkyAuto.slang pipelines below, which multiply by
    // gExposureOverride instead (spec 9) and never read PassUniforms.preExposure at all. This CPU
    // value still seeds a reset frame's exposure buffer above and pre-exposes the clear colour
    // below.
    const float preExposure = std::exp2(view.exposureEv);

    PassUniforms passUniforms{};
    passUniforms.viewProj = viewProj;
    passUniforms.shadowTransform = inputs.shadowTransform;
    passUniforms.eyePos = inputs.eyePosition;
    passUniforms.time = inputs.timeSeconds;
    passUniforms.preExposure = preExposure;
    for (size_t i = 0; i < std::size(passUniforms.lights); ++i) {
        passUniforms.lights[i] = toUniform(view.lights[i]);
    }
    passUniforms.shadowFilter =
        view.shadowFilter == ShadowFilter::PCSS ? kShadowFilterPcss : kShadowFilterPcf;
    passUniforms.viewProjUnjittered = cameraState.viewProjection;
    passUniforms.previousViewProjUnjittered = previousCamera.viewProjection;

    LMX_ASSERT(inputs.lightGrid.has_value() == inputs.lightIndices.has_value(),
               "cluster grid and index list must be supplied together");
    // The render area is origin-anchored; lookup uses its active extent and reversed-Z boundaries.
    const LocalLightMode localLightMode =
        resolveLocalLightMode(view.localLightMode, view.tables.liveLightCount,
                              inputs.lightGrid.has_value() && inputs.lightIndices.has_value());
    LocalLightParams localLightParams{
        .mode = static_cast<uint32_t>(localLightMode),
        .rowCount = localLightMode == LocalLightMode::Off ? 0u : view.tables.lightRowCount,
        .gridX = kClusterTilesX,
        .gridY = kClusterTilesY,
        .gridZ = kClusterSliceCount,
        .activeOriginX = 0,
        .activeOriginY = 0,
        .activeWidth = extents.renderWidth,
        .activeHeight = extents.renderHeight,
        .sliceDepth = {}};
    LMX_ASSERT(cameraState.nearZ > 0.0f, "the froxel slice table needs a positive near plane");
    const auto sliceDepths = clusterSliceDepths(cameraState.nearZ);
    std::copy(sliceDepths.begin(), sliceDepths.end(), std::begin(localLightParams.sliceDepth));

    // The clear has to be the value a fragment writing that colour would have produced, or the
    // background and the geometry would disagree about what space the target holds. That means
    // both steps a fragment takes: the authored display-space colour decodes to linear (once,
    // here), and it is pre-exposed like everything else -- without the second multiply the
    // background would sit still while an exposure change moved every shaded pixel.
    //
    // This always pre-exposes by the *manual* value, even in auto mode: the clear is a CPU-baked
    // hardware clear value, and auto mode's actual exposure lives only in the GPU-side exposure
    // buffer (that is the whole point of not reading it back). In practice this is a non-issue --
    // every scene with a sky draws over the clear entirely -- and is strictly better than the
    // alternative of a blocking readback just to keep an unshaded background pixel exact.
    const glm::vec3 clearLinear =
        lmx::srgbToLinear(glm::vec3(clearColor[0], clearColor[1], clearColor[2])) * preExposure;

    PassDesc sceneDesc;
    sceneDesc.textureReads.push_back(shadowRead);
    sceneDesc.bufferReads.assign(inputs.sceneBuffers.begin(), inputs.sceneBuffers.end());
    sceneDesc.bufferReads.push_back(inputs.drawRows);
    sceneDesc.indirectBufferReads.push_back(inputs.drawArguments);
    if (inputs.lights.has_value()) {
        sceneDesc.bufferReads.push_back(*inputs.lights);
    }
    if (inputs.lightGrid) {
        sceneDesc.bufferReads.push_back(*inputs.lightGrid);
        sceneDesc.bufferReads.push_back(*inputs.lightIndices);
    }
    // Declared only in auto mode: manual mode's shading never reads the feedback buffer (spec 9),
    // so declaring the read here always would be a lie about what the pass depends on.
    if (view.autoExposureEnabled) {
        sceneDesc.bufferReads.push_back(exposureCurrent);
    }
    sceneDesc.color =
        ColorAttachment{.handle = sceneColor,
                        .load = LoadOp::Clear,
                        .store = StoreOp::Store,
                        .clearColor = {clearLinear.r, clearLinear.g, clearLinear.b, clearColor[3]}};
    // 0 is the reversed projection's horizon -- no geometry is ever farther, so every fragment's
    // Greater test passes against a cleared texel, and the sky's GreaterEqual matches it exactly.
    //
    // Stored rather than discarded: nothing in this frame reads it after the pass, but the buffer
    // is the frame's own record of where its geometry is, and discarding leaves it undefined the
    // moment the pass ends -- so depthTarget() would hand a caller garbage rather than depth.
    sceneDesc.depth = DepthAttachment{
        .handle = sceneDepth, .load = LoadOp::Clear, .store = StoreOp::Store, .clearDepth = 0.0f};
    // Attachment 1 on the temporal path only. Zero is the motion of a surface that did not move,
    // which is the right value for the pixels no draw covers: a consumer reading the clear
    // reprojects onto itself rather than onto a neighbour.
    if (temporalEnabled) {
        sceneDesc.extraColor.push_back(ColorAttachment{.handle = motionTargetHandle,
                                                       .load = LoadOp::Clear,
                                                       .store = StoreOp::Store,
                                                       .clearColor = {0.0f, 0.0f, 0.0f, 0.0f}});
        // Attachment 2, on the same terms. Zero is "accumulate freely", which is the right value
        // for a texel no draw covers: the clear colour has no emissive that could switch on.
        sceneDesc.extraColor.push_back(ColorAttachment{.handle = reactiveTargetHandle,
                                                       .load = LoadOp::Clear,
                                                       .store = StoreOp::Store,
                                                       .clearColor = {0.0f, 0.0f, 0.0f, 0.0f}});
    }
    // The active rectangle, declared only when it is not the whole attachment: at scale 1 the pass
    // states exactly what it always did, which is what keeps the frame's declaration -- and every
    // golden over it -- byte for byte the one M6.2 made.
    if (upscaled) {
        sceneDesc.renderAreaWidth = extents.renderWidth;
        sceneDesc.renderAreaHeight = extents.renderHeight;
    }
    // The sky's own jitter, in NDC: its motion pair has to stay unjittered, so its vertex entry
    // point offsets the rasterised position instead of carrying the jitter in its matrix.
    const glm::vec2 jitterNdc{2.0f * jitterPixels.x / static_cast<float>(extents.renderWidth),
                              2.0f * jitterPixels.y / static_cast<float>(extents.renderHeight)};
    graph.addPass(
        "lmx.pass.scene", std::move(sceneDesc),
        [this, &commands, view, inputs, passUniforms, localLightParams, shadowRead, exposureCurrent,
         temporalEnabled, cameraState, previousCamera, jitterNdc](const PassResources& resources) {
            // Resolved rather than captured: the graph hands over the shadow map only because this
            // pass declared reading it, which is what ordered it after the pass that wrote it.
            const GraphResult<rhi::Texture*> shadowMapTexture = resources.texture(shadowRead);
            LMX_ASSERT(shadowMapTexture.has_value(), shadowMapTexture.error().message);

            // Auto-exposure selects ScenePassAuto.slang's compiled pipeline instead of
            // ScenePass.slang's (spec 9): a separate shader file and pipeline, not a runtime
            // branch in one, is what keeps the manual pipeline's compiled output identical to
            // pre-M5 -- see ScenePassAuto.slang's header.
            rhi::GraphicsPipeline* opaquePipeline = nullptr;
            if (temporalEnabled) {
                opaquePipeline = view.autoExposureEnabled
                                     ? (view.wireframe ? m_sceneWireframePipelineAutoMotion.get()
                                                       : m_scenePipelineAutoMotion.get())
                                     : (view.wireframe ? m_sceneWireframePipelineMotion.get()
                                                       : m_scenePipelineMotion.get());
            } else {
                opaquePipeline =
                    view.autoExposureEnabled
                        ? (view.wireframe ? m_sceneWireframePipelineAuto.get()
                                          : m_scenePipelineAuto.get())
                        : (view.wireframe ? m_sceneWireframePipeline.get() : m_scenePipeline.get());
            }
            commands.bindPipeline(*opaquePipeline);
            rhi::GraphicsPipeline* boundScenePipeline = opaquePipeline;
            commands.bindSampler(kLinearSamplerSlot, *inputs.linearSampler);
            commands.bindSampler(kShadowSamplerSlot, *inputs.shadowSampler);
            commands.bindSampler(kIblSamplerSlot, *inputs.iblSampler);
            commands.bindTexture(kShadowTextureSlot, **shadowMapTexture);
            // The pass-wide IBL set. Each slot falls back independently, so a SceneView that
            // carries no environment still renders -- with both image-based terms at zero.
            commands.bindTexture(kIrradianceTextureSlot, view.irradiance != nullptr
                                                             ? *view.irradiance
                                                             : *inputs.blackCubeTexture);
            commands.bindTexture(kPrefilteredEnvTextureSlot, view.prefilteredEnv != nullptr
                                                                 ? *view.prefilteredEnv
                                                                 : *inputs.blackCubeTexture);
            commands.bindTexture(kDfgLutTextureSlot,
                                 view.dfgLut != nullptr ? *view.dfgLut : *inputs.zeroDfgTexture);
            // Only ScenePassAuto.slang/SkyAuto.slang declare this resource at all, so it is bound
            // only when their pipelines are the ones in use.
            if (view.autoExposureEnabled) {
                const GraphResult<rhi::Buffer*> exposureOverride =
                    resources.buffer(exposureCurrent);
                LMX_ASSERT(exposureOverride.has_value(), exposureOverride.error().message);
                commands.bindBuffer(kExposureOverrideSlot, **exposureOverride);
            }
            commands.bindFrameData(kPassUniformsSlot, passUniforms);
            // Declared slots stay bound even when the selected loop never reads their data.
            rhi::Buffer* lightRows = m_fallbackLightRows.get();
            if (inputs.lights.has_value()) {
                const GraphResult<rhi::Buffer*> imported = resources.buffer(*inputs.lights);
                LMX_ASSERT(imported.has_value(), imported.error().message);
                lightRows = *imported;
            }
            commands.bindBuffer(kSceneLightsSlot, *lightRows);
            rhi::Buffer* grid = m_fallbackClusterGrid.get();
            rhi::Buffer* indices = m_fallbackClusterIndices.get();
            if (inputs.lightGrid) {
                const auto gridResult = resources.buffer(*inputs.lightGrid);
                const auto indexResult = resources.buffer(*inputs.lightIndices);
                LMX_ASSERT(gridResult.has_value(), gridResult.error().message);
                LMX_ASSERT(indexResult.has_value(), indexResult.error().message);
                grid = *gridResult;
                indices = *indexResult;
            }
            commands.bindBuffer(kLightClusterGridSlot, *grid);
            commands.bindBuffer(kLightClusterIndexSlot, *indices);
            commands.bindFrameData(kLocalLightParamsSlot, localLightParams);
            if (view.tables.vertices) {
                commands.bindBuffer(kVertexBufferSlot, *view.tables.vertices);
                commands.bindBuffer(kSceneInstancesSlot, *view.tables.instances);
                commands.bindBuffer(kSceneMaterialsSlot, *view.tables.materials);
            }

            commands.bindBuffer(kVisibleRowsSlot, *inputs.draws.rows);
            if (inputs.draws.mode != SubmissionMode::Direct)
                commands.bindFrameData(kDrawUniformsSlot, DrawUniforms{0});
            for (const auto& run : inputs.draws.runs) {
                const DrawItem& item = view.items[run.itemIndex];
                const bool masked = item.alphaMode == AlphaMode::Mask;
                const uint32_t maskIndex = (item.doubleSided ? 8u : 0u) +
                                           (view.autoExposureEnabled ? 4u : 0u) +
                                           (temporalEnabled ? 2u : 0u) + (view.wireframe ? 1u : 0u);
                auto* pipeline = masked ? m_maskScenePipelines[maskIndex].get() : opaquePipeline;
                if (pipeline != boundScenePipeline) {
                    commands.bindPipeline(*pipeline);
                    boundScenePipeline = pipeline;
                }
                LMX_ASSERT(item.instanceRow < view.tables.instanceCount,
                           "draw instance must name a current table row");
                commands.bindTexture(kDiffuseTextureSlot, item.diffuse != nullptr
                                                              ? *item.diffuse
                                                              : *inputs.whiteTexture);
                commands.bindTexture(kNormalTextureSlot, item.normalMap != nullptr
                                                             ? *item.normalMap
                                                             : *inputs.flatNormalTexture);
                // The shared white fallback lets each factor pass through unchanged when a
                // material carries no map -- white is the identity for all three.
                commands.bindTexture(kMetallicRoughnessTextureSlot,
                                     item.metallicRoughness != nullptr ? *item.metallicRoughness
                                                                       : *inputs.whiteTexture);
                commands.bindTexture(kOcclusionTextureSlot, item.occlusion != nullptr
                                                                ? *item.occlusion
                                                                : *inputs.whiteTexture);
                commands.bindTexture(kEmissiveTextureSlot, item.emissiveMap != nullptr
                                                               ? *item.emissiveMap
                                                               : *inputs.whiteTexture);

                if (inputs.draws.mode == SubmissionMode::Direct) {
                    commands.bindFrameData(kDrawUniformsSlot, DrawUniforms{run.firstEntry});
                    commands.drawIndexed(*view.tables.indices, item.mesh.indexCount,
                                         item.mesh.firstIndex);
                } else {
                    commands.drawIndexedIndirect(*view.tables.indices, *inputs.draws.arguments,
                                                 uint64_t{run.argumentIndex} *
                                                     sizeof(rhi::DrawIndexedIndirectArgs));
                }
            }

            // Draw the solid sky last so opaque geometry rejects covered fragments at the depth
            // clear.
            if (view.skySphere.has_value() && view.skyCubemap != nullptr) {
                // Unjittered, unlike the scene draws' mvp: the sky's vertex entry point applies
                // jitterNdc itself so its motion pair stays unjittered. Off the temporal path the
                // jitter is zero and this is the same matrix the scene rasterised with.
                const SkyUniforms sky{.viewProj = cameraState.viewProjection,
                                      .eyePos = passUniforms.eyePos,
                                      .eyePadding = 0.0f,
                                      .preExposure = passUniforms.preExposure,
                                      .jitterNdcX = jitterNdc.x,
                                      .jitterNdcY = jitterNdc.y,
                                      .jitterPadding = 0.0f,
                                      .previousViewProj = previousCamera.viewProjection,
                                      .previousEyePos = previousCamera.position,
                                      .previousEyePosPadding = 0.0f};
                // Same pipeline switch as the scene draws above, for the same reason (spec 9).
                if (temporalEnabled) {
                    commands.bindPipeline(view.autoExposureEnabled ? *m_skyPipelineAutoMotion
                                                                   : *m_skyPipelineMotion);
                } else {
                    commands.bindPipeline(view.autoExposureEnabled ? *m_skyPipelineAuto
                                                                   : *m_skyPipeline);
                }
                // Shaders/Sky.slang/SkyAuto.slang are the only readers of this slot, so it is
                // bound here rather than with the pass's shared set.
                commands.bindTexture(kSkyTextureSlot, *view.skyCubemap);
                commands.bindFrameData(kPassUniformsSlot, sky);
                commands.drawIndexed(*view.tables.indices, view.skySphere->indexCount,
                                     view.skySphere->firstIndex);
            }
        });

    return nextVersion(sceneColor);
}

} // namespace lmx::render
