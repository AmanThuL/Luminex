//----------------------------------------------------------------------------------------------------------------------
/// @file Screenshot.h
/// @brief Declares the offscreen screenshot entry point.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Engine/SceneLibrary.h"
#include "Render/Renderer.h"

#include <cstdint>
#include <filesystem>

namespace lmx::app {

/// Renders `sceneId` offscreen and writes a BMP image to `outPath`.
///
/// Renders `frames` frames in sequence (at least 1), advancing the scene's animation clock by
/// 1/60 s and following its camera track (if any) between them, committing each after it renders,
/// and writing only the last -- the roadmap's declared warmup and intermediate temporal captures.
/// `frames == 1` behaves exactly as before: no clock advance, no commit beyond the one frame. The
/// scene generation this run declares is 0 and its camera cut is always false -- a one-shot process
/// has no prior generation to differ from and never teleports its own camera.
int runScreenshot(const std::filesystem::path& outPath, engine::SceneId sceneId, uint32_t frames,
                  bool temporal, render::TemporalDebugView temporalView);

} // namespace lmx::app
