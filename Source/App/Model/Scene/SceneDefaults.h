//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDefaults.h
/// @brief Declares scene display defaults shared by interactive and headless rendering.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

namespace lmx::app {

/// Display-space neutral clear value; Renderer decodes it when declaring the scene pass.
inline constexpr float kSceneClearGray = 0.7f;

} // namespace lmx::app
