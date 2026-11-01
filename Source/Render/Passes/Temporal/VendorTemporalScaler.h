//----------------------------------------------------------------------------------------------------------------------
/// @file VendorTemporalScaler.h
/// @brief Declares vendor temporal input translation and private-history ownership.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Core/Diagnostics/Result.h"
#include "Render/Passes/Temporal/TemporalResolve.h"

#include <cstdint>
#include <string_view>

namespace lmx::render {

/// Clamps input/output scale into the engine and supported vendor intervals.
float vendorRenderScale(float scale, const engine::UpscalerSupportInfo& support);

/// Vendor history resets independently of the engine's interchangeable color/depth histories.
bool vendorHistoryReset(HistoryResetReason reason, ReconstructionMode previousMode, bool recreated);

/// Converts engine UV motion and NDC jitter to pixel motion-to-previous and texture-space jitter,
/// with the render extent as the active input content. Textures and the reset flag stay unset.
rojoRHI::UpscaleParams vendorTemporalParams(const FrameExtents& extents, glm::vec2 jitterPixels);

/// The MetalFX upscaler the adapter creates at a `width` x `height` output: input equal to output,
/// because only the active content shrinks with the render scale, and reversed depth as a creation
/// flag. Scene-linear colour needs no HDR flag on MetalFX. `label` must outlive the creation call.
rojoRHI::UpscalerDesc vendorUpscalerDesc(uint32_t width, uint32_t height, std::string_view label);

namespace temporal_detail {

/// A test-only seam over vendor scaler creation, which a real device offers no way to refuse.
/// While `refuse` is set, every creation the adapter attempts fails as a backend refusal would,
/// without calling Device::createUpscaler; `attempts` counts every attempt, refused or not.
/// Production code never writes it; a test that does restores the default before it returns.
struct VendorCreationProbe {
    bool refuse = false;   ///< Fail each attempt instead of creating the upscaler.
    uint32_t attempts = 0; ///< Creations attempted since the probe was last reset.
};

/// The process-wide probe, read and written on the renderer thread only.
VendorCreationProbe& vendorCreationProbe();

} // namespace temporal_detail

/// Translated, graph-owned signals read by the vendor pass after the packing dispatch.
struct VendorTemporalPacked {
    GraphTexture motion;   ///< RG16Float UV motion with invalid sentinels replaced by zero.
    GraphTexture reactive; ///< R8Unorm reactive weight, one wherever motion was invalid.
    GraphTexture exposure; ///< R16Float inverse applied exposure, normalizing pre-exposed input.
};

/// Composed reconstruction kernel adapter. Its upscaler lives until an output resize; render-scale
/// changes only change the content rectangle. A failed extent is retried only after a resize.
class VendorTemporalScaler {
public:
    /// Borrows the GPU context, which must outlive this adapter and its encoded work.
    explicit VendorTemporalScaler(engine::GpuContext& gpu) : m_gpu(gpu) {}

    /// Prepares a vendor request and its packing pipeline, or reports native fallback.
    ReconstructionSelection prepare(ReconstructionMode requested, uint32_t width, uint32_t height);

    /// Creates only the engine packing pipeline; works without a vendor capability or scaler.
    lmx::Result<void> preparePacking();

    /// Prepared pipeline for the engine-owned, exposure-corrected history diagnostic.
    rojoRHI::Pipeline historyPipeline() const;

    /// Retires the scaler and clears an extent-specific failure after a successful output resize.
    void invalidateOutput();

    /// Declares input translation; the pipeline must have been prepared successfully first.
    VendorTemporalPacked declarePack(RenderGraph& graph, rojoRHI::CommandList& commands,
                                     const TemporalInputs& inputs);

    /// Declares the packing and external passes, exporting this frame's color-slot version.
    GraphTexture declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                         const TemporalInputs& inputs);

    /// Records the frame's effective mode; temporal off is recorded as Raw by the stage.
    void recordMode(ReconstructionMode mode) { m_previousMode = mode; }

    /// Last reset flag passed to the scaler.
    bool reset() const { return m_reset; }

    /// Successful scaler creations across all output extents.
    uint32_t generation() const { return m_generation; }

private:
    engine::GpuContext& m_gpu;
    engine::GpuOwned<rojoRHI::Upscaler> m_scaler;
    engine::GpuOwned<rojoRHI::ShaderLibrary> m_packLibrary;
    engine::GpuOwned<rojoRHI::Pipeline> m_packPipeline;
    engine::GpuOwned<rojoRHI::ShaderLibrary> m_historyLibrary;
    engine::GpuOwned<rojoRHI::Pipeline> m_historyPipeline;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_generation = 0;
    bool m_creationFailed = false;
    bool m_warnedUnsupported = false;
    bool m_recreated = false;
    bool m_reset = false;
    ReconstructionMode m_previousMode = ReconstructionMode::Raw;
};

} // namespace lmx::render
