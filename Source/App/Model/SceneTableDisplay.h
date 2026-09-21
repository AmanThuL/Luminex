//----------------------------------------------------------------------------------------------------------------------
/// @file SceneTableDisplay.h
/// @brief Declares read-only scene table diagnostic formatting.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Engine/Scene/SceneTableStats.h"
#include <array>
#include <string>
#include <string_view>

namespace lmx::app {
/// One Inspector field with an owned formatted value.
struct SceneTableField {
    std::string_view label; ///< Stable human-readable field name.
    std::string value;      ///< Complete value for this snapshot.
};
/// Formats a coherent table snapshot without querying GPU state.
std::array<SceneTableField, 9> sceneTableFields(const engine::SceneTableStats& stats);
} // namespace lmx::app
