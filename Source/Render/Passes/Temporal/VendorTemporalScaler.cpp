//----------------------------------------------------------------------------------------------------------------------
/// @file VendorTemporalScaler.cpp
/// @brief Translates temporal inputs and declares the capability-selected vendor kernel.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/Temporal/VendorTemporalScaler.h"
#include "Engine/Gpu/CaptureSchema.h"
#include "Render/Common/Dispatch.h"
#include "Render/Common/FrameData.h"
#include "Render/Common/GraphResources.h"
#include "Render/Passes/Temporal/TemporalFrameData.h"
#include "Render/Passes/Temporal/TemporalRoots.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Diagnostics/Log.h"
#include "Core/Math/Scalar.h"
#include "Engine/Gpu/GpuContext.h"
#include "Engine/Gpu/GpuOwned.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>

namespace lmx::render {
namespace {

//======================================================================================================================
// Names the vendor path's roots and its packing record in capture sidecars, field by field.
void registerVendorLayoutsForCapture() {
    using temporal_detail::VendorPackParams;
    engine::registerUniformStruct(
        {"VendorTemporalPackRoot",
         sizeof(VendorTemporalPackRoot),
         {{"paramsAddr", offsetof(VendorTemporalPackRoot, paramsAddr), "Addr"},
          {"exposureAddr", offsetof(VendorTemporalPackRoot, exposureAddr), "Addr"},
          {"motion", offsetof(VendorTemporalPackRoot, motion), "uint"},
          {"reactive", offsetof(VendorTemporalPackRoot, reactive), "uint"},
          {"packedMotion", offsetof(VendorTemporalPackRoot, packedMotion), "uint"},
          {"packedReactive", offsetof(VendorTemporalPackRoot, packedReactive), "uint"},
          {"exposureTexel", offsetof(VendorTemporalPackRoot, exposureTexel), "uint"},
          {"padding0", offsetof(VendorTemporalPackRoot, padding0), "uint"},
          {"padding1", offsetof(VendorTemporalPackRoot, padding1), "uint"},
          {"padding2", offsetof(VendorTemporalPackRoot, padding2), "uint"}}});
    engine::registerUniformStruct(
        {"VendorTemporalHistoryRoot",
         sizeof(VendorTemporalHistoryRoot),
         {{"paramsAddr", offsetof(VendorTemporalHistoryRoot, paramsAddr), "Addr"},
          {"exposureAddr", offsetof(VendorTemporalHistoryRoot, exposureAddr), "Addr"},
          {"depth", offsetof(VendorTemporalHistoryRoot, depth), "uint"},
          {"previousDepth", offsetof(VendorTemporalHistoryRoot, previousDepth), "uint"},
          {"motion", offsetof(VendorTemporalHistoryRoot, motion), "uint"},
          {"history", offsetof(VendorTemporalHistoryRoot, history), "uint"},
          {"reprojected", offsetof(VendorTemporalHistoryRoot, reprojected), "uint"},
          {"sampler", offsetof(VendorTemporalHistoryRoot, sampler), "uint"},
          {"padding0", offsetof(VendorTemporalHistoryRoot, padding0), "uint"},
          {"padding1", offsetof(VendorTemporalHistoryRoot, padding1), "uint"}}});
    engine::registerUniformStruct({"VendorPackParams",
                                   sizeof(VendorPackParams),
                                   {{"width", offsetof(VendorPackParams, width), "uint"},
                                    {"height", offsetof(VendorPackParams, height), "uint"}}});
}

} // namespace

//======================================================================================================================
ReconstructionSelection resolveReconstruction(ReconstructionMode requested,
                                              const engine::UpscalerSupportInfo& support,
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
float vendorRenderScale(float scale, const engine::UpscalerSupportInfo& support) {
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
rojoRHI::UpscaleParams vendorTemporalParams(const FrameExtents& extents, glm::vec2 jitterPixels) {
    const glm::vec2 offset = jitterTexelOffset(jitterPixels);
    // Reversed depth is the upscaler's creation flag (UpscalerFlags::ReversedDepth), not a frame
    // value: prepare() sets it on every upscaler this adapter creates.
    return {.inputContent = {extents.renderWidth, extents.renderHeight},
            .jitterX = offset.x,
            .jitterY = offset.y,
            .motionScaleX = -static_cast<float>(extents.renderWidth),
            .motionScaleY = -static_cast<float>(extents.renderHeight),
            .preExposure = 1.0f,
            .label = "lmx.pass.temporal.vendor"};
}

//======================================================================================================================
ReconstructionSelection VendorTemporalScaler::prepare(ReconstructionMode requested, uint32_t width,
                                                      uint32_t height) {
    const auto support = m_gpu.upscalerSupport();
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
    const auto fail = [&](const lmx::Error& error) {
        m_creationFailed = true;
        LMX_LOG_WARN("{} initialization failed at {}x{}: {}; using Native TAA", support.name, width,
                     height, error.message);
        return resolveReconstruction(requested, support, true);
    };
    if (auto packed = preparePacking(); !packed) {
        return fail(packed.error());
    }
    if (!m_historyLibrary) {
        auto library = m_gpu.loadShaderLibrary("VendorTemporalHistory");
        if (!library) {
            return fail(library.error());
        }
        m_historyLibrary = std::move(*library);
    }
    if (!m_historyPipeline) {
        auto pipeline = engine::adoptGpu(
            m_gpu.device(), m_gpu.device().createPipeline(rojoRHI::ComputePipelineDesc{
                                .compute = {m_historyLibrary.get(), "computeVendorTemporalHistory"},
                                .label = "lmx.pipeline.temporal.vendor.history"}));
        if (!pipeline) {
            return fail(pipeline.error());
        }
        m_historyPipeline = std::move(*pipeline);
    }
    const std::string label = "lmx.temporal.vendor.scaler " + std::string(support.name);
    // Inputs are allocated at the output extent and only their active content shrinks with the
    // render scale, so the upscaler's input is its output; its content range derives from the
    // device's support. Scene-linear colour needs no HDR flag on MetalFX.
    auto scaler = engine::adoptGpu(
        m_gpu.device(),
        m_gpu.device().createUpscaler({.kind = rojoRHI::UpscalerKind::MetalFX,
                                       .input = {width, height},
                                       .output = {width, height},
                                       .flags = rojoRHI::UpscalerFlags::ReversedDepth,
                                       .label = label}));
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
rojoRHI::Pipeline VendorTemporalScaler::historyPipeline() const {
    LMX_ASSERT(m_historyPipeline, "vendor history pipeline must be prepared before declaration");
    return m_historyPipeline.get();
}

//======================================================================================================================
lmx::Result<void> VendorTemporalScaler::preparePacking() {
    if (!m_packLibrary) {
        auto library = m_gpu.loadShaderLibrary("VendorTemporalPack");
        if (!library) {
            return std::unexpected(library.error());
        }
        m_packLibrary = std::move(*library);
    }
    if (!m_packPipeline) {
        auto pipeline = engine::adoptGpu(
            m_gpu.device(), m_gpu.device().createPipeline(rojoRHI::ComputePipelineDesc{
                                .compute = {m_packLibrary.get(), "computeVendorTemporalPack"},
                                .label = "lmx.pipeline.temporal.vendor.pack"}));
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        m_packPipeline = std::move(*pipeline);
        registerVendorLayoutsForCapture();
    }
    return {};
}

//======================================================================================================================
VendorTemporalPacked VendorTemporalScaler::declarePack(RenderGraph& graph,
                                                       rojoRHI::CommandList& commands,
                                                       const TemporalInputs& inputs) {
    LMX_ASSERT(m_packPipeline, "vendor packing must be prepared before declaration");
    const auto makeTexture = [&](rojoRHI::Format format, std::string_view label, uint32_t width,
                                 uint32_t height) {
        return graph.createTexture({.width = width,
                                    .height = height,
                                    .format = format,
                                    .sampled = true,
                                    .storageWrite = true},
                                   label);
    };
    VendorTemporalPacked packed{
        makeTexture(rojoRHI::Format::RG16Float, "lmx.render.vendorMotion",
                    inputs.extents.outputWidth, inputs.extents.outputHeight),
        makeTexture(rojoRHI::Format::R8Unorm, "lmx.render.vendorReactive",
                    inputs.extents.outputWidth, inputs.extents.outputHeight),
        makeTexture(rojoRHI::Format::R16Float, "lmx.render.vendorExposure", 1, 1)};
    ComputePassDesc desc;
    desc.shaderTextureReads = {inputs.motion, inputs.reactive};
    desc.shaderBufferReads = {inputs.exposure};
    desc.textureWrites = {packed.motion, packed.reactive, packed.exposure};
    graph.addComputePass(
        "lmx.pass.temporal.vendor.pack", std::move(desc),
        [this, &commands, inputs, packed](const PassResources& resources) {
            const auto params = buildVendorPackParams(inputs.extents);
            const auto paramsAddress = frameData(commands, params);
            engine::registerFrameRoot("VendorPackParams", paramsAddress);
            commands.setPipeline(m_packPipeline.get());
            engine::registerFrameRoot(
                "VendorTemporalPackRoot",
                commands.pushRoot(VendorTemporalPackRoot{
                    .paramsAddr = paramsAddress,
                    .exposureAddr = resources.address(inputs.exposure),
                    .motion = static_cast<uint32_t>(resources.view(inputs.motion)),
                    .reactive = static_cast<uint32_t>(resources.view(inputs.reactive)),
                    .packedMotion = static_cast<uint32_t>(resources.storageView(packed.motion, 0)),
                    .packedReactive =
                        static_cast<uint32_t>(resources.storageView(packed.reactive, 0)),
                    .exposureTexel =
                        static_cast<uint32_t>(resources.storageView(packed.exposure, 0))}));
#ifndef NDEBUG
            const auto texture = [&](GraphTexture handle, rojoRHI::Access access) {
                return rojoRHI::ResourceAccess{.texture = resources.texture(handle).texture,
                                               .range = {0, 1, 0, 1},
                                               .stage = rojoRHI::Stage::ComputeShader,
                                               .access = access};
            };
            const std::array<rojoRHI::ResourceAccess, 6> accesses{
                {texture(inputs.motion, rojoRHI::Access::ShaderRead),
                 texture(inputs.reactive, rojoRHI::Access::ShaderRead),
                 texture(packed.motion, rojoRHI::Access::ShaderWrite),
                 texture(packed.reactive, rojoRHI::Access::ShaderWrite),
                 texture(packed.exposure, rojoRHI::Access::ShaderWrite),
                 {.buffer = resources.buffer(inputs.exposure),
                  .size = sizeof(float),
                  .stage = rojoRHI::Stage::ComputeShader,
                  .access = rojoRHI::Access::ShaderRead}}};
            commands.declareAccess(accesses);
#endif
            const auto groups = dispatchGroups2D(params.width, params.height);
            commands.dispatch(groups[0], groups[1], 1);
        });
    return {nextVersion(packed.motion), nextVersion(packed.reactive), nextVersion(packed.exposure)};
}

//======================================================================================================================
GraphTexture VendorTemporalScaler::declare(RenderGraph& graph, rojoRHI::CommandList& commands,
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
            params.color = resources.texture(inputs.sceneColor).texture;
            params.depth = resources.texture(inputs.depth).texture;
            params.motion = resources.texture(packed.motion).texture;
            params.reactive = resources.texture(packed.reactive).texture;
            params.exposure = resources.texture(packed.exposure).texture;
            params.output = resources.texture(inputs.colorSlot).texture;
            commands.upscale(m_scaler.get(), params);
        });
    const GraphTexture output = nextVersion(inputs.colorSlot);
    graph.exportTexture(output);
    return output;
}

} // namespace lmx::render
