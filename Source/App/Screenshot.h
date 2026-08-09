#pragma once
#include "Engine/SceneLibrary.h"

#include <filesystem>

namespace lmx::app {

int runScreenshot(const std::filesystem::path& outPath, engine::SceneId sceneId);

} // namespace lmx::app
