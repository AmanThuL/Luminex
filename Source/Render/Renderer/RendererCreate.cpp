//----------------------------------------------------------------------------------------------------------------------
/// @file RendererCreate.cpp
/// @brief Creates renderer-owned GPU resources and resizes persistent targets.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Common/StageSetup.h"
#include "Render/Passes/Bloom/BloomStage.h"
#include "Render/Passes/Display/DisplayStage.h"
#include "Render/Passes/Exposure/ExposureStage.h"
#include "Render/Passes/Occlusion/OcclusionReference.h"
#include "Render/Passes/Temporal/VendorTemporalScaler.h"
#include "Render/Passes/Visibility/GpuVisibility.h"
#include "Render/Renderer/Renderer.h"
#include <chrono>

#include "Core/Diagnostics/Assert.h"

#include <array>
#include <cmath>
#include <format>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace lmx::render {

namespace {

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

} // namespace

//======================================================================================================================
void registerUniformLayoutsForCapture() {
    SceneStage::registerSceneTableLayoutsForCapture();
    ShadowStage::registerUniformLayoutsForCapture();
    registerVisibilityLayoutsForCapture();
    LightClusterStage::registerLayoutsForCapture();
    SceneStage::registerPassLayoutsForCapture();
}

//======================================================================================================================
Renderer::Renderer(rojoRHI::Device& device, bool cpuReadback)
    : m_device(device), m_transientPool(device), m_drawSubmission(device),
      m_cpuReadback(cpuReadback) {}

//======================================================================================================================
Renderer::~Renderer() = default;

//======================================================================================================================
rojoRHI::Result<std::unique_ptr<Renderer>> Renderer::create(rojoRHI::Device& device, uint32_t width,
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

    auto displayStage = DisplayStage::create(device);
    if (!displayStage) {
        return std::unexpected(displayStage.error());
    }
    self->m_displayStage = std::move(*displayStage);

    auto exposureStage = ExposureStage::create(device, cpuReadback);
    if (!exposureStage) {
        return std::unexpected(exposureStage.error());
    }
    self->m_exposureStage = std::move(*exposureStage);

    auto bloomStage = BloomStage::create(device);
    if (!bloomStage) {
        return std::unexpected(bloomStage.error());
    }
    self->m_bloomStage = std::move(*bloomStage);

    // The shadow map transitions from depth attachment to sampled texture each frame.
    if (auto shadowMap = device.createTexture({.width = kShadowMapSize,
                                               .height = kShadowMapSize,
                                               .format = rojoRHI::Format::D32Float,
                                               .renderTarget = true,
                                               .sampled = true,
                                               .label = "lmx.render.shadowMap"});
        shadowMap) {
        self->m_shadowMap = std::move(*shadowMap);
    } else {
        return std::unexpected(shadowMap.error());
    }

    if (auto texture =
            createTexel(device, rojoRHI::Format::RGBA8Unorm, rojoRHI::TextureKind::Tex2D,
                        std::as_bytes(std::span{kWhiteTexel}), "lmx.render.whiteFallback");
        texture) {
        self->m_whiteTexture = std::move(*texture);
    } else {
        return std::unexpected(texture.error());
    }
    if (auto texture = createTexel(device, rojoRHI::Format::RGBA8Unorm, rojoRHI::TextureKind::Tex2D,
                                   std::as_bytes(std::span{kFlatNormalTexel}),
                                   "lmx.render.flatNormalFallback");
        texture) {
        self->m_flatNormalTexture = std::move(*texture);
    } else {
        return std::unexpected(texture.error());
    }
    if (auto texture =
            createTexel(device, rojoRHI::Format::RGBA8Unorm, rojoRHI::TextureKind::Cube,
                        std::as_bytes(std::span{kBlackTexel}), "lmx.render.blackCubeFallback");
        texture) {
        self->m_blackCubeTexture = std::move(*texture);
    } else {
        return std::unexpected(texture.error());
    }
    if (auto texture =
            createTexel(device, rojoRHI::Format::RG16Float, rojoRHI::TextureKind::Tex2D,
                        std::as_bytes(std::span{kZeroDfgTexel}), "lmx.render.zeroDfgFallback");
        texture) {
        self->m_zeroDfgTexture = std::move(*texture);
    } else {
        return std::unexpected(texture.error());
    }
    if (auto sampler = device.createSampler({.filter = rojoRHI::FilterMode::Linear,
                                             .addressMode = rojoRHI::AddressMode::Wrap,
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
    if (auto sampler = device.createSampler({.filter = rojoRHI::FilterMode::Linear,
                                             .addressMode = rojoRHI::AddressMode::Clamp,
                                             .maxAnisotropy = 16,
                                             .compare = rojoRHI::CompareFunc::GreaterEqual,
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
    if (auto sampler = self->m_device.createSampler({.filter = rojoRHI::FilterMode::Linear,
                                                     .addressMode = rojoRHI::AddressMode::Clamp,
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
rojoRHI::Result<void> Renderer::resize(uint32_t width, uint32_t height) {
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
    if (m_hzbStage) {
        if (auto result = m_hzbStage->resize(width, height); !result)
            return std::unexpected(result.error());
    }
    return {};
}

//======================================================================================================================
rojoRHI::Result<void> Renderer::createTemporalTargets() {
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

} // namespace lmx::render
