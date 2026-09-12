//----------------------------------------------------------------------------------------------------------------------
/// @file VendorTemporalScaler.cpp
/// @brief Translates temporal inputs and declares the capability-selected vendor kernel.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/VendorTemporalScaler.h"

#include "Core/Assert.h"
#include "Core/Log.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace lmx::render {
namespace {
struct VendorPackParams {
    uint32_t width;
    uint32_t height;
};
static_assert(sizeof(VendorPackParams) == 8);
static_assert(offsetof(VendorPackParams, height) == 4);
} // namespace

//======================================================================================================================
ReconstructionSelection resolveReconstruction(ReconstructionMode requested,
                                              const rhi::TemporalScalerSupport& support,
                                              bool creationFailed) {
    if (requested != ReconstructionMode::VendorTemporal) {
        return {requested, VendorFallback::None};
    }
    if (!support.available) {
        return {ReconstructionMode::NativeTaa, VendorFallback::Unsupported};
    }
    if (creationFailed) {
        return {ReconstructionMode::NativeTaa, VendorFallback::CreationFailed};
    }
    return {requested, VendorFallback::None};
}

//======================================================================================================================
bool nativeOnlyTemporalView(TemporalDebugView view) {
    return view == TemporalDebugView::RejectionMask || view == TemporalDebugView::BlendWeight ||
           view == TemporalDebugView::HistoryAge;
}

//======================================================================================================================
float vendorRenderScale(float scale, const rhi::TemporalScalerSupport& support) {
    if (!support.available) {
        return scale;
    }
    const float minimum = std::max(kMinRenderScale, support.minInputScale);
    const float maximum = std::min(kMaxRenderScale, support.maxInputScale);
    LMX_ASSERT(minimum <= maximum, "vendor temporal scale interval must overlap engine scales");
    return std::clamp(scale, minimum, maximum);
}

//======================================================================================================================
bool vendorHistoryReset(HistoryResetReason reason, ReconstructionMode previousMode,
                        bool recreated) {
    return reason != HistoryResetReason::None ||
           previousMode != ReconstructionMode::VendorTemporal || recreated;
}

//======================================================================================================================
rhi::TemporalScaleParams vendorTemporalParams(const FrameExtents& extents, glm::vec2 jitterPixels) {
    const glm::vec2 offset = jitterTexelOffset(jitterPixels);
    return {.inputContentWidth = extents.renderWidth,
            .inputContentHeight = extents.renderHeight,
            .jitterOffsetX = offset.x,
            .jitterOffsetY = offset.y,
            .motionScaleX = -static_cast<float>(extents.renderWidth),
            .motionScaleY = -static_cast<float>(extents.renderHeight),
            .preExposure = 1.0f,
            .reversedDepth = true,
            .label = "lmx.pass.temporal.vendor"};
}

//======================================================================================================================
ReconstructionSelection VendorTemporalScaler::prepare(ReconstructionMode requested, uint32_t width,
                                                      uint32_t height) {
    const auto& support = m_device.capabilities().temporalScaler;
    auto selection = resolveReconstruction(requested, support);
    if (selection.mode != ReconstructionMode::VendorTemporal) {
        if (selection.fallback == VendorFallback::Unsupported && !m_warnedUnsupported) {
            LMX_LOG_WARN("vendor temporal reconstruction unavailable; using Native TAA");
            m_warnedUnsupported = true;
        }
        return selection;
    }
    if (width != m_width || height != m_height) {
        m_scaler.reset();
        m_width = width;
        m_height = height;
        m_creationFailed = false;
    }
    if (m_creationFailed || m_scaler) {
        return resolveReconstruction(requested, support, m_creationFailed);
    }
    const auto fail = [&](const rhi::Error& error) {
        m_creationFailed = true;
        LMX_LOG_WARN("{} initialization failed at {}x{}: {}; using Native TAA", support.name, width,
                     height, error.message);
        return resolveReconstruction(requested, support, true);
    };
    if (auto packed = preparePacking(); !packed) {
        return fail(packed.error());
    }
    if (!m_historyLibrary) {
        auto library = m_device.loadShaderLibrary("Shaders/VendorTemporalHistory");
        if (!library) {
            return fail(library.error());
        }
        m_historyLibrary = std::move(*library);
    }
    if (!m_historyPipeline) {
        auto pipeline =
            m_device.createComputePipeline({.library = m_historyLibrary.get(),
                                            .computeEntry = "computeVendorTemporalHistory",
                                            .threadsPerThreadgroup = {8, 8, 1},
                                            .label = "lmx.pipeline.temporal.vendor.history"});
        if (!pipeline) {
            return fail(pipeline.error());
        }
        m_historyPipeline = std::move(*pipeline);
    }
    const std::string label = "lmx.temporal.vendor.scaler " + std::string(support.name);
    auto scaler = m_device.createTemporalScaler({.inputWidth = width,
                                                 .inputHeight = height,
                                                 .outputWidth = width,
                                                 .outputHeight = height,
                                                 .minInputScale = support.minInputScale,
                                                 .maxInputScale = support.maxInputScale,
                                                 .label = label});
    if (!scaler) {
        return fail(scaler.error());
    }
    m_scaler = std::move(*scaler);
    ++m_generation;
    m_recreated = true;
    return selection;
}

//======================================================================================================================
void VendorTemporalScaler::invalidateOutput() {
    m_scaler.reset();
    m_width = 0;
    m_height = 0;
    m_creationFailed = false;
}

//======================================================================================================================
rhi::ComputePipeline& VendorTemporalScaler::historyPipeline() const {
    LMX_ASSERT(m_historyPipeline, "vendor history pipeline must be prepared before declaration");
    return *m_historyPipeline;
}

//======================================================================================================================
rhi::Result<void> VendorTemporalScaler::preparePacking() {
    if (!m_packLibrary) {
        auto library = m_device.loadShaderLibrary("Shaders/VendorTemporalPack");
        if (!library) {
            return std::unexpected(library.error());
        }
        m_packLibrary = std::move(*library);
    }
    if (!m_packPipeline) {
        auto pipeline =
            m_device.createComputePipeline({.library = m_packLibrary.get(),
                                            .computeEntry = "computeVendorTemporalPack",
                                            .threadsPerThreadgroup = {8, 8, 1},
                                            .label = "lmx.pipeline.temporal.vendor.pack"});
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        m_packPipeline = std::move(*pipeline);
    }
    return {};
}

//======================================================================================================================
VendorTemporalPacked VendorTemporalScaler::declarePack(RenderGraph& graph,
                                                       rhi::CommandList& commands,
                                                       const TemporalInputs& inputs) {
    LMX_ASSERT(m_packPipeline, "vendor packing must be prepared before declaration");
    const auto makeTexture = [&](rhi::Format format, std::string_view label, uint32_t width,
                                 uint32_t height) {
        return graph.createTexture({.width = width,
                                    .height = height,
                                    .format = format,
                                    .sampled = true,
                                    .storageWrite = true},
                                   label);
    };
    VendorTemporalPacked packed{
        makeTexture(rhi::Format::RG16Float, "lmx.render.vendorMotion", inputs.extents.outputWidth,
                    inputs.extents.outputHeight),
        makeTexture(rhi::Format::R8Unorm, "lmx.render.vendorReactive", inputs.extents.outputWidth,
                    inputs.extents.outputHeight),
        makeTexture(rhi::Format::R16Float, "lmx.render.vendorExposure", 1, 1)};
    ComputePassDesc desc;
    desc.shaderTextureReads = {inputs.motion, inputs.reactive};
    desc.bufferReads = {inputs.exposure};
    desc.textureWrites = {packed.motion, packed.reactive, packed.exposure};
    graph.addComputePass(
        "lmx.pass.temporal.vendor.pack", std::move(desc),
        [this, &commands, inputs, packed](const PassResources& resources) {
            const auto texture = [&](GraphTexture handle) {
                auto result = resources.texture(handle);
                LMX_ASSERT(result.has_value(), result.error().message);
                return *result;
            };
            auto exposure = resources.buffer(inputs.exposure);
            LMX_ASSERT(exposure.has_value(), exposure.error().message);
            commands.bindComputePipeline(*m_packPipeline);
            commands.bindTexture(0, *texture(inputs.motion));
            commands.bindTexture(1, *texture(inputs.reactive));
            commands.bindStorageTexture(2, *texture(packed.motion), {}, rhi::StorageAccess::Write);
            commands.bindStorageTexture(3, *texture(packed.reactive), {},
                                        rhi::StorageAccess::Write);
            commands.bindStorageTexture(4, *texture(packed.exposure), {},
                                        rhi::StorageAccess::Write);
            commands.bindStorageBuffer(0, **exposure, rhi::StorageAccess::Read);
            const VendorPackParams params{inputs.extents.renderWidth, inputs.extents.renderHeight};
            commands.bindFrameData(1, params);
            commands.dispatch((params.width + 7) / 8, (params.height + 7) / 8, 1);
        });
    return {nextVersion(packed.motion), nextVersion(packed.reactive), nextVersion(packed.exposure)};
}

//======================================================================================================================
GraphTexture VendorTemporalScaler::declare(RenderGraph& graph, rhi::CommandList& commands,
                                           const TemporalInputs& inputs) {
    LMX_ASSERT(m_scaler, "vendor scaler must be prepared before declaration");
    const VendorTemporalPacked packed = declarePack(graph, commands, inputs);
    ExternalPassDesc desc;
    desc.textureReads = {inputs.sceneColor, inputs.depth, packed.motion, packed.reactive,
                         packed.exposure};
    desc.textureWrites = {inputs.colorSlot};
    auto params = vendorTemporalParams(inputs.extents, inputs.camera.jitterPixels);
    m_reset = vendorHistoryReset(inputs.resetReason, m_previousMode, m_recreated);
    params.reset = m_reset;
    m_recreated = false;
    graph.addExternalPass(
        "lmx.pass.temporal.vendor", std::move(desc),
        [this, &commands, inputs, packed, params](const PassResources& resources) mutable {
            const auto texture = [&](GraphTexture handle) {
                auto result = resources.texture(handle);
                LMX_ASSERT(result.has_value(), result.error().message);
                return *result;
            };
            params.color = texture(inputs.sceneColor);
            params.depth = texture(inputs.depth);
            params.motion = texture(packed.motion);
            params.reactive = texture(packed.reactive);
            params.exposure = texture(packed.exposure);
            params.output = texture(inputs.colorSlot);
            commands.temporalScale(*m_scaler, params);
        });
    const GraphTexture output = nextVersion(inputs.colorSlot);
    graph.exportTexture(output);
    return output;
}

} // namespace lmx::render
