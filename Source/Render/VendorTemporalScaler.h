//----------------------------------------------------------------------------------------------------------------------
/// @file VendorTemporalScaler.h
/// @brief Declares vendor temporal input translation and private-history ownership.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Render/TemporalResolve.h"

namespace lmx::render {

/// True for debug views that require the native accumulation kernel's internal state.
bool nativeOnlyTemporalView(TemporalDebugView view);

/// Clamps input/output scale into the engine and supported vendor intervals.
float vendorRenderScale(float scale, const rhi::TemporalScalerSupport& support);

/// Vendor history resets independently of the engine's interchangeable color/depth histories.
bool vendorHistoryReset(HistoryResetReason reason, ReconstructionMode previousMode, bool recreated);

/// Converts engine UV motion and NDC jitter to pixel motion-to-previous and texture-space jitter.
rhi::TemporalScaleParams vendorTemporalParams(const FrameExtents& extents, glm::vec2 jitterPixels);

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
    explicit VendorTemporalScaler(rhi::Device& device) : m_device(device) {}

    /// Prepares a vendor request and its packing pipeline, or reports native fallback.
    ReconstructionSelection prepare(ReconstructionMode requested, uint32_t width, uint32_t height);

    /// Creates only the engine packing pipeline; works without a vendor capability or scaler.
    rhi::Result<void> preparePacking();

    /// Prepared pipeline for the engine-owned, exposure-corrected history diagnostic.
    rhi::ComputePipeline& historyPipeline() const;

    /// Retires the scaler and clears an extent-specific failure after a successful output resize.
    void invalidateOutput();

    /// Declares input translation; the pipeline must have been prepared successfully first.
    VendorTemporalPacked declarePack(RenderGraph& graph, rhi::CommandList& commands,
                                     const TemporalInputs& inputs);

    /// Declares the packing and external passes, exporting this frame's color-slot version.
    GraphTexture declare(RenderGraph& graph, rhi::CommandList& commands,
                         const TemporalInputs& inputs);

    /// Records the frame's effective mode; temporal off is recorded as Raw by the stage.
    void recordMode(ReconstructionMode mode) { m_previousMode = mode; }

    /// Last reset flag passed to the scaler.
    bool reset() const { return m_reset; }

    /// Successful scaler creations across all output extents.
    uint32_t generation() const { return m_generation; }

private:
    rhi::Device& m_device;
    std::unique_ptr<rhi::TemporalScaler> m_scaler;
    std::unique_ptr<rhi::ShaderLibrary> m_packLibrary;
    std::unique_ptr<rhi::ComputePipeline> m_packPipeline;
    std::unique_ptr<rhi::ShaderLibrary> m_historyLibrary;
    std::unique_ptr<rhi::ComputePipeline> m_historyPipeline;
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
