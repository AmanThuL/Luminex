//----------------------------------------------------------------------------------------------------------------------
/// @file Pipeline.cpp
/// @brief Implements pipeline creation from shader intermediate code with no binding layout.
//----------------------------------------------------------------------------------------------------------------------

#include "Metal4Internal.h"

#include <cstring>
#include <format>
#include <string>

namespace lmx::experimental::noapi {
namespace {

constexpr MTL::LanguageVersion kShaderLanguageVersion = MTL::LanguageVersion4_0;

// The four bytes every Metal library file starts with. Anything else is treated as MSL source,
// which is the toolchain-free path the prototype needs on a machine without the Metal toolchain.
constexpr std::string_view kMetalLibraryMagic = "MTLB";

//======================================================================================================================
MTL::BlendOperation toMTL(BlendOp op) {
    switch (op) {
    case BlendOp::Subtract:
        return MTL::BlendOperationSubtract;
    case BlendOp::ReverseSubtract:
        return MTL::BlendOperationReverseSubtract;
    case BlendOp::Min:
        return MTL::BlendOperationMin;
    case BlendOp::Max:
        return MTL::BlendOperationMax;
    case BlendOp::Add:
        break;
    }
    return MTL::BlendOperationAdd;
}

//======================================================================================================================
MTL::BlendFactor toMTL(BlendFactor factor) {
    switch (factor) {
    case BlendFactor::Zero:
        return MTL::BlendFactorZero;
    case BlendFactor::SrcAlpha:
        return MTL::BlendFactorSourceAlpha;
    case BlendFactor::OneMinusSrcAlpha:
        return MTL::BlendFactorOneMinusSourceAlpha;
    case BlendFactor::DstAlpha:
        return MTL::BlendFactorDestinationAlpha;
    case BlendFactor::OneMinusDstAlpha:
        return MTL::BlendFactorOneMinusDestinationAlpha;
    case BlendFactor::One:
        break;
    }
    return MTL::BlendFactorOne;
}

//======================================================================================================================
MTL::PrimitiveTopologyClass toTopologyClass(Topology topology) {
    // Metal bakes only the topology *class*; the primitive type itself is a draw-call parameter.
    (void)topology;
    return MTL::PrimitiveTopologyClassTriangle;
}

//======================================================================================================================
MTL::PrimitiveType toPrimitive(Topology topology) {
    return topology == Topology::TriangleStrip ? MTL::PrimitiveTypeTriangleStrip
                                               : MTL::PrimitiveTypeTriangle;
}

//======================================================================================================================
void validateShader(const ShaderCode& code, std::string_view what) {
    LMX_ASSERT(!code.ir.empty(),
               std::format("{}: shader intermediate code must not be empty", what));
    LMX_ASSERT(!code.entryPoint.empty(),
               std::format("{}: shader entry point must not be empty", what));
}

//======================================================================================================================
// Specialization blocks are a struct in the model and indexed scalars in Metal, and Metal's
// function constants cannot be fed an opaque byte block. Refusing is the honest answer; the gap is
// recorded in the evidence document.
void rejectSpecConstants(std::span<const std::byte> specConstants, std::string_view label) {
    LMX_ASSERT(specConstants.empty(),
               std::format("createPipeline: '{}' supplies a specialization block, which this "
                           "prototype cannot express -- Metal function constants take indexed "
                           "scalars, not a struct, so the model's specialization constants are "
                           "unavailable on Metal 4",
                           label));
}

//======================================================================================================================
Result<NS::SharedPtr<MTL::Library>> makeLibrary(Device* device, const ShaderCode& code,
                                                std::string_view label) {
    NS::Error* error = nullptr;
    const bool isMetallib =
        code.ir.size() >= kMetalLibraryMagic.size() &&
        std::memcmp(code.ir.data(), kMetalLibraryMagic.data(), kMetalLibraryMagic.size()) == 0;

    if (isMetallib) {
        dispatch_data_t data = dispatch_data_create(code.ir.data(), code.ir.size(), nullptr,
                                                    DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        NS::SharedPtr<MTL::Library> library =
            NS::TransferPtr(device->mtl->newLibrary(data, &error));
        dispatch_release(data);
        if (!library) {
            return fail(ErrorCode::ShaderLoadFailed,
                        "failed to load the precompiled library for '" + std::string(label) +
                            "': " + describe(error));
        }
        library->setLabel(makeString(label).get());
        return library;
    }

    // Runtime MSL keeps the prototype buildable without the optional Metal toolchain, which is the
    // same fallback the production backend keeps.
    auto libraryDesc = NS::TransferPtr(MTL4::LibraryDescriptor::alloc()->init());
    auto options = NS::TransferPtr(MTL::CompileOptions::alloc()->init());
    options->setLanguageVersion(kShaderLanguageVersion);
    libraryDesc->setOptions(options.get());
    libraryDesc->setName(makeString(label).get());
    libraryDesc->setSource(
        makeString(std::string_view(reinterpret_cast<const char*>(code.ir.data()), code.ir.size()))
            .get());

    NS::SharedPtr<MTL::Library> library =
        NS::TransferPtr(device->compiler->newLibrary(libraryDesc.get(), &error));
    if (!library) {
        return fail(ErrorCode::ShaderLoadFailed, "failed to compile the MSL source for '" +
                                                     std::string(label) + "': " + describe(error));
    }
    library->setLabel(makeString(label).get());
    return library;
}

//======================================================================================================================
NS::SharedPtr<MTL4::LibraryFunctionDescriptor> makeFunction(MTL::Library* library,
                                                            std::string_view entryPoint) {
    auto function = NS::TransferPtr(MTL4::LibraryFunctionDescriptor::alloc()->init());
    function->setLibrary(library);
    function->setName(makeString(entryPoint).get());
    return function;
}

} // namespace

//======================================================================================================================
Result<Pipeline*> createComputePipeline(Device* device, const ComputePipelineDesc& desc) {
    LMX_ASSERT(device != nullptr, "createComputePipeline: device must not be null");
    LMX_ASSERT(!desc.label.empty(), "createComputePipeline: label must not be empty");
    validateShader(desc.compute, "createComputePipeline");
    rejectSpecConstants(desc.specConstants, desc.label);
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    Result<NS::SharedPtr<MTL::Library>> library = makeLibrary(device, desc.compute, desc.label);
    if (!library) {
        return std::unexpected(library.error());
    }

    auto pipelineDesc = NS::TransferPtr(MTL4::ComputePipelineDescriptor::alloc()->init());
    pipelineDesc->setComputeFunctionDescriptor(
        makeFunction(library->get(), desc.compute.entryPoint).get());
    pipelineDesc->setLabel(makeString(desc.label).get());

    NS::Error* error = nullptr;
    auto pipeline = std::make_unique<Pipeline>();
    pipeline->compute = NS::TransferPtr(device->compiler->newComputePipelineState(
        pipelineDesc.get(), /*compilerTaskOptions=*/nullptr, &error));
    if (!pipeline->compute) {
        return fail(ErrorCode::PipelineCreationFailed, "failed to create compute pipeline '" +
                                                           std::string(desc.label) + "' (kernel '" +
                                                           std::string(desc.compute.entryPoint) +
                                                           "'): " + describe(error));
    }

    const uint64_t threads = uint64_t{kThreadgroupWidth} * kThreadgroupHeight;
    if (threads > pipeline->compute->maxTotalThreadsPerThreadgroup()) {
        return fail(ErrorCode::PipelineCreationFailed,
                    "compute pipeline '" + std::string(desc.label) + "' supports at most " +
                        std::to_string(pipeline->compute->maxTotalThreadsPerThreadgroup()) +
                        " threads per threadgroup, below the " + std::to_string(threads) +
                        " this prototype dispatches with");
    }

    pipeline->graphics = false;
    pipeline->label = std::string(desc.label);
    device->livePipelines += 1;
    return pipeline.release();
}

//======================================================================================================================
Result<Pipeline*> createGraphicsPipeline(Device* device, const GraphicsPipelineDesc& desc) {
    LMX_ASSERT(device != nullptr, "createGraphicsPipeline: device must not be null");
    LMX_ASSERT(!desc.label.empty(), "createGraphicsPipeline: label must not be empty");
    validateShader(desc.vertex, "createGraphicsPipeline");
    validateShader(desc.pixel, "createGraphicsPipeline");
    rejectSpecConstants(desc.specConstants, desc.label);
    LMX_ASSERT(desc.raster.colorTargets.size() <= kMaxColorAttachments,
               std::format("createGraphicsPipeline: '{}' declares {} color targets, above the {} "
                           "this prototype supports",
                           desc.label, desc.raster.colorTargets.size(), kMaxColorAttachments));
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    Result<NS::SharedPtr<MTL::Library>> vertexLibrary =
        makeLibrary(device, desc.vertex, desc.label);
    if (!vertexLibrary) {
        return std::unexpected(vertexLibrary.error());
    }
    // The two stages usually name the same intermediate code, so compiling it twice would double
    // the pipeline-creation cost the experiment measures.
    NS::SharedPtr<MTL::Library> pixelLibrary = *vertexLibrary;
    if (desc.pixel.ir.data() != desc.vertex.ir.data() ||
        desc.pixel.ir.size() != desc.vertex.ir.size()) {
        Result<NS::SharedPtr<MTL::Library>> compiled = makeLibrary(device, desc.pixel, desc.label);
        if (!compiled) {
            return std::unexpected(compiled.error());
        }
        pixelLibrary = *compiled;
    }

    auto pipelineDesc = NS::TransferPtr(MTL4::RenderPipelineDescriptor::alloc()->init());
    pipelineDesc->setVertexFunctionDescriptor(
        makeFunction(vertexLibrary->get(), desc.vertex.entryPoint).get());
    pipelineDesc->setFragmentFunctionDescriptor(
        makeFunction(pixelLibrary.get(), desc.pixel.entryPoint).get());
    pipelineDesc->setRasterSampleCount(desc.raster.sampleCount);
    pipelineDesc->setInputPrimitiveTopology(toTopologyClass(desc.raster.topology));
    pipelineDesc->setAlphaToCoverageState(desc.raster.alphaToCoverage
                                              ? MTL4::AlphaToCoverageStateEnabled
                                              : MTL4::AlphaToCoverageStateDisabled);
    pipelineDesc->setLabel(makeString(desc.label).get());

    auto pipeline = std::make_unique<Pipeline>();
    for (size_t index = 0; index < desc.raster.colorTargets.size(); ++index) {
        const ColorTargetDesc& target = desc.raster.colorTargets[index];
        MTL4::RenderPipelineColorAttachmentDescriptor* attachment =
            pipelineDesc->colorAttachments()->object(index);
        attachment->setPixelFormat(toMTL(target.format));
        attachment->setWriteMask(static_cast<MTL::ColorWriteMask>(target.writeMask));
        if (desc.raster.blend != nullptr) {
            // Metal bakes blending into the pipeline, so the model's dynamic blend object does not
            // exist here and `Capabilities::separateBlendState` reports false.
            attachment->setBlendingState(MTL4::BlendStateEnabled);
            attachment->setRgbBlendOperation(toMTL(desc.raster.blend->colorOp));
            attachment->setSourceRGBBlendFactor(toMTL(desc.raster.blend->srcColorFactor));
            attachment->setDestinationRGBBlendFactor(toMTL(desc.raster.blend->dstColorFactor));
            attachment->setAlphaBlendOperation(toMTL(desc.raster.blend->alphaOp));
            attachment->setSourceAlphaBlendFactor(toMTL(desc.raster.blend->srcAlphaFactor));
            attachment->setDestinationAlphaBlendFactor(toMTL(desc.raster.blend->dstAlphaFactor));
        }
        pipeline->colorFormats[index] = target.format;
    }
    pipeline->colorCount = static_cast<uint32_t>(desc.raster.colorTargets.size());
    // MTL4::RenderPipelineDescriptor carries no depth attachment format: the render pass supplies
    // it, so the prototype keeps the declared format only to check the pass it is used in.
    pipeline->depthFormat = desc.raster.depthFormat;

    NS::Error* error = nullptr;
    pipeline->render = NS::TransferPtr(device->compiler->newRenderPipelineState(
        pipelineDesc.get(), /*compilerTaskOptions=*/nullptr, &error));
    if (!pipeline->render) {
        return fail(ErrorCode::PipelineCreationFailed,
                    "failed to create graphics pipeline '" + std::string(desc.label) +
                        "' (vertex '" + std::string(desc.vertex.entryPoint) + "', pixel '" +
                        std::string(desc.pixel.entryPoint) + "'): " + describe(error));
    }

    pipeline->graphics = true;
    pipeline->primitive = toPrimitive(desc.raster.topology);
    pipeline->label = std::string(desc.label);
    device->livePipelines += 1;
    return pipeline.release();
}

//======================================================================================================================
void destroyPipeline(Device* device, Pipeline* pipeline) {
    LMX_ASSERT(device != nullptr, "destroyPipeline: device must not be null");
    LMX_ASSERT(pipeline != nullptr, "destroyPipeline: pipeline must not be null");
    assertNoWorkInFlight(device, "destroyPipeline");

    LMX_ASSERT(device->livePipelines > 0, "destroyPipeline: no pipeline is live on this device");
    device->livePipelines -= 1;
    delete pipeline;
}

} // namespace lmx::experimental::noapi
