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
    float emissivePadding;   // 300 -- the float3's tail
    // Append-only growth for the motion entry points (spec 5). Every frame uploads it, temporal
    // on or off, so nothing branches on the temporal state to decide what a draw's bytes are.
    glm::mat4 previousModel; // 304
};
static_assert(sizeof(ObjectUniforms) == 368, "must match ScenePass.slang's ObjectUniforms");

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
    // The motion pair, unjittered: rasterisation carries the jitter in ObjectUniforms.mvp, and
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

// Mirrors Shaders/HistogramAccumulate.slang's HistogramParams. Every field is a scalar, so HLSL
// cbuffer packing (which Slang's Metal path still follows) leaves them contiguous -- no vector
// field ever forces a gap here.
struct HistogramParams {
    float logLuminanceMin;
    float logLuminanceMax;
    uint32_t width;
    uint32_t height;
};
static_assert(sizeof(HistogramParams) == 16,
              "must match HistogramAccumulate.slang's HistogramParams");

// Mirrors Shaders/ExposureSeed.slang's ExposureSeedParams.
struct ExposureSeedParams {
    float exposure;
};
static_assert(sizeof(ExposureSeedParams) == 4,
              "must match ExposureSeed.slang's ExposureSeedParams");

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
    float adaptUp;
    float adaptDown;
    float deltaSeconds;
    float pad;
};
static_assert(sizeof(ExposureResolveParams) == 48,
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

// ScenePass.slang's kFlagHasNormalMap and kFlagMotionInvalid.
constexpr uint32_t kFlagHasNormalMap = 1u;
constexpr uint32_t kFlagMotionInvalid = 2u;

// Shaders/Shadow.slang's kShadowFilterPcf / kShadowFilterPcss.
constexpr int32_t kShadowFilterPcf = 0;
constexpr int32_t kShadowFilterPcss = 1;

constexpr uint32_t kVertexBufferSlot = 0;
constexpr uint32_t kObjectUniformsSlot = 1;
constexpr uint32_t kPassUniformsSlot = 2;
// The persistent exposure buffer (spec 9), read by ScenePassAuto.slang/SkyAuto.slang's fragment
// shaders -- the pipelines Renderer.cpp selects only while auto-exposure is on. Bound via
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

// DisplayTransform.slang's own resource set: unrelated to the scene pass's slots above.
constexpr uint32_t kSceneColorTextureSlot = 0;
constexpr uint32_t kDisplayBloomTextureSlot = 1;
constexpr uint32_t kDisplayParamsSlot = 0;

// HistogramAccumulate.slang's slot map.
constexpr uint32_t kHistogramSceneColorSlot = 0; // texture
constexpr uint32_t kHistogramBufferSlot = 0;     // buffer
constexpr uint32_t kHistogramExposureSlot = 1;   // buffer
constexpr uint32_t kHistogramParamsSlot = 2;     // buffer

// ExposureResolve.slang's slot map (buffer space only).
constexpr uint32_t kResolveHistogramSlot = 0;
constexpr uint32_t kResolveExposureSlot = 1;
constexpr uint32_t kResolveParamsSlot = 2;

// ExposureSeed.slang's slot map (buffer space only).
constexpr uint32_t kSeedExposureSlot = 0;
constexpr uint32_t kSeedParamsSlot = 1;

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
// The seconds one declared frame advances the scene by, which is what exposure adaptation steps
// against. The App's clock is a fixed step per frame rather than wall time -- engine::
// kAnimationBakeRate -- and this restates the number rather than including it, because Engine sits
// above Render in the dependency chain. A renderer that stepped by wall time would resolve a
// different exposure for the same frame on a different machine, which is not something a frozen
// stability tolerance can survive.
constexpr float kExposureFrameSeconds = 1.0f / 60.0f;

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
                    {"emissive", offsetof(ObjectUniforms, emissive), "float3"},
                    {"previousModel", offsetof(ObjectUniforms, previousModel), "float4x4"}}});

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
    if (auto library = device.loadShaderLibrary("Shaders/ScenePassAuto"); library) {
        self->m_sceneAutoLibrary = std::move(*library);
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
    if (auto library = device.loadShaderLibrary("Shaders/SkyAuto"); library) {
        self->m_skyAutoLibrary = std::move(*library);
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
    if (auto library = device.loadShaderLibrary("Shaders/ExposureSeed"); library) {
        self->m_exposureSeedLibrary = std::move(*library);
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
    const auto makeScenePipeline = [&](rhi::ShaderLibrary* library, rhi::FillMode fill,
                                       const char* label) {
        return device.createGraphicsPipeline({.library = library,
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
             .colorFormat = kSceneColorFormat,
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
    const auto makeSkyPipeline = [&](rhi::ShaderLibrary* library, const char* label) {
        return device.createGraphicsPipeline({.library = library,
                                              .vertexEntry = "vertexMain",
                                              .fragmentEntry = "fragmentMain",
                                              .colorFormat = kSceneColorFormat,
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
             .colorFormat = kSceneColorFormat,
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
    if (auto pipeline = device.createComputePipeline({.library = self->m_exposureSeedLibrary.get(),
                                                      .computeEntry = "computeExposureSeed",
                                                      .threadsPerThreadgroup = {1, 1, 1},
                                                      .label = "lmx.render.exposureSeedPipeline"});
        pipeline) {
        self->m_exposureSeedPipeline = std::move(*pipeline);
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
    // A valid resource for DisplayTransform's bloom slot when bloom is off. The shader skips the
    // texture load in that mode, but the argument table must still contain a bound texture.
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
    // The persistent {applied, previous} pair (spec 9's feedback buffer, widened by M6.2 spec 7):
    // the seed and resolve passes write it, and -- when auto-exposure is on -- the *next* frame's
    // scene and sky passes read index 0 directly as a storage buffer (declarePasses()'s
    // exposureCurrent/bufferReads below), never through a CPU readback. Unit exposure in both slots
    // is what a buffer no frame has seeded or resolved yet holds, which is the same answer EV 0
    // gives. cpuReadback only where the caller asked for it: the App never reads this back -- that
    // is the whole point of keeping the feedback GPU-resident -- and the tests do.
    {
        constexpr std::array<float, kExposureBufferFloats> kInitialExposure{1.0f, 1.0f};
        if (auto buffer = device.createBuffer({.size = sizeof(kInitialExposure),
                                               .storageRead = true,
                                               .storageWrite = true,
                                               .cpuReadback = cpuReadback,
                                               .label = "lmx.render.exposureBuffer"},
                                              kInitialExposure.data());
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

    // The reconstruction stage owns both history pairs and the passes over them, so it is built
    // before the first resize(), which is what allocates its slots at this renderer's extent.
    if (auto stage = TemporalResolve::create(device, self->m_cpuReadback); stage) {
        self->m_temporalResolve = std::move(*stage);
    } else {
        return std::unexpected(stage.error());
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
    // Swap the targets only after every allocation succeeds.
    m_hdrColor = std::move(*hdrColor);
    m_color = std::move(*color);
    m_width = width;
    m_height = height;

    // The temporal targets follow the scene targets' extent, under the same caller idle guarantee
    // this function already requires. The histories' contents are dropped with the old textures,
    // which the extent change makes a reset anyway (HistoryResetReason::ExtentChanged). Depth is
    // among them: it ping-pongs by temporal frame parity, so the stage owns both slots of it.
    if (auto targets = createTemporalTargets(); !targets) {
        return std::unexpected(targets.error());
    }
    if (auto slots = m_temporalResolve->resize(width, height); !slots) {
        return std::unexpected(slots.error());
    }
    return {};
}

//======================================================================================================================
rhi::Result<void> Renderer::createTemporalTargets() {
    LMX_ASSERT(m_width > 0 && m_height > 0,
               "Renderer::createTemporalTargets: the render extent must be non-empty");

    // Readable on the same terms as the scene targets: motion is the one output a GPU test has to
    // compare against an arithmetic oracle rather than against a picture.
    auto motion = m_device.createTexture({.width = m_width,
                                          .height = m_height,
                                          .format = kMotionFormat,
                                          .renderTarget = true,
                                          .sampled = true,
                                          .cpuReadback = m_cpuReadback,
                                          .label = "lmx.render.motion"});
    if (!motion) {
        return std::unexpected(motion.error());
    }
    // The per-pixel reactive weight: the scene pass's second extra attachment, read by the resolve
    // alone. One byte per pixel, and colour-renderable rather than storage, because a fragment
    // writes it as an ordinary attachment.
    auto reactive = m_device.createTexture({.width = m_width,
                                            .height = m_height,
                                            .format = kReactiveFormat,
                                            .renderTarget = true,
                                            .sampled = true,
                                            .cpuReadback = m_cpuReadback,
                                            .label = "lmx.render.reactive"});
    if (!reactive) {
        return std::unexpected(reactive.error());
    }

    m_motion = std::move(*motion);
    m_reactive = std::move(*reactive);
    return {};
}

//======================================================================================================================
GraphTexture Renderer::declarePasses(RenderGraph& graph, rhi::CommandList& commands,
                                     const Camera& camera, const SceneView& view) {
    LMX_ASSERT(m_hdrColor && m_color && m_shadowMap && m_motion && m_reactive && m_temporalResolve,
               "Renderer::declarePasses: targets are missing -- create() failed");
    LMX_ASSERT(view.boundingSphere.w > 0.0f,
               "SceneView::boundingSphere needs a positive radius -- it is what the shadow "
               "frustum is fitted to");
    if (view.autoExposureEnabled) {
        LMX_ASSERT(view.exposureLowPercentile >= 0.0f &&
                       view.exposureLowPercentile < view.exposureHighPercentile &&
                       view.exposureHighPercentile <= 100.0f,
                   "SceneView exposure percentiles must satisfy 0 <= low < high <= 100");
        LMX_ASSERT(view.exposureTargetGrey > 0.0f, "SceneView exposureTargetGrey must be positive");
        LMX_ASSERT(view.exposureEvMin <= view.exposureEvMax,
                   "SceneView exposure EV minimum must not exceed its maximum");
        LMX_ASSERT(view.exposureAdaptUpStopsPerSecond >= 0.0f &&
                       view.exposureAdaptDownStopsPerSecond >= 0.0f,
                   "SceneView exposure adaptation rates must not be negative");
    }

    const bool temporalEnabled = view.temporal.enabled;
    // The render scale is meaningful only with temporal on, the way jitterEnabled already is: a
    // frame with temporal off rasterises at the output extent whatever the field says.
    LMX_ASSERT(!temporalEnabled || (view.temporal.renderScale >= kMinRenderScale &&
                                    view.temporal.renderScale <= kMaxRenderScale),
               "SceneView temporal renderScale must lie within [kMinRenderScale, kMaxRenderScale]");
    const float renderScale = temporalEnabled ? view.temporal.renderScale : 1.0f;

    // Both extents of the frame. Every render-extent target keeps its output-extent allocation and
    // is used through an origin-anchored active rectangle, so a scale change allocates nothing.
    const FrameExtents extents = renderExtentsForScale(m_width, m_height, renderScale);
    const bool upscaled = extents.renderWidth != m_width || extents.renderHeight != m_height;
    const FrameSignature signature{.sceneGeneration = view.temporal.sceneGeneration,
                                   .extents = extents,
                                   .fovY = camera.fovY,
                                   .nearZ = camera.nearZ,
                                   .temporalEnabled = temporalEnabled};
    const HistoryResetReason resetReason =
        deriveHistoryReset(m_previousSignature, signature, view.temporal.cameraCut);
    const bool historyValid = temporalEnabled && resetReason == HistoryResetReason::None;
    // The extents the previous declared frame ran at, which is what addresses the previous depth
    // slot. A reset frame reads no history at all, so it stands in its own extents rather than a
    // predecessor's that describes another image.
    const FrameExtents previousExtents =
        resetReason == HistoryResetReason::None && m_previousSignature
            ? m_previousSignature->extents
            : extents;
    // Ping-pong by declared temporal frame parity: this frame renders depth into `slot` and writes
    // its colour there, and reads the other slot as the previous frame's depth and history. A
    // frame with temporal off renders depth into slot 0 and touches no colour history at all.
    const uint32_t slot = temporalEnabled ? m_temporalFrame % 2 : 0;
    const uint32_t previousSlot = 1 - slot;
    m_currentSlot = slot;
    const ReconstructionMode reconstruction = view.temporal.reconstruction;
    const bool nativeTaa = temporalEnabled && reconstruction == ReconstructionMode::NativeTaa;
    const TemporalDebugView debugView = view.temporal.debugView;

    const glm::vec2 jitterPixels = temporalEnabled && view.temporal.jitterEnabled
                                       ? haltonJitterPixels(m_temporalFrame)
                                       : glm::vec2{0.0f};
    const CameraFrameState cameraState = buildCameraFrameState(camera, extents, jitterPixels);
    // A frame with no predecessor reprojects onto itself, which is the only honest answer: its
    // history is being reset anyway, so a fabricated previous camera would only invent motion.
    const CameraFrameState previousCamera = m_previousCamera.value_or(cameraState);

    const ShadowMatrices shadow = fitShadowOrtho(view.boundingSphere, view.lights[0].direction);

    // The graph snapshots formats at import for attachment validation; these named constants are
    // the same ones resize() and create() used to build the persistent targets.
    // These targets persist across frames while three command buffers may be in flight. Their
    // previous frame's terminal uses seed the fresh graph so its first attachment writes cannot
    // overlap those earlier reads/writes on Metal 4's queue. Depth and display use ShaderRead
    // conservatively because their public targets may be sampled by a caller after this graph;
    // that stage set also covers their ordinary fragment attachment work.
    const GraphTexture shadowMap = graph.importTexture(
        *m_shadowMap, rhi::Format::D32Float, "lmx.render.shadowMap", rhi::TextureUse::ShaderRead);
    // The scene colour's terminal use is the display pass's read on an ordinary frame and the
    // history commit's copy on a temporal one, so it is what the previous frame left rather than a
    // constant.
    const GraphTexture sceneColor = graph.importTexture(
        *m_hdrColor, kSceneColorFormat, "lmx.render.sceneColorHdr", m_previousSceneColorUse);
    const GraphTexture displayColor = graph.importTexture(
        *m_color, kDisplayFormat, "lmx.render.displayColor", rhi::TextureUse::ShaderRead);
    // A frame with temporal off imports slot 0 under the pre-temporal name and use, which is what
    // keeps its declaration exactly the one M6.1 made; a temporal frame names the slot it uses.
    const GraphTexture sceneDepth =
        temporalEnabled
            ? m_temporalResolve->importDepth(graph, slot)
            : graph.importTexture(m_temporalResolve->depthSlot(0), rhi::Format::D32Float,
                                  "lmx.render.sceneDepth", rhi::TextureUse::ShaderRead);

    // The temporal pair, imported only by a frame that declares the temporal path. Motion's
    // terminal use is its attachment write on a frame that shows no debug view and the view's own
    // read on one that does, so the import states what the previous frame recorded. History's is
    // the commit copy at the end of the previous temporal frame, which is what this frame's
    // reprojection reads across the frame boundary; the reactive attachment's is its own
    // attachment write on a Raw frame and the resolve's read on a NativeTaa one.
    GraphTexture motionTargetHandle;
    GraphTexture reactiveTargetHandle;
    GraphTexture previousDepth;
    GraphTexture colorSlotImport;
    GraphTexture historyImport;
    if (temporalEnabled) {
        motionTargetHandle =
            graph.importTexture(*m_motion, kMotionFormat, "lmx.render.motion", m_previousMotionUse);
        reactiveTargetHandle = graph.importTexture(*m_reactive, kReactiveFormat,
                                                   "lmx.render.reactive", m_previousReactiveUse);
        previousDepth = m_temporalResolve->importDepth(graph, previousSlot);
        colorSlotImport = m_temporalResolve->importColor(graph, slot);
        historyImport = m_temporalResolve->importColor(graph, previousSlot);
    }

    // Exposure feedback (spec 9): the persistent exposure buffer is imported here, before the
    // scene pass, because -- when auto-exposure is on -- the scene and sky passes read it
    // directly (a GPU-persistent value with a one-frame lag; never a CPU readback, which would
    // stall the three-frames-in-flight pipeline every auto-exposure frame). A reset frame (first
    // frame, scene switch, auto-exposure enable, resize) seeds it with the manual EV first, as an
    // ordinary compute dispatch -- not a stall -- so shading and the histogram/resolve chain below
    // agree on "this frame's preExposure" even on the frame the loop restarts.
    //
    // In auto mode it is imported as produced by an earlier frame's resolve dispatch, which is what
    // it holds: that pass wrote it with a storage write, this frame's scene and sky passes read it,
    // and the edge between them crosses a frame boundary where nothing else orders it -- the frames
    // in flight are paced against a frame two back, not against the one before. Stating the prior
    // producer is what lets this frame's derivation put a barrier in front of its first reader. A
    // manual temporal frame states the same thing for the same reason: its seed dispatch below
    // overwrites what the previous frame's seed wrote. Manual with temporal off imports it plainly:
    // no declared pass touches it, so there is no edge to state.
    const bool exposureWrittenByEarlierFrame = view.autoExposureEnabled || view.temporal.enabled;
    const GraphBuffer exposureImport =
        exposureWrittenByEarlierFrame
            ? graph.importBuffer(*m_exposureBuffer, "lmx.render.exposureBuffer",
                                 rhi::BufferUse::StorageWrite)
            : graph.importBuffer(*m_exposureBuffer, "lmx.render.exposureBuffer");
    GraphBuffer exposureCurrent = exposureImport;
    // Auto mode seeds on spec 9's reset frames, where the seed is what restarts the metering loop.
    // Manual mode seeds on every temporal frame (M6.2 spec 7): the buffer is the single source of
    // the exposure the scene pass applies, so a manual EV edit has to land in it -- with the value
    // before the edit shifted into `previous` -- for a temporal resolve to correct the history it
    // is about to blend. A manual frame with temporal off declares nothing here, which is what
    // keeps the pre-temporal frame's declaration exact.
    const bool seedExposure = view.autoExposureEnabled ? view.exposureReset : view.temporal.enabled;
    if (seedExposure) {
        const float manualExposure = std::exp2(view.exposureEv);
        ComputePassDesc seedDesc;
        seedDesc.bufferWrites.push_back(exposureImport);
        graph.addComputePass(
            "lmx.pass.exposure.seed", std::move(seedDesc),
            [this, &commands, exposureImport, manualExposure](const PassResources& resources) {
                const GraphResult<rhi::Buffer*> exposure = resources.buffer(exposureImport);
                LMX_ASSERT(exposure.has_value(), exposure.error().message);
                const ExposureSeedParams params{.exposure = manualExposure};
                commands.bindComputePipeline(*m_exposureSeedPipeline);
                // Read-write, not write: the kernel shifts index 0 into index 1 before it sets it.
                commands.bindStorageBuffer(kSeedExposureSlot, **exposure,
                                           rhi::StorageAccess::ReadWrite);
                commands.bindFrameData(kSeedParamsSlot, params);
                commands.dispatch(1, 1, 1);
            });
        exposureCurrent = nextVersion(exposureImport);
        if (!view.autoExposureEnabled) {
            // Auto mode's resolve exports the end of the chain the seed starts, so the seed reaches
            // a sink. In manual mode nothing declared in this frame consumes what the seed wrote --
            // its consumer is the next frame -- so the version it produces is exported the way the
            // history commit's is, or dead-pass culling would drop the pass that records the pair.
            graph.exportBuffer(exposureCurrent);
        }
    }

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
                          commands.bindFrameData(kObjectUniformsSlot, uniforms);
                          commands.drawIndexed(*item.mesh->indexBuffer, item.mesh->indexCount);
                      }
                  });

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
    passUniforms.shadowTransform = shadow.shadowTransform;
    passUniforms.eyePos = camera.position;
    passUniforms.time = timeSeconds;
    passUniforms.preExposure = preExposure;
    for (size_t i = 0; i < std::size(passUniforms.lights); ++i) {
        passUniforms.lights[i] = toUniform(view.lights[i]);
    }
    passUniforms.shadowFilter =
        view.shadowFilter == ShadowFilter::PCSS ? kShadowFilterPcss : kShadowFilterPcf;
    passUniforms.viewProjUnjittered = cameraState.viewProjection;
    passUniforms.previousViewProjUnjittered = previousCamera.viewProjection;

    const GraphTexture shadowRead = nextVersion(shadowMap);

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
        srgbToLinear(glm::vec3(clearColor[0], clearColor[1], clearColor[2])) * preExposure;

    PassDesc sceneDesc;
    sceneDesc.textureReads.push_back(shadowRead);
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
        [this, &commands, view, passUniforms, viewProj, shadowRead, exposureCurrent,
         temporalEnabled, cameraState, previousCamera, jitterNdc](const PassResources& resources) {
            // Resolved rather than captured: the graph hands over the shadow map only because this
            // pass declared reading it, which is what ordered it after the pass that wrote it.
            const GraphResult<rhi::Texture*> shadowMapTexture = resources.texture(shadowRead);
            LMX_ASSERT(shadowMapTexture.has_value(), shadowMapTexture.error().message);

            // Auto-exposure selects ScenePassAuto.slang's compiled pipeline instead of
            // ScenePass.slang's (spec 9): a separate shader file and pipeline, not a runtime
            // branch in one, is what keeps the manual pipeline's compiled output identical to
            // pre-M5 -- see ScenePassAuto.slang's header.
            if (temporalEnabled) {
                // The motion twins of the four pipelines below: same shading, one more attachment.
                if (view.autoExposureEnabled) {
                    commands.bindPipeline(view.wireframe ? *m_sceneWireframePipelineAutoMotion
                                                         : *m_scenePipelineAutoMotion);
                } else {
                    commands.bindPipeline(view.wireframe ? *m_sceneWireframePipelineMotion
                                                         : *m_scenePipelineMotion);
                }
            } else if (view.autoExposureEnabled) {
                commands.bindPipeline(view.wireframe ? *m_sceneWireframePipelineAuto
                                                     : *m_scenePipelineAuto);
            } else {
                commands.bindPipeline(view.wireframe ? *m_sceneWireframePipeline
                                                     : *m_scenePipeline);
            }
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
            // Only ScenePassAuto.slang/SkyAuto.slang declare this resource at all, so it is bound
            // only when their pipelines are the ones in use.
            if (view.autoExposureEnabled) {
                const GraphResult<rhi::Buffer*> exposureOverride =
                    resources.buffer(exposureCurrent);
                LMX_ASSERT(exposureOverride.has_value(), exposureOverride.error().message);
                commands.bindBuffer(kExposureOverrideSlot, **exposureOverride);
            }
            commands.bindFrameData(kPassUniformsSlot, passUniforms);

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
                uniforms.previousModel = item.previousModel;
                if (item.motionClass == MotionClass::Invalid) {
                    uniforms.flags |= kFlagMotionInvalid;
                }

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
                // bindFrameData copies into frame-owned memory before the next draw rebinds the
                // slot.
                commands.bindFrameData(kObjectUniformsSlot, uniforms);
                commands.drawIndexed(*item.mesh->indexBuffer, item.mesh->indexCount);
            }

            // Draw the solid sky last so opaque geometry rejects covered fragments at the depth
            // clear.
            if (view.skySphere != nullptr && view.skyCubemap != nullptr) {
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
                commands.bindBuffer(kVertexBufferSlot, *view.skySphere->vertexBuffer);
                commands.bindFrameData(kPassUniformsSlot, sky);
                commands.drawIndexed(*view.skySphere->indexBuffer, view.skySphere->indexCount);
            }
        });

    const GraphTexture sceneColorRead = nextVersion(sceneColor);
    const uint32_t sceneWidth = m_width;
    const uint32_t sceneHeight = m_height;
    const GraphTexture motionRead =
        temporalEnabled ? nextVersion(motionTargetHandle) : GraphTexture{};

    // ---- Temporal reconstruction (M6.2 spec 6). The stage declares the reprojection diagnostic,
    // this frame's accumulation or its raw commit, and the debug view; this frame routes what it
    // produced into bloom and display. The debug view draws over the version the display pass
    // produces, which is declared further down -- the graph's schedule is topological, so naming
    // that version here still orders the view behind its producer.
    GraphTexture displayResult = nextVersion(displayColor);
    TemporalResolveOutputs temporalOutputs;
    if (temporalEnabled) {
        TemporalInputs temporalInputs;
        temporalInputs.sceneColor = sceneColorRead;
        temporalInputs.depth = nextVersion(sceneDepth);
        temporalInputs.previousDepth = previousDepth;
        temporalInputs.motion = motionRead;
        temporalInputs.reactive = nextVersion(reactiveTargetHandle);
        temporalInputs.history = historyImport;
        temporalInputs.colorSlot = colorSlotImport;
        // The version the scene pass and the histogram read: after the seed, so "applied this
        // frame" means the same thing to shading, metering and the exposure correction alike.
        temporalInputs.exposure = exposureCurrent;
        temporalInputs.camera = cameraState;
        temporalInputs.previousCamera = previousCamera;
        temporalInputs.extents = extents;
        temporalInputs.previousExtents = previousExtents;
        temporalInputs.resetReason = resetReason;
        temporalInputs.mode = reconstruction;
        temporalOutputs =
            m_temporalResolve->declare(graph, commands, temporalInputs, debugView, displayResult);
    }

    // What bloom and the display transform read: the colour slot whenever the frame accumulated or
    // upscaled into it -- both leave the finished output-extent picture there -- and the raw
    // jittered frame otherwise. Only a Raw frame that rasterised at the output extent still reads
    // scene colour. The histogram deliberately keeps metering the raw scene colour, so metering
    // stays independent of the accumulation it corrects.
    const GraphTexture displayInput =
        nativeTaa || upscaled ? temporalOutputs.resolved : sceneColorRead;

    // ---- Exposure feedback continued: histogram + resolve (spec 9). Declared every frame;
    // exported only when auto-exposure is on, so dead-pass culling drops the whole chain when it
    // is off. Both read `exposureCurrent` -- the exact version the scene/sky passes read above (or
    // its import version, harmlessly, when they did not) -- so the histogram's reconstruction of
    // "this frame's preExposure" agrees with what shading actually used, by construction rather
    // than by a CPU value threaded through both.
    // The previous frame's resolve dispatch is still potentially reading the histogram when this
    // frame clears it. Seeding the import with that terminal read makes the first live clear wait
    // on the real WAR edge; when auto exposure is off the chain is culled, so this costs nothing.
    const GraphBuffer histogramImport = graph.importBuffer(
        *m_histogramBuffer, "lmx.render.histogramBuffer", rhi::BufferUse::StorageRead);

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

    // Metering reads the raw scene colour, which is a render-extent signal: the pass dispatches
    // over the active rectangle and states it, so no invocation reads the stale region outside it.
    // Metering is per texel, so the exposure it derives is the same at every scale.
    const uint32_t meterWidth = extents.renderWidth;
    const uint32_t meterHeight = extents.renderHeight;
    ComputePassDesc histogramDesc;
    histogramDesc.shaderTextureReads.push_back(sceneColorRead);
    histogramDesc.shaderBufferReads.push_back(exposureCurrent);
    histogramDesc.bufferWrites.push_back(histogramCleared);
    graph.addComputePass(
        "lmx.pass.exposure.histogram", std::move(histogramDesc),
        [this, &commands, sceneColorRead, histogramCleared, exposureCurrent, meterWidth,
         meterHeight](const PassResources& resources) {
            const GraphResult<rhi::Texture*> scene = resources.texture(sceneColorRead);
            LMX_ASSERT(scene.has_value(), scene.error().message);
            const GraphResult<rhi::Buffer*> histogram = resources.buffer(histogramCleared);
            LMX_ASSERT(histogram.has_value(), histogram.error().message);
            const GraphResult<rhi::Buffer*> exposure = resources.buffer(exposureCurrent);
            LMX_ASSERT(exposure.has_value(), exposure.error().message);

            const HistogramParams params{.logLuminanceMin = kExposureLogLuminanceMin,
                                         .logLuminanceMax = kExposureLogLuminanceMax,
                                         .width = meterWidth,
                                         .height = meterHeight};
            commands.bindComputePipeline(*m_histogramPipeline);
            commands.bindTexture(kHistogramSceneColorSlot, **scene);
            commands.bindStorageBuffer(kHistogramBufferSlot, **histogram,
                                       rhi::StorageAccess::ReadWrite);
            commands.bindBuffer(kHistogramExposureSlot, **exposure);
            commands.bindFrameData(kHistogramParamsSlot, params);
            commands.dispatch(divRoundUp(meterWidth, kComputeThreadsPerGroup2D),
                              divRoundUp(meterHeight, kComputeThreadsPerGroup2D), 1);
        });
    const GraphBuffer histogramFinal = nextVersion(histogramCleared);

    ComputePassDesc resolveDesc;
    resolveDesc.bufferReads.push_back(histogramFinal);
    // The kernel reads the exposure it adapts from out of the same version it overwrites, and the
    // write alone is what states that: a declared write already orders this pass after every
    // earlier producer and consumer of that version, so naming the read as well would add a
    // declaration to the temporal-off frame's record without adding an edge to derive from it.
    resolveDesc.bufferWrites.push_back(exposureCurrent);
    graph.addComputePass(
        "lmx.pass.exposure.resolve", std::move(resolveDesc),
        [this, &commands, histogramFinal, exposureCurrent, view](const PassResources& resources) {
            const GraphResult<rhi::Buffer*> histogram = resources.buffer(histogramFinal);
            LMX_ASSERT(histogram.has_value(), histogram.error().message);
            const GraphResult<rhi::Buffer*> exposure = resources.buffer(exposureCurrent);
            LMX_ASSERT(exposure.has_value(), exposure.error().message);

            const ExposureResolveParams params{.lowPercentile = view.exposureLowPercentile,
                                               .highPercentile = view.exposureHighPercentile,
                                               .targetGrey = view.exposureTargetGrey,
                                               .evMin = view.exposureEvMin,
                                               .evMax = view.exposureEvMax,
                                               .compensationEv = view.exposureCompensationEv,
                                               .logLuminanceMin = kExposureLogLuminanceMin,
                                               .logLuminanceMax = kExposureLogLuminanceMax,
                                               .adaptUp = view.exposureAdaptUpStopsPerSecond,
                                               .adaptDown = view.exposureAdaptDownStopsPerSecond,
                                               .deltaSeconds = kExposureFrameSeconds,
                                               .pad = 0.0f};
            commands.bindComputePipeline(*m_exposureResolvePipeline);
            commands.bindStorageBuffer(kResolveHistogramSlot, **histogram,
                                       rhi::StorageAccess::Read);
            // Read-write, not write: the kernel steps from the exposure this frame applied, which
            // it reads out of index 0 before overwriting it.
            commands.bindStorageBuffer(kResolveExposureSlot, **exposure,
                                       rhi::StorageAccess::ReadWrite);
            commands.bindFrameData(kResolveParamsSlot, params);
            commands.dispatch(1, 1, 1);
        });
    const GraphBuffer exposureResolved = nextVersion(exposureCurrent);
    if (view.autoExposureEnabled) {
        graph.exportBuffer(exposureResolved);
    }

    // ---- Bloom (spec 10). Two graph-created transients: `bloomChain`'s mips hold the threshold
    // and the downsample chain, and `bloomBlur`'s mips hold the upsample-accumulate walk back up
    // -- a second transient rather than accumulating into bloomChain in place, because one compute
    // pass may read and write one texture only through disjoint ranges (spec 6), and the
    // accumulate step's inputs (a bloomChain mip) and output (the same-sized bloomBlur mip) would
    // otherwise name overlapping ranges of one resource if they shared it. BloomUpsample.slang's
    // header carries the same reasoning. Declared every frame; only the display pass's read of
    // bloomBlur is conditional, so dead-pass culling drops threshold/downsample/upsample together
    // when bloom is off.
    // Ceil division includes the final source column and row in the threshold pass for odd scene
    // extents. Upsample and display reconstruction map pixel centres using the actual extents,
    // with bilinear filtering and clamped edge samples.
    const uint32_t bloomWidth = divRoundUp(sceneWidth, 2u);
    const uint32_t bloomHeight = divRoundUp(sceneHeight, 2u);

    // "A downsample chain into the mips" (spec 10) wants several levels, clamped to whatever the
    // extent supports without a mip collapsing to 1x1 before it has to: bloomMipCount counts mip 0
    // (the threshold's own output) plus up to kMaxBloomDownsampleLevels further halvings.
    constexpr uint32_t kMaxBloomDownsampleLevels = 4;
    uint32_t bloomMipCount = 1;
    for (uint32_t levelExtent = std::min(bloomWidth, bloomHeight);
         bloomMipCount <= kMaxBloomDownsampleLevels && levelExtent > 1; ++bloomMipCount) {
        levelExtent = std::max(levelExtent / 2u, 1u);
    }
    // A chain that cannot downsample even once (an extent already at 1x1) has nothing for an
    // upsample-accumulate step to combine; bloom degrades to no contribution that frame rather
    // than declaring a transient nothing would ever write.
    const bool bloomChainSupportsUpsample = bloomMipCount >= 2;

    const GraphTexture bloomChain = graph.createTexture({.width = bloomWidth,
                                                         .height = bloomHeight,
                                                         .format = kSceneColorFormat,
                                                         .mipLevels = bloomMipCount,
                                                         .storageRead = true,
                                                         .storageWrite = true},
                                                        "lmx.render.bloomChain");

    static constexpr rhi::TextureSubresourceRange kBloomMip0{.baseMipLevel = 0, .mipLevelCount = 1};
    ComputePassDesc thresholdDesc;
    thresholdDesc.shaderTextureReads.push_back(displayInput);
    thresholdDesc.textureWrites.push_back(TextureUseDesc(bloomChain, kBloomMip0));
    const float bloomThreshold = view.bloomThreshold;
    graph.addComputePass(
        "lmx.pass.bloom.threshold", std::move(thresholdDesc),
        [this, &commands, displayInput, bloomChain, sceneWidth, sceneHeight, bloomWidth,
         bloomHeight, bloomThreshold](const PassResources& resources) {
            const GraphResult<rhi::Texture*> scene = resources.texture(displayInput);
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
            commands.bindFrameData(kBloomThresholdParamsSlot, params);
            commands.dispatch(divRoundUp(bloomWidth, kComputeThreadsPerGroup2D),
                              divRoundUp(bloomHeight, kComputeThreadsPerGroup2D), 1);
        });

    // Downsample chain: one pass per level, mip (L-1) -> mip L, each reading and writing disjoint
    // mips of the one bloomChain version that step left behind -- dispatches within a single
    // compute pass carry no ordering guarantee, so each level needs its own pass regardless of how
    // many there are.
    GraphTexture bloomChainVersion = nextVersion(bloomChain); // after the threshold wrote mip 0
    for (uint32_t level = 1; level < bloomMipCount; ++level) {
        const uint32_t srcWidth = std::max(bloomWidth >> (level - 1), 1u);
        const uint32_t srcHeight = std::max(bloomHeight >> (level - 1), 1u);
        const uint32_t dstWidth = std::max(bloomWidth >> level, 1u);
        const uint32_t dstHeight = std::max(bloomHeight >> level, 1u);
        const rhi::TextureSubresourceRange srcRange{.baseMipLevel = level - 1, .mipLevelCount = 1};
        const rhi::TextureSubresourceRange dstRange{.baseMipLevel = level, .mipLevelCount = 1};

        ComputePassDesc downsampleDesc;
        downsampleDesc.textureReads.push_back(TextureUseDesc(bloomChainVersion, srcRange));
        downsampleDesc.textureWrites.push_back(TextureUseDesc(bloomChainVersion, dstRange));
        graph.addComputePass(
            std::format("lmx.pass.bloom.downsample{}", level - 1), std::move(downsampleDesc),
            [this, &commands, bloomChainVersion, srcRange, dstRange, srcWidth, srcHeight, dstWidth,
             dstHeight](const PassResources& resources) {
                const GraphResult<rhi::Texture*> chain = resources.texture(bloomChainVersion);
                LMX_ASSERT(chain.has_value(), chain.error().message);

                const BloomDownsampleParams params{.srcWidth = srcWidth,
                                                   .srcHeight = srcHeight,
                                                   .dstWidth = dstWidth,
                                                   .dstHeight = dstHeight};
                commands.bindComputePipeline(*m_bloomDownsamplePipeline);
                commands.bindStorageTexture(kBloomDownsampleSrcSlot, **chain,
                                            rhi::TextureViewDesc{.range = srcRange},
                                            rhi::StorageAccess::Read);
                commands.bindStorageTexture(kBloomDownsampleDstSlot, **chain,
                                            rhi::TextureViewDesc{.range = dstRange},
                                            rhi::StorageAccess::Write);
                commands.bindFrameData(kBloomDownsampleParamsSlot, params);
                commands.dispatch(divRoundUp(dstWidth, kComputeThreadsPerGroup2D),
                                  divRoundUp(dstHeight, kComputeThreadsPerGroup2D), 1);
            });
        bloomChainVersion = nextVersion(bloomChainVersion);
    }
    const GraphTexture bloomChainFinal = bloomChainVersion;

    GraphTexture bloomResult = bloomChainFinal; // overwritten below when there is a chain to walk
    if (bloomChainSupportsUpsample) {
        const GraphTexture bloomBlur = graph.createTexture({.width = bloomWidth,
                                                            .height = bloomHeight,
                                                            .format = kSceneColorFormat,
                                                            .mipLevels = bloomMipCount - 1,
                                                            .storageRead = true,
                                                            .storageWrite = true},
                                                           "lmx.render.bloomBlur");

        // Upsample-accumulate: walks from the smallest mip back to mip 0, one pass per level. The
        // first step's "small" input is bloomChain's own smallest mip; every later step's is the
        // previous step's own bloomBlur output, so the same kernel serves every level regardless
        // of which resource happens to be on the small side.
        GraphTexture bloomBlurVersion = bloomBlur; // v0 until the first write below
        for (uint32_t stepsRemaining = bloomMipCount - 1; stepsRemaining > 0; --stepsRemaining) {
            const uint32_t level = stepsRemaining - 1; // walks bloomMipCount - 2 down to 0
            const bool smallFromChain = level == bloomMipCount - 2;
            const uint32_t baseWidth = std::max(bloomWidth >> level, 1u);
            const uint32_t baseHeight = std::max(bloomHeight >> level, 1u);
            const uint32_t smallWidth = std::max(bloomWidth >> (level + 1), 1u);
            const uint32_t smallHeight = std::max(bloomHeight >> (level + 1), 1u);
            const rhi::TextureSubresourceRange baseRange{.baseMipLevel = level, .mipLevelCount = 1};
            const rhi::TextureSubresourceRange smallRange{.baseMipLevel = level + 1,
                                                          .mipLevelCount = 1};

            ComputePassDesc upsampleDesc;
            upsampleDesc.textureReads.push_back(TextureUseDesc(bloomChainFinal, baseRange));
            upsampleDesc.textureReads.push_back(
                TextureUseDesc(smallFromChain ? bloomChainFinal : bloomBlurVersion, smallRange));
            upsampleDesc.textureWrites.push_back(TextureUseDesc(bloomBlurVersion, baseRange));
            graph.addComputePass(
                std::format("lmx.pass.bloom.upsample{}", level), std::move(upsampleDesc),
                [this, &commands, bloomChainFinal, bloomBlurVersion, smallFromChain, baseRange,
                 smallRange, baseWidth, baseHeight, smallWidth,
                 smallHeight](const PassResources& resources) {
                    const GraphResult<rhi::Texture*> chain = resources.texture(bloomChainFinal);
                    LMX_ASSERT(chain.has_value(), chain.error().message);
                    const GraphResult<rhi::Texture*> blur = resources.texture(bloomBlurVersion);
                    LMX_ASSERT(blur.has_value(), blur.error().message);
                    rhi::Texture& smallTexture = smallFromChain ? **chain : **blur;

                    const BloomUpsampleParams params{.smallWidth = smallWidth,
                                                     .smallHeight = smallHeight,
                                                     .dstWidth = baseWidth,
                                                     .dstHeight = baseHeight};
                    commands.bindComputePipeline(*m_bloomUpsamplePipeline);
                    commands.bindStorageTexture(kBloomUpsampleBaseSlot, **chain,
                                                rhi::TextureViewDesc{.range = baseRange},
                                                rhi::StorageAccess::Read);
                    commands.bindStorageTexture(kBloomUpsampleSmallSlot, smallTexture,
                                                rhi::TextureViewDesc{.range = smallRange},
                                                rhi::StorageAccess::Read);
                    commands.bindStorageTexture(kBloomUpsampleDstSlot, **blur,
                                                rhi::TextureViewDesc{.range = baseRange},
                                                rhi::StorageAccess::Write);
                    commands.bindFrameData(kBloomUpsampleParamsSlot, params);
                    commands.dispatch(divRoundUp(baseWidth, kComputeThreadsPerGroup2D),
                                      divRoundUp(baseHeight, kComputeThreadsPerGroup2D), 1);
                });
            bloomBlurVersion = nextVersion(bloomBlurVersion);
        }
        bloomResult = bloomBlurVersion;
    }

    PassDesc displayDesc;
    // Declaring the read is what orders this pass after the scene pass and puts the scene
    // target's transition to a shader read in front of it; nothing here places a barrier.
    displayDesc.textureReads.push_back(displayInput);
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
        [this, &commands, displayInput, bloomResult, bloomEnabled,
         bloomIntensity](const PassResources& resources) {
            const GraphResult<rhi::Texture*> hdrTexture = resources.texture(displayInput);
            LMX_ASSERT(hdrTexture.has_value(), hdrTexture.error().message);

            commands.bindPipeline(*m_displayPipeline);
            commands.bindTexture(kSceneColorTextureSlot, **hdrTexture);
            // Disabled bloom binds a valid 1x1 resource and sets the exact zero that makes the
            // shader skip its texture load: scene color + 0 stays bit-identical to scene color
            // alone without addressing outside the fallback texture.
            if (bloomEnabled) {
                const GraphResult<rhi::Texture*> bloomTexture = resources.texture(bloomResult);
                LMX_ASSERT(bloomTexture.has_value(), bloomTexture.error().message);
                commands.bindTexture(kDisplayBloomTextureSlot, **bloomTexture);
            } else {
                commands.bindTexture(kDisplayBloomTextureSlot, *m_blackBloomFallback);
            }
            const DisplayParams params{.bloomIntensity = bloomEnabled ? bloomIntensity : 0.0f};
            commands.bindFrameData(kDisplayParamsSlot, params);
            commands.draw(3);
        });

    // The frame just declared becomes the previous one. A frame the caller abandoned before
    // declaring never reaches here, so it never becomes anyone's predecessor.
    ++m_declaredFrames;
    m_temporalStatus.lastReset = resetReason;
    if (resetReason != HistoryResetReason::None) {
        m_temporalStatus.lastResetFrame = m_declaredFrames;
    }
    m_temporalStatus.jitterIndex = m_temporalFrame % kJitterSequenceLength;
    m_temporalStatus.historyValid = historyValid;
    m_temporalStatus.historyBytes = m_temporalResolve->colorBytes();
    m_temporalStatus.depthHistoryBytes = m_temporalResolve->depthBytes();
    m_temporalStatus.reconstruction = reconstruction;
    // The age counts declared temporal frames since the last non-None reason, whatever the mode:
    // both modes leave a real frame in the colour slot, so the count survives a mode switch. A
    // frame with temporal off starts it over, because the history the next temporal frame finds is
    // not the one this count would have described.
    if (!temporalEnabled) {
        m_temporalStatus.historyAge = 0;
    } else if (resetReason != HistoryResetReason::None) {
        m_temporalStatus.historyAge = 1;
    } else {
        m_temporalStatus.historyAge = std::min<uint32_t>(m_temporalStatus.historyAge + 1, 65535);
    }
    m_temporalStatus.warmupComplete = m_temporalStatus.historyAge >= kTemporalWarmupFrames;
    m_temporalStatus.extents = extents;
    m_temporalStatus.renderScale = renderScale;
    m_temporalStatus.upscaled = upscaled;
    // A render-extent change under a reset reason says nothing: the history is being thrown away
    // anyway, and a frame with temporal off has no history to have survived anything -- it also
    // rasterises at the output extent whatever the scale field says, so a scale change straddling
    // it would otherwise be recorded twice. Under None with temporal on it is the whole point --
    // the history survived a change of the extent the scene rasterised at -- so that is the only
    // frame the count records.
    if (temporalEnabled && resetReason == HistoryResetReason::None && m_previousSignature &&
        (m_previousSignature->extents.renderWidth != extents.renderWidth ||
         m_previousSignature->extents.renderHeight != extents.renderHeight)) {
        m_temporalStatus.lastRenderExtentChangeFrame = m_declaredFrames;
    }
    m_previousSignature = signature;
    m_previousCamera = cameraState;
    // What this frame's last access to each persistent target was, for the next frame's imports to
    // state. The motion and reactive records survive frames with temporal off, which touch neither
    // target: overwriting them there would let a later re-enabling frame claim the last access was
    // its own attachment write, and its fragment-stage barrier would not drain the dispatch-stage
    // read the last temporal frame actually ended with.
    if (temporalEnabled) {
        // The resolve reads motion on every NativeTaa frame, and the debug view reads it whenever
        // one is shown.
        m_previousMotionUse = nativeTaa || debugView != TemporalDebugView::Off
                                  ? rhi::TextureUse::ShaderRead
                                  : rhi::TextureUse::RenderTarget;
        // Only the resolve reads the reactive attachment, so a Raw frame ends with its own write.
        m_previousReactiveUse =
            nativeTaa ? rhi::TextureUse::ShaderRead : rhi::TextureUse::RenderTarget;
        m_temporalResolve->recordFrame(slot, reconstruction, debugView, historyValid, upscaled);
    }
    // The scene colour is written and read by every frame. Under Raw at the output extent the
    // commit copy is the last thing to touch it; an upscaled Raw frame samples it in the spatial
    // pass instead of copying it, and under NativeTaa and with temporal off, bloom, the histogram
    // and the display transform all read it and none copies out of it.
    m_previousSceneColorUse = temporalEnabled && !nativeTaa && !upscaled
                                  ? rhi::TextureUse::CopySource
                                  : rhi::TextureUse::ShaderRead;
    if (temporalEnabled) {
        ++m_temporalFrame;
    }
    return displayResult;
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
    LMX_ASSERT(m_temporalResolve != nullptr,
               "Renderer::depthTarget: no depth target -- create() failed");
    return m_temporalResolve->depthSlot(m_currentSlot);
}

//======================================================================================================================
rhi::Texture* Renderer::historyTarget() {
    LMX_ASSERT(m_temporalResolve != nullptr,
               "Renderer::historyTarget: no history -- create() failed");
    return &m_temporalResolve->colorSlot(m_currentSlot);
}

} // namespace lmx::render
