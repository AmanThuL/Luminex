//----------------------------------------------------------------------------------------------------------------------
/// @file Screenshot.h
/// @brief Declares the offscreen screenshot entry point.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/AppOptions.h"
#include "Render/SceneView.h"
#include "Scene/SceneLibrary.h"

#include <cstdint>
#include <filesystem>

namespace lmx::app {

/// Renders `sceneId` offscreen and writes a PNG or BMP image, selected by extension, to `outPath`.
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
int runScreenshot(const std::filesystem::path& outPath, scene::SceneId sceneId, uint32_t frames,
                  TemporalMode temporal, render::TemporalDebugView temporalView, float renderScale,
                  bool visibilityEnabled = true,
                  render::SubmissionMode submission = render::SubmissionMode::Indirect,
                  uint32_t labInstances = 4096,
                  render::ClassifyMode classifyMode = render::ClassifyMode::Cpu,
                  bool classifyCheck = false, bool occlusionEnabled = false,
                  bool occlusionCheck = false, int32_t hzbDebugLevel = -1,
                  uint32_t labOccluders = 0);

/// Captures options.frames post-warmup frames at exactly 60 Hz into a new or empty directory.
/// Writes numbered PNGs or BMPs and a settings/camera/status manifest. A vendor fallback, flat
/// image or write failure returns nonzero and leaves an incomplete manifest; existing output is
/// never replaced.
int runCaptureSequence(const AppOptions& options);

} // namespace lmx::app
