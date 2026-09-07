//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalHistory.h
/// @brief Declares the frame signature and the history reset derivation.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/Temporal.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace lmx::render {

/// Why a frame cannot reproject the previous frame's history. Ordered by precedence: the
/// derivation reports the first reason that applies.
enum class HistoryResetReason : uint8_t {
    None,              ///< History is valid and may be reprojected.
    FirstFrame,        ///< No previous frame was recorded.
    TemporalEnabled,   ///< The previous frame ran with temporal off and this one has it on.
    SceneChanged,      ///< The scene generation differs, so history describes other geometry.
    ExtentChanged,     ///< A render or output extent differs, so history has the wrong footprint.
    ProjectionChanged, ///< fovY or nearZ differs, so history reprojects to the wrong pixels.
    CameraCut          ///< The caller raised an explicit cut for this frame.
};

/// What a declared frame records so the next one can decide whether history survives. The
/// Renderer records this every frame, temporal on or off, so re-enabling stays distinguishable
/// from the first frame.
struct FrameSignature {
    uint64_t sceneGeneration = 0; ///< Monotonic counter the scene bumps on any content change.
    FrameExtents extents;         ///< Render and output extents the frame was declared at.
    float fovY = 0.0f;            ///< Vertical field of view in radians.
    float nearZ = 0.0f;           ///< Positive near-plane distance in world units.
    bool temporalEnabled = false; ///< Whether the frame declared the temporal path.
};

/// Derives whether `current` may reuse the history left by `previous`. Pure and ordered: absent
/// previous, then enabling, scene generation, extents, projection, then the explicit `cameraCut`.
/// fovY and nearZ are compared exactly -- any authored change is a change.
HistoryResetReason deriveHistoryReset(const std::optional<FrameSignature>& previous,
                                      const FrameSignature& current, bool cameraCut);

/// Returns the enumerator's stable spelling, for status display and frame dumps.
std::string_view historyResetReasonName(HistoryResetReason reason);

} // namespace lmx::render
