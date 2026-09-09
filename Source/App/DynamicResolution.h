//----------------------------------------------------------------------------------------------------------------------
/// @file DynamicResolution.h
/// @brief Declares the pure per-frame dynamic-resolution controller-driving policy.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/EditorRenderSettings.h"
#include "App/FrameRecordRing.h"
#include "RHI/RHI.h"
#include "Render/ResolutionController.h"

#include <cstdint>
#include <span>

namespace lmx::app {

/// What the dynamic-resolution policy needs to remember across frames, kept by EditorShell
/// alongside its `render::ResolutionController`. No field is meaningful on its own: the first two
/// exist only so `applyDynamicResolution` can tell "just turned on" and "already observed this
/// frame" apart from one call to the next, and the third carries the judged GPU time forward for
/// the Inspector's status row.
struct DynamicResolutionState {
    /// Whether the controller was active (`dynamicResolutionActive`) as of the last call, so the
    /// off->on edge of that state can be told apart from "still active" and the controller is
    /// seeded exactly once per enable.
    bool wasEnabled = false;
    /// The frame number `observe()` was last called with, so a newest-timed frame that has not
    /// advanced since the last call is not judged twice.
    uint64_t lastObservedFrame = 0;
    /// The GPU time the controller last judged, in milliseconds, for the Inspector's status row.
    /// Left unchanged when no new frame is observed.
    double lastObservedMilliseconds = 0.0;
};

/// Whether the dynamic-resolution controller is driving this frame: dynamic resolution is on and
/// the temporal path is on with it. The renderer rasterises at scale 1 whenever temporal is off,
/// so a frame declared or timed in that period belongs to no scale the controller chose; the
/// policy and the shell both gate on this, not on `dynamicResolutionEnabled` alone.
bool dynamicResolutionActive(const EditorRenderSettings& settings);

/// Sums a retired frame's per-pass GPU times -- the controller's cost model, spec section 8.
double frameGpuMilliseconds(std::span<const rhi::PassTiming> timings);

/// Runs one frame of the dynamic-resolution policy (spec section 9):
///
/// The controller runs only while it is active -- `dynamicResolutionActive(settings)`, which is
/// dynamic resolution and the temporal path both on. With temporal off the renderer rasterises at
/// scale 1, so there is no scale of the controller's for a frame to be judged against.
///
/// - Off->on edge of the active state (`state.wasEnabled` false): seeds the controller from the
///   manual `settings.renderScale`, so turning it on does not jump the frame.
/// - While active: copies `settings.gpuBudgetMilliseconds` into the controller's settings (only
///   when it actually changed, so an unedited budget does not re-clamp `scale()` every frame),
///   observes
///   `newestTimed` once per frame it advances to, then writes `settings.renderScale` from the
///   controller's `scale()`.
/// - While idle: declares and observes nothing and leaves `settings.renderScale` alone, at
///   whatever the controller last computed, so the picture does not jump when dynamic resolution
///   or the temporal path is switched off. The newest timed frame's number is still recorded as
///   `state.lastObservedFrame`, so becoming active again cannot consume an idle period's timing.
///
/// `state.wasEnabled` is updated last, after the edge it names has been acted on.
void applyDynamicResolution(DynamicResolutionState& state, render::ResolutionController& controller,
                            EditorRenderSettings& settings, const RetainedFrame* newestTimed);

} // namespace lmx::app
