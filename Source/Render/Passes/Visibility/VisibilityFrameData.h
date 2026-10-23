//----------------------------------------------------------------------------------------------------------------------
/// @file VisibilityFrameData.h
/// @brief Declares the pure function that builds the GPU visibility parameters.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/Passes/Visibility/VisibilityTables.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace lmx::render {

/// Plain values the visibility parameters are built from.
struct VisibilityParamsInputs {
    bool dense = false;          ///< Batched submission: dense layout; otherwise sparse.
    uint64_t rowBufferBytes = 0; ///< Size of the draw row buffer, bytes; four bytes per row.
    /// Size of the draw argument buffer, bytes; twenty per argument.
    uint64_t argumentBufferBytes = 0;
    uint32_t stateCapacity = 0;  ///< State words the paced slot holds.
    uint32_t candidateCount = 0; ///< Combined scene and shadow candidates.
    /// Row, argument and state capacity ceilings a fixture imposes; each caps its capacity.
    std::optional<std::array<uint32_t, 3>> capacityOverride;
};

/// Builds the visibility parameters: layout one when dense, rows and arguments as the buffer sizes
/// divided by four and twenty bytes, then each capacity capped by its override ceiling when one is
/// set. The view count keeps its default of two and the padding stays zero.
inline VisibilityParams buildVisibilityParams(const VisibilityParamsInputs& inputs) {
    VisibilityParams params{.layout = inputs.dense ? 1u : 0u,
                            .rowCapacity = static_cast<uint32_t>(inputs.rowBufferBytes / 4),
                            .argumentCapacity =
                                static_cast<uint32_t>(inputs.argumentBufferBytes / 20),
                            .stateCapacity = inputs.stateCapacity,
                            .candidateCount = inputs.candidateCount};
    if (inputs.capacityOverride) {
        params.rowCapacity = std::min(params.rowCapacity, (*inputs.capacityOverride)[0]);
        params.argumentCapacity = std::min(params.argumentCapacity, (*inputs.capacityOverride)[1]);
        params.stateCapacity = std::min(params.stateCapacity, (*inputs.capacityOverride)[2]);
    }
    return params;
}

} // namespace lmx::render
