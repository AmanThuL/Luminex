//----------------------------------------------------------------------------------------------------------------------
/// @file RendererTemporal.cpp
/// @brief Creates temporal targets and preserves slot states through resize.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Renderer/RendererInternal.h"

#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/GpuTexture.h"
#include "Render/Common/Formats.h"
#include "Render/Passes/Temporal/TemporalResolve.h"

#include <utility>

namespace lmx::render {

//======================================================================================================================
// Drops the temporal slots' entries before resize() replaces the slots, returning them so a failed
// resize, which keeps the old slots, can put them back with restoreTemporalSlots().
TemporalSlotUses forgetTemporalSlots(TextureStateTable& states, TemporalResolve& temporal) {
    TemporalSlotUses outgoing{};
    for (uint32_t slot = 0; slot < 2; ++slot) {
        outgoing[slot] = states.find(temporal.depthTexture(slot).rhi());
        outgoing[2 + slot] = states.find(temporal.colorTexture(slot).rhi());
        states.forget(temporal.depthTexture(slot).rhi());
        states.forget(temporal.colorTexture(slot).rhi());
    }
    return outgoing;
}

//======================================================================================================================
// Puts back the entries forgetTemporalSlots() dropped, for slots a failed resize kept.
void restoreTemporalSlots(TextureStateTable& states, TemporalResolve& temporal,
                          const TemporalSlotUses& outgoing) {
    for (uint32_t slot = 0; slot < 2; ++slot) {
        if (outgoing[slot]) {
            states.set(temporal.depthTexture(slot).rhi(), *outgoing[slot]);
        }
        if (outgoing[2 + slot]) {
            states.set(temporal.colorTexture(slot).rhi(), *outgoing[2 + slot]);
        }
    }
}

//======================================================================================================================
lmx::Result<void> Renderer::createTemporalTargets() {
    LMX_ASSERT(m_width > 0 && m_height > 0,
               "Renderer::createTemporalTargets: the render extent must be non-empty");

    // Readable on the same terms as the scene targets: motion is the one output a GPU test has to
    // compare against an arithmetic oracle rather than against a picture.
    auto motion = engine::GpuTexture::create(m_gpu.device(), {.width = m_width,
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
    auto reactive = engine::GpuTexture::create(m_gpu.device(), {.width = m_width,
                                                                .height = m_height,
                                                                .format = kReactiveFormat,
                                                                .renderTarget = true,
                                                                .sampled = true,
                                                                .cpuReadback = m_cpuReadback,
                                                                .label = "lmx.render.reactive"});
    if (!reactive) {
        return std::unexpected(reactive.error());
    }

    // Both start as their own attachment write leaves them, the scene pass's.
    seedReplacement(m_textureStates, m_motion, motion->rhi(), TextureUse::RenderTarget);
    seedReplacement(m_textureStates, m_reactive, reactive->rhi(), TextureUse::RenderTarget);
    m_motion = std::move(*motion);
    m_reactive = std::move(*reactive);
    return {};
}

} // namespace lmx::render
