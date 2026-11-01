//----------------------------------------------------------------------------------------------------------------------
/// @file RendererTemporal.cpp
/// @brief Creates temporal targets and replaces slot states through resize.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Renderer/RendererInternal.h"

#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/GpuTexture.h"
#include "Render/Common/Formats.h"
#include "Render/Passes/Temporal/TemporalResolve.h"

#include <array>
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
lmx::Result<void> Renderer::createTemporalTargets() {
    LMX_ASSERT(m_width > 0 && m_height > 0,
               "Renderer::createTemporalTargets: the render extent must be non-empty");

    // Sampled as well as rendered into: the resolve reads motion, and a GPU test compares it
    // against an arithmetic oracle through a staged copy rather than against a picture.
    auto motion =
        engine::GpuTexture::create(m_gpu.device(), {.format = kMotionFormat,
                                                    .extent = {m_width, m_height, 1},
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
                                                    .extent = {m_width, m_height, 1},
                                                    .usage = rojoRHI::TextureUsage::RenderTarget |
                                                             rojoRHI::TextureUsage::Sampled,
                                                    .label = "lmx.render.reactive"});
    if (!reactive) {
        return std::unexpected(reactive.error());
    }

    // Both start as their own attachment write leaves them, the scene pass's.
    seedReplacement(m_textureStates, m_motion, *motion, TextureUse::RenderTarget);
    seedReplacement(m_textureStates, m_reactive, *reactive, TextureUse::RenderTarget);
    m_motion = std::move(*motion);
    m_reactive = std::move(*reactive);
    return {};
}

//======================================================================================================================
lmx::Result<void> Renderer::resizeTemporalSlots() {
    // The outgoing identities, depth then colour per slot. The stage holds none before its first
    // resize, which create() performs through this function.
    const bool replacing = m_temporalResolve->depthBytes() > 0;
    std::array<rojoRHI::Texture, 4> outgoing{};
    if (replacing) {
        for (uint32_t slot = 0; slot < 2; ++slot) {
            outgoing[slot] = m_temporalResolve->depthTexture(slot).rhi();
            outgoing[2 + slot] = m_temporalResolve->colorTexture(slot).rhi();
        }
    }
    auto slots = m_temporalResolve->resize(m_width, m_height);
    if (!slots) {
        return std::unexpected(slots.error());
    }
    // The outgoing entries go with their slots. The slots' contents went with the old textures,
    // and the extent change resets history anyway: a colour slot starts as the commit copy leaves
    // it, a depth slot as sampled.
    if (replacing) {
        for (const rojoRHI::Texture texture : outgoing) {
            m_textureStates.forget(texture);
        }
    }
    for (uint32_t slot = 0; slot < 2; ++slot) {
        seedTemporalSlot(m_textureStates, m_temporalResolve->colorTexture(slot),
                         TextureUse::CopyDestination);
        seedTemporalSlot(m_textureStates, m_temporalResolve->depthTexture(slot),
                         TextureUse::ShaderRead);
    }
    return {};
}

} // namespace lmx::render
