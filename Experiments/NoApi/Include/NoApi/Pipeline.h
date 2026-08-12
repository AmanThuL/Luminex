//----------------------------------------------------------------------------------------------------------------------
/// @file Pipeline.h
/// @brief Declares pipeline creation without any binding layout description.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "NoApi/Handles.h"
#include "NoApi/Result.h"
#include "NoApi/Types.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace lmx::experimental::noapi {

/// Names one shader entry point inside a block of intermediate code.
struct ShaderCode {
    /// Precompiled or textual shader intermediate code; must be non-empty.
    std::span<const std::byte> ir;
    std::string_view entryPoint; ///< Entry point name inside `ir`; must be non-empty.
};

/// Names a blending operation applied to a color target.
enum class BlendOp : uint8_t {
    Add,             ///< Source plus destination.
    Subtract,        ///< Source minus destination.
    ReverseSubtract, ///< Destination minus source.
    Min,             ///< Component-wise minimum.
    Max,             ///< Component-wise maximum.
};

/// Names a blending factor applied to a color or alpha term.
enum class BlendFactor : uint8_t {
    Zero,             ///< Multiply by zero.
    One,              ///< Multiply by one.
    SrcAlpha,         ///< Multiply by the source alpha.
    OneMinusSrcAlpha, ///< Multiply by one minus the source alpha.
    DstAlpha,         ///< Multiply by the destination alpha.
    OneMinusDstAlpha, ///< Multiply by one minus the destination alpha.
};

/// Describes a fixed-function blending equation.
///
/// The model would prefer this state to be settable outside the pipeline, so that changing a blend
/// mode does not create a pipeline permutation. Whether a target can do that is reported by
/// `Capabilities::separateBlendState`; where it cannot, this descriptor is embedded and the
/// permutation is real. The prototype therefore exposes blending only in this embedded form and
/// never pretends the dynamic form exists.
struct BlendDesc {
    BlendOp colorOp = BlendOp::Add;                 ///< Operation combining the color terms.
    BlendFactor srcColorFactor = BlendFactor::One;  ///< Factor applied to the source color.
    BlendFactor dstColorFactor = BlendFactor::Zero; ///< Factor applied to the destination color.
    BlendOp alphaOp = BlendOp::Add;                 ///< Operation combining the alpha terms.
    BlendFactor srcAlphaFactor = BlendFactor::One;  ///< Factor applied to the source alpha.
    BlendFactor dstAlphaFactor = BlendFactor::Zero; ///< Factor applied to the destination alpha.
};

/// Describes one color target of a graphics pipeline.
struct ColorTargetDesc {
    Format format = Format::Undefined; ///< Attachment format the pipeline is compiled against.
    /// Channel write mask, low bit red; zero disables the target's writes.
    uint8_t writeMask = 0xF;
};

/// Describes the rasterizer state a graphics pipeline must bake.
///
/// Everything a target can change without recompiling microcode is deliberately absent here and is
/// a command-buffer operation instead: viewport, scissor, cull mode, winding, depth bias, and the
/// depth-stencil state object. What remains is state that changes the generated code.
struct RasterDesc {
    Topology topology = Topology::TriangleList; ///< Primitive assembly rule.
    uint8_t sampleCount = 1;                    ///< Samples per pixel.
    bool alphaToCoverage = false;               ///< Whether alpha is converted to a coverage mask.
    Format depthFormat = Format::Undefined;     ///< Depth attachment format, or undefined for none.
    /// Color attachment formats, in attachment order.
    std::span<const ColorTargetDesc> colorTargets;
    /// Optional embedded blend state applied to every target.
    const BlendDesc* blend = nullptr;
};

/// Describes a compute pipeline.
struct ComputePipelineDesc {
    ShaderCode compute; ///< Compute entry point.
    /// Specialization block matching the shader's declaration; may be empty.
    std::span<const std::byte> specConstants;
    std::string_view label; ///< Debug label; must be non-empty.
};

/// Describes a vertex plus pixel graphics pipeline.
///
/// There is no vertex layout, no binding layout, and no root signature: vertex data is read from
/// addresses the shader receives in its root block, and textures are read from bindless slots.
struct GraphicsPipelineDesc {
    ShaderCode vertex; ///< Vertex entry point.
    ShaderCode pixel;  ///< Pixel entry point; may name the same `ir` as `vertex`.
    RasterDesc raster; ///< Rasterizer state baked into the pipeline.
    /// Specialization block matching the shader's declaration; may be empty.
    std::span<const std::byte> specConstants;
    std::string_view label; ///< Debug label; must be non-empty.
};

/// Creates a compute pipeline.
///
/// Fails with `ErrorCode::ShaderLoadFailed` when the intermediate code cannot be loaded or
/// compiled and with `ErrorCode::PipelineCreationFailed` when the device rejects the pipeline. The
/// error message names the label and, when the device supplies one, the compiler diagnostic.
Result<Pipeline*> createComputePipeline(Device* device, const ComputePipelineDesc& desc);

/// Creates a graphics pipeline.
///
/// @copydetails createComputePipeline
Result<Pipeline*> createGraphicsPipeline(Device* device, const GraphicsPipelineDesc& desc);

/// Destroys a pipeline created by either creation entry point.
///
/// Every submission referencing the pipeline must be retired; this is a caller contract and
/// asserts.
void destroyPipeline(Device* device, Pipeline* pipeline);

} // namespace lmx::experimental::noapi
