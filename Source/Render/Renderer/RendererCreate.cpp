//----------------------------------------------------------------------------------------------------------------------
/// @file RendererCreate.cpp
/// @brief Creates renderer-owned GPU resources and resizes persistent targets.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Common/StageSetup.h"
#ifndef LMX_PENDING_POST
#include "Render/Passes/Bloom/BloomStage.h"
#endif
#include "Render/Passes/Display/DisplayStage.h"
#ifndef LMX_PENDING_POST
#include "Render/Passes/Exposure/ExposureStage.h"
#endif

#ifndef LMX_PENDING_VISIBILITY
#include "Render/Passes/Occlusion/OcclusionReference.h"
#endif

#ifndef LMX_PENDING_TEMPORAL
#include "Render/Passes/Temporal/VendorTemporalScaler.h"
#endif

#ifndef LMX_PENDING_VISIBILITY
#include "Render/Passes/Visibility/GpuVisibility.h"
#endif
#include "Render/Renderer/Renderer.h"
#include "Render/Renderer/RendererInternal.h"
#include "Render/Renderer/RendererStandIns.h"
#include <chrono>

#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/GpuOwned.h"

#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace lmx::render {

namespace {

#ifndef LMX_PENDING_SCENE
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
#endif

} // namespace

//======================================================================================================================
// Seeds a target resize() allocates. A replacement starts from the use its predecessor was left
// in, so a resize changes none of the barriers the next frame records; the first allocation starts
// from `initial`. The predecessor's entry goes with it, so its address carries nothing forward.
void seedReplacement(TextureStateTable& states, const engine::GpuTexture& previous,
                     const rojoRHI::Texture& replacement, TextureUse initial) {
    TextureUse use = initial;
    if (previous) {
        use = states.find(previous.rhi()).value_or(initial);
        states.forget(previous.rhi());
    }
    states.set(replacement, use);
}

//======================================================================================================================
void registerUniformLayoutsForCapture() {
#ifndef LMX_PENDING_SCENE
    SceneStage::registerSceneTableLayoutsForCapture();
    ShadowStage::registerUniformLayoutsForCapture();
#endif

#ifndef LMX_PENDING_VISIBILITY
    registerVisibilityLayoutsForCapture();
#endif

#ifndef LMX_PENDING_LIGHTS
    LightClusterStage::registerLayoutsForCapture();
#endif

#ifndef LMX_PENDING_SCENE
    SceneStage::registerPassLayoutsForCapture();
#endif
}

//======================================================================================================================
Renderer::Renderer(engine::GpuContext& gpu, bool cpuReadback)
    : m_gpu(gpu), m_transientPool(gpu.device()),
#ifndef LMX_PENDING_SCENE
      m_drawSubmission(gpu.device()),
#endif
      m_cpuReadback(cpuReadback) {
}

//======================================================================================================================
Renderer::~Renderer() = default;

//======================================================================================================================
lmx::Result<std::unique_ptr<Renderer>> Renderer::create(engine::GpuContext& gpu, uint32_t width,
                                                        uint32_t height, bool cpuReadback) {
    LMX_ASSERT(width > 0 && height > 0, "Renderer::create: width and height must be non-zero");

    // Registration is idempotent and keeps capture startup independent of Renderer state.
    registerUniformLayoutsForCapture();

    rojoRHI::Device& device = gpu.device();
    std::unique_ptr<Renderer> self(new Renderer(gpu, cpuReadback));

#ifndef LMX_PENDING_SCENE
    {
        auto stage = ShadowStage::create(gpu);
        if (!stage) {
            return std::unexpected(stage.error());
        }
        self->m_shadowStage = std::move(*stage);
    }
    {
        auto stage = SceneStage::create(gpu, kSceneColorFormat);
        if (!stage) {
            return std::unexpected(stage.error());
        }
        self->m_sceneStage = std::move(*stage);
    }
#endif

    auto displayStage = DisplayStage::create(gpu);
    if (!displayStage) {
        return std::unexpected(displayStage.error());
    }
    self->m_displayStage = std::move(*displayStage);

#ifndef LMX_PENDING_POST
    auto exposureStage = ExposureStage::create(gpu, cpuReadback);
    if (!exposureStage) {
        return std::unexpected(exposureStage.error());
    }
    self->m_exposureStage = std::move(*exposureStage);

    auto bloomStage = BloomStage::create(gpu);
    if (!bloomStage) {
        return std::unexpected(bloomStage.error());
    }
    self->m_bloomStage = std::move(*bloomStage);
#endif

#ifndef LMX_PENDING_SCENE
    // The shadow map transitions from depth attachment to sampled texture each frame.
    {
        auto shadowMap = engine::GpuTexture::create(device, {.width = kShadowMapSize,
                                                             .height = kShadowMapSize,
                                                             .format = rojoRHI::Format::D32Float,
                                                             .renderTarget = true,
                                                             .sampled = true,
                                                             .label = "lmx.render.shadowMap"});
        if (!shadowMap) {
            return std::unexpected(shadowMap.error());
        }
        self->m_shadowMap = std::move(*shadowMap);
        // What every frame leaves it as: the scene pass samples it after the shadow pass writes it.
        self->m_textureStates.set(self->m_shadowMap.rhi(), TextureUse::ShaderRead);
    }

    {
        auto texture =
            createTexel(device, rojoRHI::Format::RGBA8Unorm, rojoRHI::TextureKind::Tex2D,
                        std::as_bytes(std::span{kWhiteTexel}), "lmx.render.whiteFallback");
        if (!texture) {
            return std::unexpected(texture.error());
        }
        self->m_whiteTexture = std::move(*texture);
    }
    {
        auto texture = createTexel(device, rojoRHI::Format::RGBA8Unorm, rojoRHI::TextureKind::Tex2D,
                                   std::as_bytes(std::span{kFlatNormalTexel}),
                                   "lmx.render.flatNormalFallback");
        if (!texture) {
            return std::unexpected(texture.error());
        }
        self->m_flatNormalTexture = std::move(*texture);
    }
    {
        auto texture =
            createTexel(device, rojoRHI::Format::RGBA8Unorm, rojoRHI::TextureKind::Cube,
                        std::as_bytes(std::span{kBlackTexel}), "lmx.render.blackCubeFallback");
        if (!texture) {
            return std::unexpected(texture.error());
        }
        self->m_blackCubeTexture = std::move(*texture);
    }
    {
        auto texture =
            createTexel(device, rojoRHI::Format::RG16Float, rojoRHI::TextureKind::Tex2D,
                        std::as_bytes(std::span{kZeroDfgTexel}), "lmx.render.zeroDfgFallback");
        if (!texture) {
            return std::unexpected(texture.error());
        }
        self->m_zeroDfgTexture = std::move(*texture);
    }
    {
        auto sampler =
            engine::adoptGpu(device.createSampler({.filter = rojoRHI::FilterMode::Linear,
                                                   .addressMode = rojoRHI::AddressMode::Wrap,
                                                   .maxAnisotropy = 16,
                                                   .label = "lmx.render.linearSampler"}));
        if (!sampler) {
            return std::unexpected(sampler.error());
        }
        self->m_linearSampler = std::move(*sampler);
    }
    // GreaterEqual is the reversed-Z compare: the sampler answers "lit" where the receiver's own
    // depth is at least the stored one, because nearer to the light is now the larger number.
    // Clamp extends the 0.0 clear outside the fitted shadow footprint, and every receiver depth
    // clears that bar, so that region stays lit exactly as it did under the 1.0 clear before.
    {
        auto sampler =
            engine::adoptGpu(device.createSampler({.filter = rojoRHI::FilterMode::Linear,
                                                   .addressMode = rojoRHI::AddressMode::Clamp,
                                                   .maxAnisotropy = 16,
                                                   .compare = rojoRHI::CompareFunc::GreaterEqual,
                                                   .label = "lmx.render.shadowSampler"}));
        if (!sampler) {
            return std::unexpected(sampler.error());
        }
        self->m_shadowSampler = std::move(*sampler);
    }

    // Clamped, not wrapped: the IBL set is read at the very edge of its domain -- the DFG table at
    // N.V = 1 and at roughness 1 -- where a wrapping sampler would fold the opposite edge's texels
    // into the result. Linear filtering carries the mip filter the prefiltered chain is sampled
    // across; no anisotropy, because neither lookup has a screen-space footprint to be anisotropic
    // about.
    {
        auto sampler = engine::adoptGpu(
            self->m_gpu.device().createSampler({.filter = rojoRHI::FilterMode::Linear,
                                                .addressMode = rojoRHI::AddressMode::Clamp,
                                                .label = "lmx.render.iblSampler"}));
        if (!sampler) {
            return std::unexpected(sampler.error());
        }
        self->m_iblSampler = std::move(*sampler);
    }
#endif

#ifndef LMX_PENDING_TEMPORAL
    // The reconstruction stage owns both history pairs and the passes over them, so it is built
    // before the first resize(), which is what allocates its slots at this renderer's extent.
    {
        auto stage = TemporalResolve::create(gpu, self->m_cpuReadback);
        if (!stage) {
            return std::unexpected(stage.error());
        }
        self->m_temporalResolve = std::move(*stage);
    }
#endif

    {
        auto targets = self->resize(width, height);
        if (!targets) {
            return std::unexpected(targets.error());
        }
    }
    return self;
}

//======================================================================================================================
lmx::Result<void> Renderer::resize(uint32_t width, uint32_t height) {
    LMX_ASSERT(width > 0 && height > 0, "Renderer::resize: width and height must be non-zero");

    // Scene-linear radiance: rendered into by the scene and sky passes, sampled by the display
    // pass. Readable on the same terms as the display target, because a caller that wants to
    // inspect radiance rather than the picture has nowhere else to read it from.
    auto hdrColor =
        engine::GpuTexture::create(m_gpu.device(), {.width = width,
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
    auto color = engine::GpuTexture::create(m_gpu.device(), {.width = width,
                                                             .height = height,
                                                             .format = kDisplayFormat,
                                                             .renderTarget = true,
                                                             .sampled = true,
                                                             .cpuReadback = m_cpuReadback,
                                                             .label = "lmx.render.displayColor"});
    if (!color) {
        return std::unexpected(color.error());
    }
    // Swap the targets only after every allocation succeeds. The scene colour starts as the display
    // transform's read left it; the display target as the sampled image it always ends as.
    seedReplacement(m_textureStates, m_hdrColor, hdrColor->rhi(), TextureUse::ShaderRead);
    seedReplacement(m_textureStates, m_color, color->rhi(), TextureUse::ShaderRead);
    m_hdrColor = std::move(*hdrColor);
    m_color = std::move(*color);
    m_width = width;
    m_height = height;

#ifndef LMX_PENDING_TEMPORAL
    // The temporal targets follow the scene targets' extent, under the same caller idle guarantee
    // this function already requires. The histories' contents are dropped with the old textures,
    // which the extent change makes a reset anyway (HistoryResetReason::ExtentChanged). Depth is
    // among them: it ping-pongs by temporal frame parity, so the stage owns both slots of it.
    {
        auto targets = createTemporalTargets();
        if (!targets) {
            return std::unexpected(targets.error());
        }
    }
    {
        // The outgoing slots' entries go with them. A failed resize keeps those slots, so it puts
        // their entries back.
        const bool replacing = m_temporalResolve->depthBytes() > 0;
        const TemporalSlotUses outgoing =
            replacing ? forgetTemporalSlots(m_textureStates, *m_temporalResolve)
                      : TemporalSlotUses{};
        auto slots = m_temporalResolve->resize(width, height);
        if (!slots) {
            if (replacing) {
                restoreTemporalSlots(m_textureStates, *m_temporalResolve, outgoing);
            }
            return std::unexpected(slots.error());
        }
        // The slots' contents went with the old textures, and the extent change resets history
        // anyway: a colour slot starts as the commit copy leaves it, a depth slot as sampled.
        for (uint32_t slot = 0; slot < 2; ++slot) {
            m_textureStates.set(m_temporalResolve->colorTexture(slot).rhi(),
                                TextureUse::CopyDestination);
            m_textureStates.set(m_temporalResolve->depthTexture(slot).rhi(),
                                TextureUse::ShaderRead);
        }
    }

#endif

#ifndef LMX_PENDING_VISIBILITY
    if (m_hzbStage) {
        {
            auto result = m_hzbStage->resize(width, height, m_textureStates);
            if (!result) {
                return std::unexpected(result.error());
            }
        }
    }
#endif
    auto standIns = createRendererStandIns(m_gpu.device(), width, height);
    if (!standIns) {
        return std::unexpected(standIns.error());
    }
    seedReplacement(m_textureStates, m_standIns.depth, standIns->depth.rhi(),
                    TextureUse::ShaderRead);
    m_standIns = std::move(*standIns);
    return {};
}

} // namespace lmx::render
