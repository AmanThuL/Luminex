//----------------------------------------------------------------------------------------------------------------------
/// @file Screenshot.h
/// @brief Declares the offscreen screenshot entry point.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Engine/SceneLibrary.h"

#include <filesystem>

namespace lmx::app {

/// Renders `sceneId` offscreen and writes a BMP image to `outPath`.
int runScreenshot(const std::filesystem::path& outPath, engine::SceneId sceneId);

} // namespace lmx::app
