//----------------------------------------------------------------------------------------------------------------------
/// @file RendererTemporal.cpp
/// @brief Allocates temporal targets and adopts them, with their slot states, through resize.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Renderer/RendererInternal.h"

#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/GpuTexture.h"
#include "Render/Common/Formats.h"
#include "Render/Passes/Temporal/TemporalResolve.h"

#include <utility>

namespace lmx::render {

namespace {

//======================================================================================================================
// Seeds a slot owner the resize just created: its diagnostic use, and its physical cells from its
// own creation layout.
void seedTemporalSlot(TextureStateTable& states, const engine::GpuTexture& texture,
                      TextureUse initial) {
    states.setLogicalUse(texture.rhi(), initial);
    states.seedPhysical(texture.rhi(), texture.desc().mips, texture.desc().layers,
                        texture.initialLayout());
}

} // namespace

//======================================================================================================================
lmx::Result<Renderer::TemporalTargets> Renderer::createTemporalTargets(uint32_t width,
                                                                       uint32_t height) const {
    LMX_ASSERT(width > 0 && height > 0,
               "Renderer::createTemporalTargets: the render extent must be non-empty");

    // Sampled as well as rendered into: the resolve reads motion, and a GPU test compares it
    // against an arithmetic oracle through a staged copy rather than against a picture.
    auto motion =
        engine::GpuTexture::create(m_gpu.device(), {.format = kMotionFormat,
                                                    .extent = {width, height, 1},
                                                    .usage = rojoRHI::TextureUsage::RenderTarget |
                                                             rojoRHI::TextureUsage::Sampled,
                                                    .label = "lmx.render.motion"});
    if (!motion) {
        return std::unexpected(motion.error());
    }
    // The per-pixel reactive weight: the scene pass's second extra attachment, read by the resolve
    // alone. One byte per pixel, and colour-renderable rather than storage, because a fragment
    // writes it as an ordinary attachment.
    auto reactive =
        engine::GpuTexture::create(m_gpu.device(), {.format = kReactiveFormat,
                                                    .extent = {width, height, 1},
                                                    .usage = rojoRHI::TextureUsage::RenderTarget |
                                                             rojoRHI::TextureUsage::Sampled,
                                                    .label = "lmx.render.reactive"});
    if (!reactive) {
        return std::unexpected(reactive.error());
    }
    return TemporalTargets{std::move(*motion), std::move(*reactive)};
}

//======================================================================================================================
void Renderer::adoptTemporalTargets(TemporalTargets targets, TemporalSlots slots) {
    // Both attachments start as their own attachment write leaves them, the scene pass's.
    seedReplacement(m_textureStates, m_motion, targets.motion, TextureUse::RenderTarget);
    seedReplacement(m_textureStates, m_reactive, targets.reactive, TextureUse::RenderTarget);
    m_motion = std::move(targets.motion);
    m_reactive = std::move(targets.reactive);

    // The outgoing slots' entries go with them. The stage holds none before its first adoption,
    // which create() performs through resize().
    if (m_temporalResolve->depthBytes() > 0) {
        for (uint32_t slot = 0; slot < 2; ++slot) {
            m_textureStates.forget(m_temporalResolve->depthTexture(slot).rhi());
            m_textureStates.forget(m_temporalResolve->colorTexture(slot).rhi());
        }
    }
    m_temporalResolve->adoptSlots(std::move(slots));
    // The slots' contents went with the old textures, and the extent change resets history anyway:
    // a colour slot starts as the commit copy leaves it, a depth slot as sampled.
    for (uint32_t slot = 0; slot < 2; ++slot) {
        seedTemporalSlot(m_textureStates, m_temporalResolve->colorTexture(slot),
                         TextureUse::CopyDestination);
        seedTemporalSlot(m_textureStates, m_temporalResolve->depthTexture(slot),
                         TextureUse::ShaderRead);
    }
}

} // namespace lmx::render
