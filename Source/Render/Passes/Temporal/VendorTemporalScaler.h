//----------------------------------------------------------------------------------------------------------------------
/// @file VendorTemporalScaler.h
/// @brief Declares vendor temporal input translation and private-history ownership.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Render/Passes/Temporal/TemporalResolve.h"

namespace lmx::render {

/// Clamps input/output scale into the engine and supported vendor intervals.
float vendorRenderScale(float scale, const rojoRHI::TemporalScalerSupport& support);

/// Vendor history resets independently of the engine's interchangeable color/depth histories.
bool vendorHistoryReset(HistoryResetReason reason, ReconstructionMode previousMode, bool recreated);

/// Converts engine UV motion and NDC jitter to pixel motion-to-previous and texture-space jitter.
rojoRHI::TemporalScaleParams vendorTemporalParams(const FrameExtents& extents,
                                                  glm::vec2 jitterPixels);

/// Translated, graph-owned signals read by the vendor pass after the packing dispatch.
struct VendorTemporalPacked {
    GraphTexture motion;   ///< RG16Float UV motion with invalid sentinels replaced by zero.
    GraphTexture reactive; ///< R8Unorm reactive weight, one wherever motion was invalid.
    GraphTexture exposure; ///< R16Float inverse applied exposure, normalizing pre-exposed input.
};

/// Composed reconstruction kernel adapter. Its scaler lives until an output resize; render-scale
/// changes only change the content rectangle. A failed extent is retried only after a resize.
class VendorTemporalScaler {
public:
    /// Borrows the device, which must outlive this adapter and its encoded work.
    explicit VendorTemporalScaler(rojoRHI::Device& device) : m_device(device) {}

    /// Prepares a vendor request and its packing pipeline, or reports native fallback.
    ReconstructionSelection prepare(ReconstructionMode requested, uint32_t width, uint32_t height);

    /// Creates only the engine packing pipeline; works without a vendor capability or scaler.
    rojoRHI::Result<void> preparePacking();

    /// Prepared pipeline for the engine-owned, exposure-corrected history diagnostic.
    rojoRHI::ComputePipeline& historyPipeline() const;

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
    rojoRHI::Device& m_device;
    std::unique_ptr<rojoRHI::TemporalScaler> m_scaler;
    std::unique_ptr<rojoRHI::ShaderLibrary> m_packLibrary;
    std::unique_ptr<rojoRHI::ComputePipeline> m_packPipeline;
    std::unique_ptr<rojoRHI::ShaderLibrary> m_historyLibrary;
    std::unique_ptr<rojoRHI::ComputePipeline> m_historyPipeline;
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
