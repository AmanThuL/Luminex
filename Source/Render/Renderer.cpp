//----------------------------------------------------------------------------------------------------------------------
/// @file Renderer.cpp
/// @brief Implements frame pass declaration and renderer-owned GPU resources.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Renderer.h"
#include "Render/VendorTemporalScaler.h"

#include "Core/Assert.h"

#include <array>
#include <cmath>
#include <format>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace lmx::render {

namespace {

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
// against. The App's clock is a fixed step per frame rather than wall time -- asset::
// kAnimationBakeRate -- and this restates the number because Render cannot depend on Asset. A
// renderer that stepped by wall time would resolve a different exposure for the same frame on a
// different machine, which is not something a frozen stability tolerance can survive.
constexpr float kExposureFrameSeconds = 1.0f / 60.0f;

constexpr uint32_t kComputeThreadsPerGroup2D = 8;

//======================================================================================================================
uint32_t divRoundUp(uint32_t value, uint32_t divisor) {
    return (value + divisor - 1) / divisor;
}

constexpr uint32_t kShadowMapSize = 2048;

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

} // namespace

//======================================================================================================================
void registerUniformLayoutsForCapture() {
    SceneStage::registerObjectLayoutForCapture();
    ShadowStage::registerUniformLayoutsForCapture();
    SceneStage::registerPassLayoutsForCapture();
}

//======================================================================================================================
rhi::Result<std::unique_ptr<Renderer>> Renderer::create(rhi::Device& device, uint32_t width,
                                                        uint32_t height, bool cpuReadback) {
    LMX_ASSERT(width > 0 && height > 0, "Renderer::create: width and height must be non-zero");

    // Registration is idempotent and keeps capture startup independent of Renderer state.
    registerUniformLayoutsForCapture();

    std::unique_ptr<Renderer> self(new Renderer(device, cpuReadback));

    if (auto stage = ShadowStage::create(device); stage) {
        self->m_shadowStage = std::move(*stage);
    } else {
        return std::unexpected(stage.error());
    }
    if (auto stage = SceneStage::create(device, kSceneColorFormat); stage) {
        self->m_sceneStage = std::move(*stage);
    } else {
        return std::unexpected(stage.error());
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
    const ReconstructionSelection selection =
        temporalEnabled
            ? m_temporalResolve->prepare(view.temporal.reconstruction, m_width, m_height)
            : ReconstructionSelection{view.temporal.reconstruction, VendorFallback::None};
    const ReconstructionMode reconstruction = selection.mode;
    const bool vendorTemporal =
        temporalEnabled && reconstruction == ReconstructionMode::VendorTemporal;
    const float renderScale =
        temporalEnabled
            ? (vendorTemporal ? vendorRenderScale(view.temporal.renderScale,
                                                  m_device.capabilities().temporalScaler)
                              : view.temporal.renderScale)
            : 1.0f;

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
    const bool nativeTaa = temporalEnabled && reconstruction == ReconstructionMode::NativeTaa;
    const TemporalDebugView debugView =
        vendorTemporal && nativeOnlyTemporalView(view.temporal.debugView) ? TemporalDebugView::Off
                                                                          : view.temporal.debugView;

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
                                  "lmx.render.sceneDepth", m_temporalResolve->depthUse(0));

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

    const GraphTexture shadowRead =
        m_shadowStage->declare(graph, commands, view,
                               {.shadowMap = shadowMap,
                                .lightViewProj = shadow.viewProj,
                                .whiteTexture = m_whiteTexture.get(),
                                .linearSampler = m_linearSampler.get()});
    const GraphTexture sceneColorRead = m_sceneStage->declare(
        graph, commands, view,
        {.sceneColor = sceneColor,
         .sceneDepth = sceneDepth,
         .shadowRead = shadowRead,
         .motion = motionTargetHandle,
         .reactive = reactiveTargetHandle,
         .exposure = exposureCurrent,
         .camera = cameraState,
         .previousCamera = previousCamera,
         .eyePosition = camera.position,
         .shadowTransform = shadow.shadowTransform,
         .extents = extents,
         .jitterPixels = jitterPixels,
         .clearColor = {clearColor[0], clearColor[1], clearColor[2], clearColor[3]},
         .timeSeconds = timeSeconds,
         .temporalEnabled = temporalEnabled,
         .whiteTexture = m_whiteTexture.get(),
         .flatNormalTexture = m_flatNormalTexture.get(),
         .blackCubeTexture = m_blackCubeTexture.get(),
         .zeroDfgTexture = m_zeroDfgTexture.get(),
         .linearSampler = m_linearSampler.get(),
         .shadowSampler = m_shadowSampler.get(),
         .iblSampler = m_iblSampler.get()});
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
        nativeTaa || vendorTemporal || upscaled ? temporalOutputs.resolved : sceneColorRead;

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
    m_temporalStatus.vendorFallback = selection.fallback;
    m_temporalStatus.vendorName = m_device.capabilities().temporalScaler.name;
    m_temporalStatus.vendorReset = vendorTemporal && m_temporalResolve->vendorReset();
    m_temporalStatus.vendorScalerGeneration = m_temporalResolve->vendorScalerGeneration();
    // The age counts declared temporal frames since the last non-None reason, whatever the mode:
    // both modes leave a real frame in the colour slot, so the count survives a mode switch. A
    // frame with temporal off starts it over, because the history the next temporal frame finds is
    // not the one this count would have described.
    if (!temporalEnabled) {
        m_temporalResolve->recordDisabledFrame();
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
        m_previousMotionUse = nativeTaa || vendorTemporal || debugView != TemporalDebugView::Off
                                  ? rhi::TextureUse::ShaderRead
                                  : rhi::TextureUse::RenderTarget;
        // Only the resolve reads the reactive attachment, so a Raw frame ends with its own write.
        m_previousReactiveUse = nativeTaa || vendorTemporal ? rhi::TextureUse::ShaderRead
                                                            : rhi::TextureUse::RenderTarget;
        m_temporalResolve->recordFrame(slot, reconstruction, debugView, historyValid, upscaled);
    }
    // The scene colour is written and read by every frame. Under Raw at the output extent the
    // commit copy is the last thing to touch it; an upscaled Raw frame samples it in the spatial
    // pass instead of copying it, and under NativeTaa and with temporal off, bloom, the histogram
    // and the display transform all read it and none copies out of it.
    m_previousSceneColorUse = vendorTemporal ? rhi::TextureUse::ExternalRead
                              : temporalEnabled && !nativeTaa && !upscaled
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
