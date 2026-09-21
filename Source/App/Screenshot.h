//----------------------------------------------------------------------------------------------------------------------
/// @file Screenshot.h
/// @brief Declares the offscreen screenshot entry point.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/AppOptions.h"
#include "Render/SceneView.h"
#include "Scenes/SceneLibrary.h"

#include <cstdint>
#include <filesystem>

namespace lmx::app {

/// Renders the requested scene offscreen and writes options.screenshotPath as PNG or BMP.
///
/// Renders `frames` frames in sequence (at least 1), advancing the scene's animation clock by
/// 1/60 s and following its camera track (if any) between them, committing each after it renders,
/// and writing only the last -- the roadmap's declared warmup and intermediate temporal captures.
/// `frames == 1` behaves exactly as before: no clock advance, no commit beyond the one frame. The
/// scene generation this run declares is 0 and its camera cut is always false -- a one-shot process
/// has no prior generation to differ from and never teleports its own camera.
///
/// `temporal` maps `Off` to `render::TemporalSettings::enabled = false`, and `Raw`/`Taa` to
/// enabled with the corresponding `render::ReconstructionMode` and jitter on. `renderScale` is
/// copied verbatim onto `render::SceneView::temporal.renderScale`.
int runScreenshot(const AppOptions& options);

/// Captures options.frames post-warmup frames at exactly 60 Hz into a new or empty directory.
/// Writes numbered PNGs or BMPs and a settings/camera/status manifest. A vendor fallback, flat
/// final image or write failure returns nonzero and leaves an incomplete manifest; existing output
/// is never replaced.
int runCaptureSequence(const AppOptions& options);

} // namespace lmx::app
