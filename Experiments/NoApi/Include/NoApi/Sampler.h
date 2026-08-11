//----------------------------------------------------------------------------------------------------------------------
/// @file Sampler.h
/// @brief Declares sampler state creation for bindless table slots.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "NoApi/Handles.h"
#include "NoApi/Result.h"
#include "NoApi/Types.h"

#include <cstdint>
#include <string_view>

namespace lmx::noapi {

/// Selects a texture filtering rule.
enum class FilterMode : uint8_t {
    Nearest, ///< Take the closest texel.
    Linear,  ///< Interpolate between neighboring texels.
};

/// Selects how texture coordinates outside the unit range are resolved.
enum class AddressMode : uint8_t {
    Repeat,      ///< Wrap coordinates back into range.
    ClampToEdge, ///< Clamp coordinates to the edge texel.
};

/// Describes an immutable sampler state.
struct SamplerDesc {
    FilterMode minFilter = FilterMode::Linear;  ///< Filter applied when minifying.
    FilterMode magFilter = FilterMode::Linear;  ///< Filter applied when magnifying.
    FilterMode mipFilter = FilterMode::Linear;  ///< Filter applied between mip levels.
    AddressMode addressU = AddressMode::Repeat; ///< Addressing rule along u.
    AddressMode addressV = AddressMode::Repeat; ///< Addressing rule along v.
    AddressMode addressW = AddressMode::Repeat; ///< Addressing rule along w.
    uint32_t maxAnisotropy = 1;            ///< Maximum anisotropic sample count; one disables it.
    bool compare = false;                  ///< Whether the sampler performs a depth comparison.
    CompareOp compareOp = CompareOp::Less; ///< Comparison applied when `compare` is set.
    std::string_view label;                ///< Debug label; must be non-empty.
};

/// Creates an immutable sampler state.
///
/// Samplers are created once and referenced from bindless table slots. This interface has no
/// per-draw sampler binding.
///
/// Fails with `ErrorCode::ResourceCreationFailed` when the device rejects the descriptor.
Result<Sampler*> createSampler(Device* device, const SamplerDesc& desc);

/// Destroys a sampler created by `createSampler`.
///
/// Every submission referencing the sampler must be retired and every bindless slot holding it
/// must be cleared; both are caller contracts and assert.
void destroySampler(Device* device, Sampler* sampler);

} // namespace lmx::noapi
