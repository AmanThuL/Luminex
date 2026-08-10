//----------------------------------------------------------------------------------------------------------------------
/// @file ExposureReset.h
/// @brief Declares the pure decision behind spec 9's four exposure-feedback reset triggers.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Engine/SceneLibrary.h"

#include <cstdint>
#include <optional>

namespace lmx::app {

/// Everything the exposure feedback loop's reset decision depends on. EditorShell keeps one of
/// these as "the state the last decision was made against" and builds a candidate one at each of
/// spec 9's four trigger sites (first frame, scene switch, auto-exposure enable, resize) to compare
/// against it.
///
/// `sceneId` is unset only before the first scene has loaded, which is what lets
/// shouldResetExposure tell "no scene yet" apart from "the same scene as last time" without a
/// separate first-frame flag to keep in sync with this struct.
struct ExposureResetContext {
    std::optional<engine::SceneId> sceneId; ///< The active scene, or unset before one has loaded.
    bool autoExposureEnabled = false;       ///< Whether auto-exposure is the current mode.
    uint32_t width = 0;                     ///< Scene target extent the metering last covered.
    uint32_t height = 0;                    ///< Scene target extent the metering last covered.
};

/// Whether the exposure feedback loop must restart from the manual EV rather than continue
/// metering across the transition from `previous` to `current` (spec 9's four triggers):
///
/// - **First frame**: `previous.sceneId` is unset.
/// - **Scene switch**: `current.sceneId` differs from `previous.sceneId`.
/// - **Auto-exposure enable**: `autoExposureEnabled` goes false -> true. The reverse transition
///   (disable) does not reset -- manual mode never reads the feedback buffer, so there is nothing
///   for it to restart -- and neither does holding either state steady.
/// - **Resize**: `width`/`height` differ. Callers only build a `current` with new dimensions once a
///   resize has actually taken effect, so a failed resize (dimensions unchanged) never reaches here
///   as a difference.
///
/// Pure and total: every field of `current` that does not name one of the four triggers above is
/// ignored, so passing a `current` that only updates one field (carrying the rest forward from
/// `previous`) answers exactly the question that field's call site is asking.
bool shouldResetExposure(const ExposureResetContext& previous, const ExposureResetContext& current);

} // namespace lmx::app
