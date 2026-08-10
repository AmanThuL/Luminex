//----------------------------------------------------------------------------------------------------------------------
/// @file Renderer.cpp
/// @brief Implements frame pass declaration and renderer-owned GPU resources.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Renderer.h"

#include "Core/Assert.h"
#include "RHI/CaptureSchema.h"
#include "Render/ColorTransfer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <iterator>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace lmx::render {

namespace {

// Mirrors Shaders/ScenePass.slang's ObjectUniforms.
struct ObjectUniforms {
    glm::mat4 mvp;           // 0
    glm::mat4 model;         // 64
    glm::mat4 normalMatrix;  // 128 -- inverse transpose of model; 4x4 for one unambiguous layout
    glm::mat4 uvTransform;   // 192
    glm::vec4 albedo;        // 256
    float roughness;         // 272
    uint32_t flags;          // 276
    float metallic;          // 280
    float occlusionStrength; // 284 -- fills the register before emissive's 16-byte alignment
    glm::vec3 emissive;      // 288
    float emissivePadding;   // 300 -- the float3's tail, rounds the struct to 304
};
static_assert(sizeof(ObjectUniforms) == 304, "must match ScenePass.slang's ObjectUniforms");

// Mirrors Shaders/ShadowPass.slang's ObjectUniforms.
struct ShadowObjectUniforms {
    glm::mat4 mvp;
};
static_assert(sizeof(ShadowObjectUniforms) == 64, "must match ShadowPass.slang's ObjectUniforms");

// Mirrors Shaders/Lighting.slang's DirLight.
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
};
static_assert(sizeof(PassUniforms) == 272, "must match ScenePass.slang's PassUniforms");

// Mirrors Shaders/Sky.slang's SkyUniforms.
struct SkyUniforms {
    glm::mat4 viewProj;   // 0
    glm::vec3 eyePos;     // 64
    float eyePadding;     // 76 -- the float3's tail
    float preExposure;    // 80
    float tailPadding[3]; // 84 -- the struct's own 16-byte alignment
};
static_assert(sizeof(SkyUniforms) == 96, "must match Sky.slang's SkyUniforms");

// Mirrors Shaders/HistogramAccumulate.slang's HistogramParams. Every field is a scalar, so HLSL
// cbuffer packing (which Slang's Metal path still follows) leaves them contiguous -- no vector
// field ever forces a gap here.
struct HistogramParams {
    float preExposure;
    float logLuminanceMin;
    float logLuminanceMax;
    uint32_t width;
    uint32_t height;
};
static_assert(sizeof(HistogramParams) == 20,
              "must match HistogramAccumulate.slang's HistogramParams");

// Mirrors Shaders/ExposureResolve.slang's ExposureResolveParams.
struct ExposureResolveParams {
    float lowPercentile;
    float highPercentile;
    float targetGrey;
    float evMin;
    float evMax;
    float compensationEv;
    float logLuminanceMin;
    float logLuminanceMax;
};
static_assert(sizeof(ExposureResolveParams) == 32,
              "must match ExposureResolve.slang's ExposureResolveParams");

// Mirrors Shaders/BloomThreshold.slang's BloomThresholdParams.
struct BloomThresholdParams {
    float threshold;
    uint32_t srcWidth;
    uint32_t srcHeight;
    uint32_t dstWidth;
    uint32_t dstHeight;
};
static_assert(sizeof(BloomThresholdParams) == 20,
              "must match BloomThreshold.slang's BloomThresholdParams");

// Mirrors Shaders/BloomDownsample.slang's BloomDownsampleParams.
struct BloomDownsampleParams {
    uint32_t srcWidth;
    uint32_t srcHeight;
    uint32_t dstWidth;
    uint32_t dstHeight;
};
static_assert(sizeof(BloomDownsampleParams) == 16,
              "must match BloomDownsample.slang's BloomDownsampleParams");

// Mirrors Shaders/BloomUpsample.slang's BloomUpsampleParams.
struct BloomUpsampleParams {
    uint32_t smallWidth;
    uint32_t smallHeight;
    uint32_t dstWidth;
    uint32_t dstHeight;
};
static_assert(sizeof(BloomUpsampleParams) == 16,
              "must match BloomUpsample.slang's BloomUpsampleParams");

// Mirrors Shaders/DisplayTransform.slang's DisplayParams.
struct DisplayParams {
    float bloomIntensity;
};
static_assert(sizeof(DisplayParams) == 4, "must match DisplayTransform.slang's DisplayParams");

// ScenePass.slang's kFlagHasNormalMap.
constexpr uint32_t kFlagHasNormalMap = 1u;

// Shaders/Shadow.slang's kShadowFilterPcf / kShadowFilterPcss.
constexpr int32_t kShadowFilterPcf = 0;
constexpr int32_t kShadowFilterPcss = 1;

constexpr uint32_t kVertexBufferSlot = 0;
constexpr uint32_t kObjectUniformsSlot = 1;
constexpr uint32_t kPassUniformsSlot = 2;
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

// DisplayTransform.slang's own resource set: unrelated to the scene pass's slots above.
constexpr uint32_t kSceneColorTextureSlot = 0;
constexpr uint32_t kDisplayBloomTextureSlot = 1;
constexpr uint32_t kDisplayParamsSlot = 0;

// HistogramAccumulate.slang's slot map.
constexpr uint32_t kHistogramSceneColorSlot = 0; // texture
constexpr uint32_t kHistogramBufferSlot = 0;     // buffer
constexpr uint32_t kHistogramParamsSlot = 1;     // buffer

// ExposureResolve.slang's slot map (buffer space only).
constexpr uint32_t kResolveHistogramSlot = 0;
constexpr uint32_t kResolveExposureSlot = 1;
constexpr uint32_t kResolveParamsSlot = 2;

// BloomThreshold.slang's slot map.
constexpr uint32_t kBloomThresholdSceneColorSlot = 0; // texture
constexpr uint32_t kBloomThresholdDstSlot = 1;        // texture
constexpr uint32_t kBloomThresholdParamsSlot = 0;     // buffer

// BloomDownsample.slang's slot map (texture space only, plus its own buffer 0).
constexpr uint32_t kBloomDownsampleSrcSlot = 0;
constexpr uint32_t kBloomDownsampleDstSlot = 1;
constexpr uint32_t kBloomDownsampleParamsSlot = 0; // buffer

// BloomUpsample.slang's slot map (texture space only, plus its own buffer 0).
constexpr uint32_t kBloomUpsampleBaseSlot = 0;
constexpr uint32_t kBloomUpsampleSmallSlot = 1;
constexpr uint32_t kBloomUpsampleDstSlot = 2;
constexpr uint32_t kBloomUpsampleParamsSlot = 0; // buffer

constexpr uint32_t kHistogramBins = 256;
constexpr uint64_t kHistogramBufferSize = uint64_t{kHistogramBins} * sizeof(uint32_t);
// Wide enough to cover everything from near-black shadow detail to a strongly overexposed
// highlight without the metering formula ever needing to know a scene's real range in advance;
// HistogramAccumulate.slang and ExposureResolve.slang must agree on both.
constexpr float kExposureLogLuminanceMin = -12.0f;
constexpr float kExposureLogLuminanceMax = 4.0f;

constexpr uint32_t kComputeThreadsPerGroup2D = 8;

//======================================================================================================================
uint32_t divRoundUp(uint32_t value, uint32_t divisor) {
    return (value + divisor - 1) / divisor;
}

constexpr uint32_t kShadowMapSize = 2048;

// The 25-texel PCF radius needs slope bias across the whole kernel, not one texel. GPU
// measurements reached the unshadowed reference at 32; 64 provided no further improvement.
//
// Negative because depth is reversed: the bias has to push a caster's stored depth *away* from
// the light so the surface stops shadowing itself, and away from the light is now the smaller
// number. The magnitudes carry over unchanged -- the light's projection is orthographic, so its
// depth is linear in light-space distance and reversing it negates the slope without changing
// its size, which leaves the same 32 covering the same kernel. Tests/GpuRendererTests.cpp's
// sloped-bias case is the instrument that pins the sign.
constexpr rhi::DepthBias kShadowDepthBias{.constant = -4.0f, .slopeScale = -32.0f};

// Linear white is the neutral multiplier for every per-draw material factor.
constexpr std::array<uint8_t, 4> kWhiteTexel = {255, 255, 255, 255};
// Encoded tangent-space (0, 0, 1); bound to keep every declared slot valid.
constexpr std::array<uint8_t, 4> kFlatNormalTexel = {128, 128, 255, 255};
// Black zeroes both image-based terms when a scene carries no IBL set.
constexpr std::array<uint8_t, 4> kBlackTexel = {0, 0, 0, 255};
// The DFG fallback's (scale, bias), as RG16Float bits. Zero makes the specular reconstruction
// F0 * 0 + 0 vanish -- the matching answer for an environment that is itself black.
constexpr std::array<uint16_t, 2> kZeroDfgTexel = {0, 0};

//======================================================================================================================
rhi::Result<std::unique_ptr<rhi::Texture>> createFallbackTexture(rhi::Device& device,
                                                                 const std::array<uint8_t, 4>& rgba,
                                                                 rhi::TextureKind kind,
                                                                 std::string_view label) {
    const rhi::TextureMip mip{.data = rgba.data(), .bytesPerRow = 4};
    const uint32_t faceCount = kind == rhi::TextureKind::Cube ? 6u : 1u;
    // Cube fallbacks must cover every face with the same neutral texel.
    const std::array<rhi::TextureMip, 6> mips = {mip, mip, mip, mip, mip, mip};
    return device.createTexture({.width = 1,
                                 .height = 1,
                                 .format = rhi::Format::RGBA8Unorm,
                                 .kind = kind,
                                 .sampled = true,
                                 .label = label},
                                std::span{mips.data(), faceCount});
}

//======================================================================================================================
// The DFG fallback needs its own creator: it is the one fallback that is neither RGBA8 nor a cube,
// because the split-sum table it stands in for is RG16Float and a shader reading it as anything
// else would find its two channels in the wrong place.
rhi::Result<std::unique_ptr<rhi::Texture>> createZeroDfgTexture(rhi::Device& device) {
    const rhi::TextureMip mip{.data = kZeroDfgTexel.data(), .bytesPerRow = sizeof(kZeroDfgTexel)};
    return device.createTexture({.width = 1,
                                 .height = 1,
                                 .format = rhi::Format::RG16Float,
                                 .sampled = true,
                                 .label = "lmx.render.zeroDfgFallback"},
                                std::span{&mip, 1});
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
// offsetof and sizeof keep capture metadata tied to the CPU mirrors; field names and types follow
// the shader-facing layout and omit CPU-only padding.
void registerUniformLayoutsForCapture() {
    using rhi::debug::CaptureSchema;
    using rhi::debug::SchemaUniformField;

    CaptureSchema& schema = CaptureSchema::instance();

    schema.registerUniformStruct(
        {.name = "ObjectUniforms",
         .slot = kObjectUniformsSlot,
         .sizeBytes = sizeof(ObjectUniforms),
         .fields = {{"mvp", offsetof(ObjectUniforms, mvp), "float4x4"},
                    {"model", offsetof(ObjectUniforms, model), "float4x4"},
                    {"normalMatrix", offsetof(ObjectUniforms, normalMatrix), "float4x4"},
                    {"uvTransform", offsetof(ObjectUniforms, uvTransform), "float4x4"},
                    {"albedo", offsetof(ObjectUniforms, albedo), "float4"},
                    {"roughness", offsetof(ObjectUniforms, roughness), "float"},
                    {"flags", offsetof(ObjectUniforms, flags), "uint"},
                    {"metallic", offsetof(ObjectUniforms, metallic), "float"},
                    {"occlusionStrength", offsetof(ObjectUniforms, occlusionStrength), "float"},
                    {"emissive", offsetof(ObjectUniforms, emissive), "float3"}}});

    schema.registerUniformStruct(
        {.name = "ShadowObjectUniforms",
         .slot = kObjectUniformsSlot,
         .sizeBytes = sizeof(ShadowObjectUniforms),
         .fields = {{"mvp", offsetof(ShadowObjectUniforms, mvp), "float4x4"}}});

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
                    {"preExposure", offsetof(SkyUniforms, preExposure), "float"}}});
}

//======================================================================================================================
ShadowMatrices fitShadowOrtho(const glm::vec4& boundingSphere, const glm::vec3& lightDir) {
    const glm::vec3 center{boundingSphere};
    const float radius = boundingSphere.w;
    LMX_ASSERT(radius > 0.0f, "fitShadowOrtho: the bounding sphere's radius must be positive");
    LMX_ASSERT(glm::length(lightDir) > 0.0f,
               "fitShadowOrtho: the light direction must not be the zero vector");

    const glm::vec3 direction = glm::normalize(lightDir);
    // Offset from the sphere center so translated scenes retain the same fitted light volume.
    const glm::vec3 eye = center - 2.0f * radius * direction;

    // Avoid lookAt's degenerate cross product when light direction is parallel to world up.
    constexpr glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};
    const glm::vec3 up =
        std::abs(glm::dot(direction, kWorldUp)) > 0.999f ? glm::vec3{0.0f, 0.0f, 1.0f} : kWorldUp;
    const glm::mat4 lightView = glm::lookAtRH(eye, center, up);

    const glm::vec3 centerLS = glm::vec3(lightView * glm::vec4(center, 1.0f));
    // Right-handed view space looks down -z, so positive near/far distances use -centerLS.z: the
    // frustum runs from r to 3r about a centre 2r out.
    //
    // Reversed to match the camera (Camera.cpp): the near plane maps to 1, the far plane to 0, so
    // "nearer to the light" is the numerically larger depth throughout -- the shadow map, the
    // GreaterEqual comparison sampler that reads it, and the Greater depth test that fills it all
    // agree on one direction. The reversal is expressed by handing orthoRH_ZO its far distance as
    // near and vice versa, which is exactly a z negate-and-offset applied to the standard form and
    // leaves the xy fit untouched.
    const float nearDistance = -centerLS.z - radius;
    const float farDistance = -centerLS.z + radius;
    const glm::mat4 lightProj =
        glm::orthoRH_ZO(centerLS.x - radius, centerLS.x + radius, centerLS.y - radius,
                        centerLS.y + radius, farDistance, nearDistance);

    // Map NDC xy to texture coordinates and flip y; Metal depth already uses [0, 1].
    glm::mat4 ndcToTexcoord{1.0f};
    ndcToTexcoord[0][0] = 0.5f;
    ndcToTexcoord[1][1] = -0.5f;
    ndcToTexcoord[3][0] = 0.5f;
    ndcToTexcoord[3][1] = 0.5f;

    const glm::mat4 viewProj = lightProj * lightView;
    return {.viewProj = viewProj, .shadowTransform = ndcToTexcoord * viewProj};
}

//======================================================================================================================
float resolveAutoExposureOverride(bool reset, float manualExposureEv,
                                  float previousResolvedExposure) {
    return reset ? std::exp2(manualExposureEv) : previousResolvedExposure;
}

//======================================================================================================================
rhi::Result<std::unique_ptr<Renderer>> Renderer::create(rhi::Device& device, uint32_t width,
                                                        uint32_t height, bool cpuReadback) {
    LMX_ASSERT(width > 0 && height > 0, "Renderer::create: width and height must be non-zero");

    // Registration is idempotent and keeps capture startup independent of Renderer state.
    registerUniformLayoutsForCapture();

    std::unique_ptr<Renderer> self(new Renderer(device, cpuReadback));

    // Separate libraries prevent Slang from attaching the scene resource set to the depth-only
    // entry points.
    if (auto library = device.loadShaderLibrary("Shaders/ScenePass"); library) {
        self->m_sceneLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    if (auto library = device.loadShaderLibrary("Shaders/ShadowPass"); library) {
        self->m_shadowLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    if (auto library = device.loadShaderLibrary("Shaders/Sky"); library) {
        self->m_skyLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    if (auto library = device.loadShaderLibrary("Shaders/DisplayTransform"); library) {
        self->m_displayLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    if (auto library = device.loadShaderLibrary("Shaders/HistogramAccumulate"); library) {
        self->m_histogramLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    if (auto library = device.loadShaderLibrary("Shaders/ExposureResolve"); library) {
        self->m_exposureResolveLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    if (auto library = device.loadShaderLibrary("Shaders/BloomThreshold"); library) {
        self->m_bloomThresholdLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    if (auto library = device.loadShaderLibrary("Shaders/BloomDownsample"); library) {
        self->m_bloomDownsampleLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    if (auto library = device.loadShaderLibrary("Shaders/BloomUpsample"); library) {
        self->m_bloomUpsampleLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }

    const auto makeScenePipeline = [&](rhi::FillMode fill, const char* label) {
        return device.createGraphicsPipeline({.library = self->m_sceneLibrary.get(),
                                              .vertexEntry = "vertexMain",
                                              .fragmentEntry = "fragmentMain",
                                              .colorFormat = kSceneColorFormat,
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
    if (auto pipeline = makeScenePipeline(rhi::FillMode::Solid, "lmx.render.scenePipeline");
        pipeline) {
        self->m_scenePipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    // Fill mode is baked into Metal pipeline state; compile both variants once.
    if (auto pipeline =
            makeScenePipeline(rhi::FillMode::Wireframe, "lmx.render.sceneWireframePipeline");
        pipeline) {
        self->m_sceneWireframePipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }

    // Unknown color format matches the depth-only pass and its void fragment output.
    if (auto pipeline =
            device.createGraphicsPipeline({.library = self->m_shadowLibrary.get(),
                                           .vertexEntry = "vertexMain",
                                           .fragmentEntry = "fragmentMain",
                                           .colorFormat = rhi::Format::Unknown,
                                           .depthFormat = rhi::Format::D32Float,
                                           .depthTestEnable = true,
                                           .depthWriteEnable = true,
                                           // Store the light-facing surface, not the back face.
                                           .cullMode = rhi::CullMode::Back,
                                           // fitShadowOrtho is reversed too, so the surface
                                           // nearest the light is the largest depth and the map
                                           // keeps what compares Greater against its 0 clear.
                                           .depthCompare = rhi::DepthCompare::Greater,
                                           .depthBias = kShadowDepthBias,
                                           .label = "lmx.render.shadowPipeline"});
        pipeline) {
        self->m_shadowPipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }

    // Sky vertices force z == 0, the reversed far plane: use GreaterEqual so they survive the
    // pass's own 0 clear, render inside faces, and avoid rewriting the unchanged depth value.
    if (auto pipeline =
            device.createGraphicsPipeline({.library = self->m_skyLibrary.get(),
                                           .vertexEntry = "vertexMain",
                                           .fragmentEntry = "fragmentMain",
                                           .colorFormat = kSceneColorFormat,
                                           .depthFormat = rhi::Format::D32Float,
                                           .depthTestEnable = true,
                                           .depthWriteEnable = false,
                                           .cullMode = rhi::CullMode::None,
                                           .depthCompare = rhi::DepthCompare::GreaterEqual,
                                           .label = "lmx.render.skyPipeline"});
        pipeline) {
        self->m_skyPipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }

    // A fullscreen triangle over an already-rasterised image: no depth to test against and no
    // face to cull, since the one primitive covers the target by construction.
    if (auto pipeline = device.createGraphicsPipeline({.library = self->m_displayLibrary.get(),
                                                       .vertexEntry = "vertexMain",
                                                       .fragmentEntry = "fragmentMain",
                                                       .colorFormat = kDisplayFormat,
                                                       .depthFormat = rhi::Format::Unknown,
                                                       .cullMode = rhi::CullMode::None,
                                                       .label = "lmx.render.displayPipeline"});
        pipeline) {
        self->m_displayPipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }

    if (auto pipeline = device.createComputePipeline(
            {.library = self->m_histogramLibrary.get(),
             .computeEntry = "computeHistogramAccumulate",
             .threadsPerThreadgroup = {kComputeThreadsPerGroup2D, kComputeThreadsPerGroup2D, 1},
             .label = "lmx.render.histogramPipeline"});
        pipeline) {
        self->m_histogramPipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline =
            device.createComputePipeline({.library = self->m_exposureResolveLibrary.get(),
                                          .computeEntry = "computeExposureResolve",
                                          .threadsPerThreadgroup = {1, 1, 1},
                                          .label = "lmx.render.exposureResolvePipeline"});
        pipeline) {
        self->m_exposureResolvePipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline = device.createComputePipeline(
            {.library = self->m_bloomThresholdLibrary.get(),
             .computeEntry = "computeBloomThreshold",
             .threadsPerThreadgroup = {kComputeThreadsPerGroup2D, kComputeThreadsPerGroup2D, 1},
             .label = "lmx.render.bloomThresholdPipeline"});
        pipeline) {
        self->m_bloomThresholdPipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline = device.createComputePipeline(
            {.library = self->m_bloomDownsampleLibrary.get(),
             .computeEntry = "computeBloomDownsample",
             .threadsPerThreadgroup = {kComputeThreadsPerGroup2D, kComputeThreadsPerGroup2D, 1},
             .label = "lmx.render.bloomDownsamplePipeline"});
        pipeline) {
        self->m_bloomDownsamplePipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline = device.createComputePipeline(
            {.library = self->m_bloomUpsampleLibrary.get(),
             .computeEntry = "computeBloomUpsample",
             .threadsPerThreadgroup = {kComputeThreadsPerGroup2D, kComputeThreadsPerGroup2D, 1},
             .label = "lmx.render.bloomUpsamplePipeline"});
        pipeline) {
        self->m_bloomUpsamplePipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }

    // The shadow map transitions from depth attachment to sampled texture each frame.
    if (auto shadowMap = device.createTexture({.width = kShadowMapSize,
                                               .height = kShadowMapSize,
                                               .format = rhi::Format::D32Float,
                                               .renderTarget = true,
                                               .sampled = true,
                                               .label = "lmx.render.shadowMap"});
        shadowMap) {
        self->m_shadowMap = std::move(*shadowMap);
    } else {
        return std::unexpected(shadowMap.error());
    }

    if (auto texture = createFallbackTexture(device, kWhiteTexel, rhi::TextureKind::Tex2D,
                                             "lmx.render.whiteFallback");
        texture) {
        self->m_whiteTexture = std::move(*texture);
    } else {
        return std::unexpected(texture.error());
    }
    if (auto texture = createFallbackTexture(device, kFlatNormalTexel, rhi::TextureKind::Tex2D,
                                             "lmx.render.flatNormalFallback");
        texture) {
        self->m_flatNormalTexture = std::move(*texture);
    } else {
        return std::unexpected(texture.error());
    }
    if (auto texture = createFallbackTexture(device, kBlackTexel, rhi::TextureKind::Cube,
                                             "lmx.render.blackCubeFallback");
        texture) {
        self->m_blackCubeTexture = std::move(*texture);
    } else {
        return std::unexpected(texture.error());
    }
    if (auto texture = createZeroDfgTexture(device); texture) {
        self->m_zeroDfgTexture = std::move(*texture);
    } else {
        return std::unexpected(texture.error());
    }
    // Zero in kSceneColorFormat so "composite exactly x + 0" holds even without the intensity
    // uniform's own zero (Shaders/DisplayTransform.slang).
    {
        const std::array<uint16_t, 4> kZeroHalf4 = {0, 0, 0, 0};
        const rhi::TextureMip mip{.data = kZeroHalf4.data(), .bytesPerRow = sizeof(kZeroHalf4)};
        if (auto texture = device.createTexture({.width = 1,
                                                 .height = 1,
                                                 .format = kSceneColorFormat,
                                                 .sampled = true,
                                                 .label = "lmx.render.blackBloomFallback"},
                                                std::span{&mip, 1});
            texture) {
            self->m_blackBloomFallback = std::move(*texture);
        } else {
            return std::unexpected(texture.error());
        }
    }

    if (auto buffer = device.createBuffer({.size = kHistogramBufferSize,
                                           .storageRead = true,
                                           .storageWrite = true,
                                           .label = "lmx.render.histogramBuffer"},
                                          nullptr);
        buffer) {
        self->m_histogramBuffer = std::move(*buffer);
    } else {
        return std::unexpected(buffer.error());
    }
    // One float; cpuReadback lets the App (main.cpp) read the resolved value back for the next
    // frame's SceneView::autoExposureOverride (spec 9's feedback loop, realised as a CPU sync
    // boundary rather than a same-timeline GPU buffer read -- see Renderer.h's exposureBuffer()).
    {
        constexpr float kInitialExposure = 1.0f;
        if (auto buffer = device.createBuffer({.size = sizeof(float),
                                               .storageRead = true,
                                               .storageWrite = true,
                                               .cpuReadback = true,
                                               .label = "lmx.render.exposureBuffer"},
                                              &kInitialExposure);
            buffer) {
            self->m_exposureBuffer = std::move(*buffer);
        } else {
            return std::unexpected(buffer.error());
        }
    }

    if (auto sampler = device.createSampler({.filter = rhi::FilterMode::Linear,
                                             .addressMode = rhi::AddressMode::Wrap,
                                             .maxAnisotropy = 16,
                                             .label = "lmx.render.linearSampler"});
        sampler) {
        self->m_linearSampler = std::move(*sampler);
    } else {
        return std::unexpected(sampler.error());
    }
    // GreaterEqual is the reversed-Z compare: the sampler answers "lit" where the receiver's own
    // depth is at least the stored one, because nearer to the light is now the larger number.
    // Clamp extends the 0.0 clear outside the fitted shadow footprint, and every receiver depth
    // clears that bar, so that region stays lit exactly as it did under the 1.0 clear before.
    if (auto sampler = device.createSampler({.filter = rhi::FilterMode::Linear,
                                             .addressMode = rhi::AddressMode::Clamp,
                                             .maxAnisotropy = 16,
                                             .compare = rhi::CompareFunc::GreaterEqual,
                                             .label = "lmx.render.shadowSampler"});
        sampler) {
        self->m_shadowSampler = std::move(*sampler);
    } else {
        return std::unexpected(sampler.error());
    }

    // Clamped, not wrapped: the IBL set is read at the very edge of its domain -- the DFG table at
    // N.V = 1 and at roughness 1 -- where a wrapping sampler would fold the opposite edge's texels
    // into the result. Linear filtering carries the mip filter the prefiltered chain is sampled
    // across; no anisotropy, because neither lookup has a screen-space footprint to be anisotropic
    // about.
    if (auto sampler = self->m_device.createSampler({.filter = rhi::FilterMode::Linear,
                                                     .addressMode = rhi::AddressMode::Clamp,
                                                     .label = "lmx.render.iblSampler"});
        sampler) {
        self->m_iblSampler = std::move(*sampler);
    } else {
        return std::unexpected(sampler.error());
    }

    if (auto targets = self->resize(width, height); !targets) {
        return std::unexpected(targets.error());
    }
    return self;
}

//======================================================================================================================
rhi::Result<void> Renderer::resize(uint32_t width, uint32_t height) {
    LMX_ASSERT(width > 0 && height > 0, "Renderer::resize: width and height must be non-zero");

    // Scene-linear radiance: rendered into by the scene and sky passes, sampled by the display
    // pass. Readable on the same terms as the display target, because a caller that wants to
    // inspect radiance rather than the picture has nowhere else to read it from.
    auto hdrColor = m_device.createTexture({.width = width,
                                            .height = height,
                                            .format = kSceneColorFormat,
                                            .renderTarget = true,
                                            .sampled = true,
                                            .cpuReadback = m_cpuReadback,
                                            .label = "lmx.render.sceneColorHdr"});
    if (!hdrColor) {
        return std::unexpected(hdrColor.error());
    }
    // The display transform's output: what the viewport samples and a screenshot reads back.
    auto color = m_device.createTexture({.width = width,
                                         .height = height,
                                         .format = kDisplayFormat,
                                         .renderTarget = true,
                                         .sampled = true,
                                         .cpuReadback = m_cpuReadback,
                                         .label = "lmx.render.displayColor"});
    if (!color) {
        return std::unexpected(color.error());
    }
    // Sampled as well as rendered into: the frame's own depth is the only record of where its
    // geometry is, and a caller that wants to read a distance back out of the image -- the depth
    // reconstruction the reversed projection is pinned by, and any later pass that shades from
    // depth -- has nowhere else to get it. It costs the driver's lossless depth compression,
    // which is why the flag is stated here rather than left on by habit.
    auto depth = m_device.createTexture({.width = width,
                                         .height = height,
                                         .format = rhi::Format::D32Float,
                                         .renderTarget = true,
                                         .sampled = true,
                                         .label = "lmx.render.sceneDepth"});
    if (!depth) {
        return std::unexpected(depth.error());
    }

    // Swap the targets only after every allocation succeeds.
    m_hdrColor = std::move(*hdrColor);
    m_color = std::move(*color);
    m_depth = std::move(*depth);
    m_width = width;
    m_height = height;
    return {};
}

//======================================================================================================================
GraphTexture Renderer::declarePasses(RenderGraph& graph, rhi::CommandList& commands,
                                     const Camera& camera, const SceneView& view) {
    LMX_ASSERT(m_hdrColor && m_color && m_depth && m_shadowMap,
               "Renderer::declarePasses: targets are missing -- create() failed");
    LMX_ASSERT(view.boundingSphere.w > 0.0f,
               "SceneView::boundingSphere needs a positive radius -- it is what the shadow "
               "frustum is fitted to");

    const ShadowMatrices shadow = fitShadowOrtho(view.boundingSphere, view.lights[0].direction);

    // The formats are declared here because the graph checks attachment roles against them and
    // rhi::Texture does not report its own; the named constants are the same ones resize() and
    // create() built these from, so the two statements cannot drift apart.
    const GraphTexture shadowMap =
        graph.importTexture(*m_shadowMap, rhi::Format::D32Float, "lmx.render.shadowMap");
    const GraphTexture sceneColor =
        graph.importTexture(*m_hdrColor, kSceneColorFormat, "lmx.render.sceneColorHdr");
    const GraphTexture displayColor =
        graph.importTexture(*m_color, kDisplayFormat, "lmx.render.displayColor");
    const GraphTexture sceneDepth =
        graph.importTexture(*m_depth, rhi::Format::D32Float, "lmx.render.sceneDepth");

    PassDesc shadowDesc;
    // 0 is the reversed far plane: nothing in the light's frustum is farther, so every caster's
    // Greater test passes against a cleared texel.
    shadowDesc.depth = DepthAttachment{
        .handle = shadowMap, .load = LoadOp::Clear, .store = StoreOp::Store, .clearDepth = 0.0f};
    graph.addPass("lmx.pass.shadow", std::move(shadowDesc),
                  [this, &commands, view, lightViewProj = shadow.viewProj](const PassResources&) {
                      commands.bindPipeline(*m_shadowPipeline);
                      for (const DrawItem& item : view.items) {
                          LMX_ASSERT(item.mesh != nullptr, "DrawItem.mesh must not be null");
                          const ShadowObjectUniforms uniforms{.mvp = lightViewProj * item.model};
                          commands.bindBuffer(kVertexBufferSlot, *item.mesh->vertexBuffer);
                          commands.setUniforms(kObjectUniformsSlot, &uniforms, sizeof(uniforms));
                          commands.drawIndexed(*item.mesh->indexBuffer, item.mesh->indexCount);
                      }
                  });

    const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    const glm::mat4 viewProj = camera.projectionMatrix(aspect) * camera.viewMatrix();

    // One stop is one doubling, so the slider's unit becomes a multiply here and every fragment
    // applies it to its linear output. Manual mode (the default) keeps exactly this expression,
    // unchanged from before auto-exposure existed -- parity with pre-M5 output when auto is off
    // therefore follows from this branch never being taken, not from the two branches agreeing
    // numerically. Auto mode substitutes the App's own readback of the exposure buffer (spec 9);
    // see SceneView::autoExposureOverride and Renderer::exposureBuffer().
    const float preExposure =
        view.autoExposureEnabled ? view.autoExposureOverride : std::exp2(view.exposureEv);

    PassUniforms passUniforms{};
    passUniforms.viewProj = viewProj;
    passUniforms.shadowTransform = shadow.shadowTransform;
    passUniforms.eyePos = camera.position;
    passUniforms.time = timeSeconds;
    passUniforms.preExposure = preExposure;
    for (size_t i = 0; i < std::size(passUniforms.lights); ++i) {
        passUniforms.lights[i] = toUniform(view.lights[i]);
    }
    passUniforms.shadowFilter =
        view.shadowFilter == ShadowFilter::PCSS ? kShadowFilterPcss : kShadowFilterPcf;

    const GraphTexture shadowRead = nextVersion(shadowMap);

    // The clear has to be the value a fragment writing that colour would have produced, or the
    // background and the geometry would disagree about what space the target holds. That means
    // both steps a fragment takes: the authored display-space colour decodes to linear (once,
    // here), and it is pre-exposed like everything else -- without the second multiply the
    // background would sit still while an exposure change moved every shaded pixel.
    const glm::vec3 clearLinear =
        srgbToLinear(glm::vec3(clearColor[0], clearColor[1], clearColor[2])) * preExposure;

    PassDesc sceneDesc;
    sceneDesc.textureReads.push_back(shadowRead);
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
    graph.addPass(
        "lmx.pass.scene", std::move(sceneDesc),
        [this, &commands, view, passUniforms, viewProj,
         shadowRead](const PassResources& resources) {
            // Resolved rather than captured: the graph hands over the shadow map only because this
            // pass declared reading it, which is what ordered it after the pass that wrote it.
            const GraphResult<rhi::Texture*> shadowMapTexture = resources.texture(shadowRead);
            LMX_ASSERT(shadowMapTexture.has_value(), shadowMapTexture.error().message);

            commands.bindPipeline(view.wireframe ? *m_sceneWireframePipeline : *m_scenePipeline);
            commands.bindSampler(kLinearSamplerSlot, *m_linearSampler);
            commands.bindSampler(kShadowSamplerSlot, *m_shadowSampler);
            commands.bindSampler(kIblSamplerSlot, *m_iblSampler);
            commands.bindTexture(kShadowTextureSlot, **shadowMapTexture);
            // The pass-wide IBL set. Each slot falls back independently, so a SceneView that
            // carries no environment still renders -- with both image-based terms at zero.
            commands.bindTexture(kIrradianceTextureSlot, view.irradiance != nullptr
                                                             ? *view.irradiance
                                                             : *m_blackCubeTexture);
            commands.bindTexture(kPrefilteredEnvTextureSlot, view.prefilteredEnv != nullptr
                                                                 ? *view.prefilteredEnv
                                                                 : *m_blackCubeTexture);
            commands.bindTexture(kDfgLutTextureSlot,
                                 view.dfgLut != nullptr ? *view.dfgLut : *m_zeroDfgTexture);
            commands.setUniforms(kPassUniformsSlot, &passUniforms, sizeof(passUniforms));

            for (const DrawItem& item : view.items) {
                const Material& material = item.material;
                ObjectUniforms uniforms{};
                uniforms.mvp = viewProj * item.model;
                uniforms.model = item.model;
                // The inverse transpose, computed here rather than in the vertex shader because it
                // is one value per draw and inverting a matrix per vertex would pay for it tens of
                // thousands of times over. Taken on the 3x3 linear part: translation does not act
                // on a direction, and inverting the full 4x4 would only divide it back out again.
                uniforms.normalMatrix =
                    glm::mat4(glm::transpose(glm::inverse(glm::mat3(item.model))));
                uniforms.uvTransform = material.uvTransform;
                uniforms.albedo = material.albedo;
                uniforms.roughness = material.roughness;
                uniforms.flags = material.normalMap != nullptr ? kFlagHasNormalMap : 0u;
                uniforms.metallic = material.metallic;
                uniforms.occlusionStrength = material.occlusionStrength;
                uniforms.emissive = material.emissive;

                commands.bindTexture(kDiffuseTextureSlot, material.diffuse != nullptr
                                                              ? *material.diffuse
                                                              : *m_whiteTexture);
                commands.bindTexture(kNormalTextureSlot, material.normalMap != nullptr
                                                             ? *material.normalMap
                                                             : *m_flatNormalTexture);
                // The shared white fallback lets each factor pass through unchanged when a
                // material carries no map -- white is the identity for all three.
                commands.bindTexture(kMetallicRoughnessTextureSlot,
                                     material.metallicRoughness != nullptr
                                         ? *material.metallicRoughness
                                         : *m_whiteTexture);
                commands.bindTexture(kOcclusionTextureSlot, material.occlusion != nullptr
                                                                ? *material.occlusion
                                                                : *m_whiteTexture);
                commands.bindTexture(kEmissiveTextureSlot, material.emissiveMap != nullptr
                                                               ? *material.emissiveMap
                                                               : *m_whiteTexture);
                commands.bindBuffer(kVertexBufferSlot, *item.mesh->vertexBuffer);
                // setUniforms copies into transient storage before the next draw rebinds the slot.
                commands.setUniforms(kObjectUniformsSlot, &uniforms, sizeof(uniforms));
                commands.drawIndexed(*item.mesh->indexBuffer, item.mesh->indexCount);
            }

            // Draw the solid sky last so opaque geometry rejects covered fragments at the depth
            // clear.
            if (view.skySphere != nullptr && view.skyCubemap != nullptr) {
                const SkyUniforms sky{.viewProj = viewProj,
                                      .eyePos = passUniforms.eyePos,
                                      .eyePadding = 0.0f,
                                      .preExposure = passUniforms.preExposure,
                                      .tailPadding = {}};
                commands.bindPipeline(*m_skyPipeline);
                // Shaders/Sky.slang is the only reader of this slot, so it is bound here rather
                // than with the pass's shared set.
                commands.bindTexture(kSkyTextureSlot, *view.skyCubemap);
                commands.bindBuffer(kVertexBufferSlot, *view.skySphere->vertexBuffer);
                commands.setUniforms(kPassUniformsSlot, &sky, sizeof(sky));
                commands.drawIndexed(*view.skySphere->indexBuffer, view.skySphere->indexCount);
            }
        });

    const GraphTexture sceneColorRead = nextVersion(sceneColor);
    const uint32_t sceneWidth = m_width;
    const uint32_t sceneHeight = m_height;

    // ---- Exposure feedback (spec 9). Declared every frame; only exported when auto-exposure is
    // on, so dead-pass culling drops the whole histogram -> resolve chain when it is off. Manual
    // mode's shading above never reads any of this -- preExposure was already resolved by the
    // ternary before the scene pass was declared.
    const GraphBuffer histogramImport =
        graph.importBuffer(*m_histogramBuffer, "lmx.render.histogramBuffer");
    const GraphBuffer exposureImport =
        graph.importBuffer(*m_exposureBuffer, "lmx.render.exposureBuffer");

    CopyPassDesc histogramClearDesc;
    histogramClearDesc.bufferDestinations.push_back(histogramImport);
    graph.addCopyPass("lmx.pass.exposure.clearHistogram", std::move(histogramClearDesc),
                      [&commands, histogramImport](const PassResources& resources) {
                          const GraphResult<rhi::Buffer*> histogram =
                              resources.buffer(histogramImport);
                          LMX_ASSERT(histogram.has_value(), histogram.error().message);
                          commands.fillBuffer(**histogram, 0, kHistogramBufferSize, 0);
                      });
    const GraphBuffer histogramCleared = nextVersion(histogramImport);

    ComputePassDesc histogramDesc;
    histogramDesc.textureReads.push_back(sceneColorRead);
    histogramDesc.bufferWrites.push_back(histogramCleared);
    graph.addComputePass(
        "lmx.pass.exposure.histogram", std::move(histogramDesc),
        [this, &commands, sceneColorRead, histogramCleared, preExposure, sceneWidth,
         sceneHeight](const PassResources& resources) {
            const GraphResult<rhi::Texture*> scene = resources.texture(sceneColorRead);
            LMX_ASSERT(scene.has_value(), scene.error().message);
            const GraphResult<rhi::Buffer*> histogram = resources.buffer(histogramCleared);
            LMX_ASSERT(histogram.has_value(), histogram.error().message);

            const HistogramParams params{.preExposure = preExposure,
                                         .logLuminanceMin = kExposureLogLuminanceMin,
                                         .logLuminanceMax = kExposureLogLuminanceMax,
                                         .width = sceneWidth,
                                         .height = sceneHeight};
            commands.bindComputePipeline(*m_histogramPipeline);
            commands.bindTexture(kHistogramSceneColorSlot, **scene);
            commands.bindStorageBuffer(kHistogramBufferSlot, **histogram,
                                       rhi::StorageAccess::ReadWrite);
            commands.setUniforms(kHistogramParamsSlot, &params, sizeof(params));
            commands.dispatch(divRoundUp(sceneWidth, kComputeThreadsPerGroup2D),
                              divRoundUp(sceneHeight, kComputeThreadsPerGroup2D), 1);
        });
    const GraphBuffer histogramFinal = nextVersion(histogramCleared);

    ComputePassDesc resolveDesc;
    resolveDesc.bufferReads.push_back(histogramFinal);
    resolveDesc.bufferWrites.push_back(exposureImport);
    graph.addComputePass(
        "lmx.pass.exposure.resolve", std::move(resolveDesc),
        [this, &commands, histogramFinal, exposureImport, view](const PassResources& resources) {
            const GraphResult<rhi::Buffer*> histogram = resources.buffer(histogramFinal);
            LMX_ASSERT(histogram.has_value(), histogram.error().message);
            const GraphResult<rhi::Buffer*> exposure = resources.buffer(exposureImport);
            LMX_ASSERT(exposure.has_value(), exposure.error().message);

            const ExposureResolveParams params{.lowPercentile = view.exposureLowPercentile,
                                               .highPercentile = view.exposureHighPercentile,
                                               .targetGrey = view.exposureTargetGrey,
                                               .evMin = view.exposureEvMin,
                                               .evMax = view.exposureEvMax,
                                               .compensationEv = view.exposureCompensationEv,
                                               .logLuminanceMin = kExposureLogLuminanceMin,
                                               .logLuminanceMax = kExposureLogLuminanceMax};
            commands.bindComputePipeline(*m_exposureResolvePipeline);
            commands.bindStorageBuffer(kResolveHistogramSlot, **histogram,
                                       rhi::StorageAccess::Read);
            commands.bindStorageBuffer(kResolveExposureSlot, **exposure, rhi::StorageAccess::Write);
            commands.setUniforms(kResolveParamsSlot, &params, sizeof(params));
            commands.dispatch(1, 1, 1);
        });
    const GraphBuffer exposureResolved = nextVersion(exposureImport);
    if (view.autoExposureEnabled) {
        graph.exportBuffer(exposureResolved);
    }

    // ---- Bloom (spec 10). Two graph-created transients: `bloomChain`'s two mips hold the
    // threshold and one downsample step, and `bloomBlur`'s one mip holds the upsample-accumulate
    // result -- a second transient rather than accumulating into bloomChain in place, because one
    // compute pass may read and write one texture only through disjoint ranges (spec 6), and the
    // accumulate step's inputs (bloomChain's two mips) and output would otherwise name overlapping
    // ranges of one resource. BloomUpsample.slang's header carries the same reasoning. Declared
    // every frame; only the display pass's read of bloomBlur is conditional, so dead-pass culling
    // drops threshold/downsample/upsample together when bloom is off.
    const uint32_t bloomWidth = std::max(sceneWidth / 2u, 1u);
    const uint32_t bloomHeight = std::max(sceneHeight / 2u, 1u);
    const uint32_t bloomSmallWidth = std::max(bloomWidth / 2u, 1u);
    const uint32_t bloomSmallHeight = std::max(bloomHeight / 2u, 1u);

    const GraphTexture bloomChain = graph.createTexture({.width = bloomWidth,
                                                         .height = bloomHeight,
                                                         .format = kSceneColorFormat,
                                                         .mipLevels = 2,
                                                         .storageRead = true,
                                                         .storageWrite = true},
                                                        "lmx.render.bloomChain");
    const GraphTexture bloomBlur = graph.createTexture({.width = bloomWidth,
                                                        .height = bloomHeight,
                                                        .format = kSceneColorFormat,
                                                        .mipLevels = 1,
                                                        .storageRead = true,
                                                        .storageWrite = true},
                                                       "lmx.render.bloomBlur");
    static constexpr rhi::TextureSubresourceRange kBloomMip0{.baseMipLevel = 0, .mipLevelCount = 1};
    static constexpr rhi::TextureSubresourceRange kBloomMip1{.baseMipLevel = 1, .mipLevelCount = 1};

    ComputePassDesc thresholdDesc;
    thresholdDesc.textureReads.push_back(sceneColorRead);
    thresholdDesc.textureWrites.push_back(TextureUseDesc(bloomChain, kBloomMip0));
    const float bloomThreshold = view.bloomThreshold;
    graph.addComputePass(
        "lmx.pass.bloom.threshold", std::move(thresholdDesc),
        [this, &commands, sceneColorRead, bloomChain, sceneWidth, sceneHeight, bloomWidth,
         bloomHeight, bloomThreshold](const PassResources& resources) {
            const GraphResult<rhi::Texture*> scene = resources.texture(sceneColorRead);
            LMX_ASSERT(scene.has_value(), scene.error().message);
            const GraphResult<rhi::Texture*> chain = resources.texture(bloomChain);
            LMX_ASSERT(chain.has_value(), chain.error().message);

            const BloomThresholdParams params{.threshold = bloomThreshold,
                                              .srcWidth = sceneWidth,
                                              .srcHeight = sceneHeight,
                                              .dstWidth = bloomWidth,
                                              .dstHeight = bloomHeight};
            commands.bindComputePipeline(*m_bloomThresholdPipeline);
            commands.bindTexture(kBloomThresholdSceneColorSlot, **scene);
            commands.bindStorageTexture(kBloomThresholdDstSlot, **chain,
                                        rhi::TextureViewDesc{.range = kBloomMip0},
                                        rhi::StorageAccess::Write);
            commands.setUniforms(kBloomThresholdParamsSlot, &params, sizeof(params));
            commands.dispatch(divRoundUp(bloomWidth, kComputeThreadsPerGroup2D),
                              divRoundUp(bloomHeight, kComputeThreadsPerGroup2D), 1);
        });
    const GraphTexture bloomAfterThreshold = nextVersion(bloomChain);

    ComputePassDesc downsampleDesc;
    downsampleDesc.textureReads.push_back(TextureUseDesc(bloomAfterThreshold, kBloomMip0));
    downsampleDesc.textureWrites.push_back(TextureUseDesc(bloomAfterThreshold, kBloomMip1));
    graph.addComputePass(
        "lmx.pass.bloom.downsample", std::move(downsampleDesc),
        [this, &commands, bloomAfterThreshold, bloomWidth, bloomHeight, bloomSmallWidth,
         bloomSmallHeight](const PassResources& resources) {
            const GraphResult<rhi::Texture*> chain = resources.texture(bloomAfterThreshold);
            LMX_ASSERT(chain.has_value(), chain.error().message);

            const BloomDownsampleParams params{.srcWidth = bloomWidth,
                                               .srcHeight = bloomHeight,
                                               .dstWidth = bloomSmallWidth,
                                               .dstHeight = bloomSmallHeight};
            commands.bindComputePipeline(*m_bloomDownsamplePipeline);
            commands.bindStorageTexture(kBloomDownsampleSrcSlot, **chain,
                                        rhi::TextureViewDesc{.range = kBloomMip0},
                                        rhi::StorageAccess::Read);
            commands.bindStorageTexture(kBloomDownsampleDstSlot, **chain,
                                        rhi::TextureViewDesc{.range = kBloomMip1},
                                        rhi::StorageAccess::Write);
            commands.setUniforms(kBloomDownsampleParamsSlot, &params, sizeof(params));
            commands.dispatch(divRoundUp(bloomSmallWidth, kComputeThreadsPerGroup2D),
                              divRoundUp(bloomSmallHeight, kComputeThreadsPerGroup2D), 1);
        });
    const GraphTexture bloomChainFinal = nextVersion(bloomAfterThreshold);

    ComputePassDesc upsampleDesc;
    upsampleDesc.textureReads.push_back(TextureUseDesc(bloomChainFinal, kBloomMip0));
    upsampleDesc.textureReads.push_back(TextureUseDesc(bloomChainFinal, kBloomMip1));
    upsampleDesc.textureWrites.push_back(bloomBlur);
    graph.addComputePass(
        "lmx.pass.bloom.upsample", std::move(upsampleDesc),
        [this, &commands, bloomChainFinal, bloomBlur, bloomWidth, bloomHeight, bloomSmallWidth,
         bloomSmallHeight](const PassResources& resources) {
            const GraphResult<rhi::Texture*> chain = resources.texture(bloomChainFinal);
            LMX_ASSERT(chain.has_value(), chain.error().message);
            const GraphResult<rhi::Texture*> blur = resources.texture(bloomBlur);
            LMX_ASSERT(blur.has_value(), blur.error().message);

            const BloomUpsampleParams params{.smallWidth = bloomSmallWidth,
                                             .smallHeight = bloomSmallHeight,
                                             .dstWidth = bloomWidth,
                                             .dstHeight = bloomHeight};
            commands.bindComputePipeline(*m_bloomUpsamplePipeline);
            commands.bindStorageTexture(kBloomUpsampleBaseSlot, **chain,
                                        rhi::TextureViewDesc{.range = kBloomMip0},
                                        rhi::StorageAccess::Read);
            commands.bindStorageTexture(kBloomUpsampleSmallSlot, **chain,
                                        rhi::TextureViewDesc{.range = kBloomMip1},
                                        rhi::StorageAccess::Read);
            commands.bindStorageTexture(kBloomUpsampleDstSlot, **blur, rhi::TextureViewDesc{},
                                        rhi::StorageAccess::Write);
            commands.setUniforms(kBloomUpsampleParamsSlot, &params, sizeof(params));
            commands.dispatch(divRoundUp(bloomWidth, kComputeThreadsPerGroup2D),
                              divRoundUp(bloomHeight, kComputeThreadsPerGroup2D), 1);
        });
    const GraphTexture bloomResult = nextVersion(bloomBlur);

    PassDesc displayDesc;
    // Declaring the read is what orders this pass after the scene pass and puts the scene
    // target's transition to a shader read in front of it; nothing here places a barrier.
    displayDesc.textureReads.push_back(sceneColorRead);
    // Bloom's read is declared only when the toggle is on: an undeclared bloomResult reaches no
    // sink through this pass, so dead-pass culling drops threshold/downsample/upsample together
    // when it is off (spec 10) -- the same pattern the exposure passes above use.
    const bool bloomEnabled = view.bloomEnabled;
    if (bloomEnabled) {
        displayDesc.textureReads.push_back(TextureUseDesc(
            bloomResult, rhi::TextureSubresourceRange{.baseMipLevel = 0, .mipLevelCount = 1}));
    }
    // The fullscreen triangle covers every pixel, so the clear only states an attachment load
    // action the RHI requires; no fragment reads what it wrote.
    displayDesc.color = ColorAttachment{
        .handle = displayColor, .load = LoadOp::Clear, .store = StoreOp::Store, .clearColor = {}};
    const float bloomIntensity = view.bloomIntensity;
    graph.addPass(
        "lmx.pass.display", std::move(displayDesc),
        [this, &commands, sceneColorRead, bloomResult, bloomEnabled,
         bloomIntensity](const PassResources& resources) {
            const GraphResult<rhi::Texture*> hdrTexture = resources.texture(sceneColorRead);
            LMX_ASSERT(hdrTexture.has_value(), hdrTexture.error().message);

            commands.bindPipeline(*m_displayPipeline);
            commands.bindTexture(kSceneColorTextureSlot, **hdrTexture);
            // Disabled bloom binds a texture that is exactly zero and multiplies by exactly zero
            // (belt and suspenders): scene color + 0 is bit-identical to scene color alone, which
            // is what keeps a bloom-off frame byte-identical to pre-M5 output.
            if (bloomEnabled) {
                const GraphResult<rhi::Texture*> bloomTexture = resources.texture(bloomResult);
                LMX_ASSERT(bloomTexture.has_value(), bloomTexture.error().message);
                commands.bindTexture(kDisplayBloomTextureSlot, **bloomTexture);
            } else {
                commands.bindTexture(kDisplayBloomTextureSlot, *m_blackBloomFallback);
            }
            const DisplayParams params{.bloomIntensity = bloomEnabled ? bloomIntensity : 0.0f};
            commands.setUniforms(kDisplayParamsSlot, &params, sizeof(params));
            commands.draw(3);
        });

    return nextVersion(displayColor);
}

//======================================================================================================================
void Renderer::render(rhi::CommandList& commands, const Camera& camera, const SceneView& view,
                      bool barrierForSampling) {
    // declarePasses() always declares bloom's transients (spec 10: it is an ordinary graph pass
    // whether or not a caller's own graph has a pool), so this convenience path needs one of its
    // own -- the caller already opened this frame with Device::beginFrame() before calling here,
    // which is what m_transientPool.beginFrame() requires.
    m_transientPool.beginFrame();
    RenderGraph graph(m_transientPool);
    const GraphTexture displayColor = declarePasses(graph, commands, camera, view);
    // The display-transformed image is this frame's whole result, so it is what the graph roots.
    graph.exportTexture(displayColor);
    graph.execute(commands, m_device.frameNumber());

    if (barrierForSampling) {
        // For a sampling pass outside this graph: a hand-encoded pass declares nothing, so there
        // is no read for the graph to have derived the transition from.
        commands.textureBarrier(*m_color, rhi::TextureUse::RenderTarget,
                                rhi::TextureUse::ShaderRead);
    }
}

//======================================================================================================================
rhi::Texture& Renderer::colorTarget() {
    LMX_ASSERT(m_color != nullptr, "Renderer::colorTarget: no color target -- create() failed");
    return *m_color;
}

//======================================================================================================================
rhi::Texture& Renderer::hdrColorTarget() {
    LMX_ASSERT(m_hdrColor != nullptr,
               "Renderer::hdrColorTarget: no scene color target -- create() failed");
    return *m_hdrColor;
}

//======================================================================================================================
rhi::Texture& Renderer::depthTarget() {
    LMX_ASSERT(m_depth != nullptr, "Renderer::depthTarget: no depth target -- create() failed");
    return *m_depth;
}

//======================================================================================================================
rhi::Buffer& Renderer::exposureBuffer() {
    LMX_ASSERT(m_exposureBuffer != nullptr,
               "Renderer::exposureBuffer: no exposure buffer -- create() failed");
    return *m_exposureBuffer;
}

} // namespace lmx::render
