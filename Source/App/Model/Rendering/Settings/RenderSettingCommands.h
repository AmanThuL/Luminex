//----------------------------------------------------------------------------------------------------------------------
/// @file RenderSettingCommands.h
/// @brief Declares shared rendering-setting edits and their CLI names.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Rendering/Settings/EditorRenderSettings.h"

#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace lmx::app {

/// Rendering settings exposed to the panel and session command bridge.
enum class RenderSettingKey {
    Temporal,       ///< Temporal reconstruction request.
    RenderScale,    ///< Manual render extent fraction.
    Visibility,     ///< Camera-frustum culling.
    Classify,       ///< Visibility classifier.
    ClassifyCheck,  ///< CPU visibility oracle.
    Occlusion,      ///< Previous-frame HZB culling.
    OcclusionCheck, ///< Independent occlusion oracle.
    Submission,     ///< Draw submission mode.
    LocalLights,    ///< Local-light evaluation mode.
    LightCheck      ///< CPU light-list oracle.
};

/// Returns the corresponding CLI flag name without leading dashes.
std::string_view renderSettingName(RenderSettingKey key);

/// Parses a CLI flag name without leading dashes; unknown names return no key.
std::optional<RenderSettingKey> parseRenderSettingName(std::string_view name);

/// Applies a CLI-spelled value with the editor's dependent-setting cascades. Unavailable or invalid
/// requests return a panel-facing reason and leave every setting unchanged.
std::expected<void, std::string> applyRenderSetting(EditorRenderSettings& settings,
                                                    RenderSettingKey key, std::string_view value);

/// Returns the CLI-spelled value currently requested by the editor. Temporal "off" retains its
/// reconstruction and scale fields for a later re-enable.
std::string renderSettingValue(const EditorRenderSettings& settings, RenderSettingKey key);

/// Formats a panel slider scale without losing float precision before it reaches the command layer.
/// The caller supplies a finite scale within the slider's [0.5, 1.0] range.
std::string renderScaleCommandValue(float scale);

} // namespace lmx::app
