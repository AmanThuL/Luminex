//----------------------------------------------------------------------------------------------------------------------
/// @file OcclusionValidation.h
/// @brief Declares an offscreen active-extent step for unscored occlusion recovery evidence.
//----------------------------------------------------------------------------------------------------------------------
#pragma once
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace lmx::app {
/// One diagnostic change in active extent, without resizing output or the HZB allocation.
struct OcclusionScaleStep {
    uint32_t frame = 0; ///< Zero-based deterministic sequence frame at which the scale changes.
    float scale = 1.0f; ///< Render scale from this frame onward.
};
/// Reads the paired LMX_OCCLUSION_SCALE_CHANGE_FRAME and _VALUE variables, refusing malformed
/// or scored/temporal-off use; absence of both leaves the normal run unchanged.
std::expected<std::optional<OcclusionScaleStep>, std::string>
readOcclusionScaleStep(bool unscored, bool temporalEnabled);
} // namespace lmx::app
