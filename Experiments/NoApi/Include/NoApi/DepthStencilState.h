//----------------------------------------------------------------------------------------------------------------------
/// @file DepthStencilState.h
/// @brief Declares depth and stencil state as an object separate from the pipeline.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "NoApi/Handles.h"
#include "NoApi/Result.h"
#include "NoApi/Types.h"

#include <cstdint>
#include <string_view>

namespace lmx::experimental::noapi {

/// Names an action applied to the stencil buffer after a test.
enum class StencilOp : uint8_t {
    Keep,      ///< Leave the stored value unchanged.
    Zero,      ///< Store zero.
    Replace,   ///< Store the reference value.
    Increment, ///< Increment the stored value, clamping at the maximum.
    Decrement, ///< Decrement the stored value, clamping at zero.
    Invert,    ///< Bitwise-invert the stored value.
};

/// Describes the stencil test and its three outcome actions for one facing.
struct StencilDesc {
    CompareOp test = CompareOp::Always;      ///< Comparison against the reference value.
    StencilOp failOp = StencilOp::Keep;      ///< Action when the stencil test fails.
    StencilOp passOp = StencilOp::Keep;      ///< Action when both stencil and depth tests pass.
    StencilOp depthFailOp = StencilOp::Keep; ///< Action when stencil passes and depth fails.
};

/// Describes the depth and stencil configuration of a pass.
///
/// This state is separate from the pipeline because it configures fixed-function units through
/// their own command packets and does not change shader microcode. Keeping it out of the pipeline
/// removes one axis of pipeline permutation.
struct DepthStencilDesc {
    bool depthTestEnabled = false;           ///< Whether the depth test runs at all.
    bool depthWriteEnabled = false;          ///< Whether passing fragments write depth.
    CompareOp depthTest = CompareOp::Always; ///< Comparison applied to depth.
    bool stencilEnabled = false;             ///< Whether the stencil test runs at all.
    uint8_t stencilReadMask = 0xFF;          ///< Mask applied to values read for the stencil test.
    uint8_t stencilWriteMask = 0xFF;         ///< Mask applied to values written by stencil actions.
    StencilDesc front{};                     ///< Stencil configuration for front-facing primitives.
    StencilDesc back{};                      ///< Stencil configuration for back-facing primitives.
    std::string_view label;                  ///< Debug label; must be non-empty.
};

/// Creates an immutable depth-stencil state object.
///
/// Fails with `ErrorCode::ResourceCreationFailed` when the device rejects the descriptor.
Result<DepthStencilState*> createDepthStencilState(Device* device, const DepthStencilDesc& desc);

/// Destroys a depth-stencil state created by `createDepthStencilState`.
///
/// Every submission referencing the state must be retired; this is a caller contract and asserts.
void destroyDepthStencilState(Device* device, DepthStencilState* state);

} // namespace lmx::experimental::noapi
