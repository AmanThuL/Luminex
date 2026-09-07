//----------------------------------------------------------------------------------------------------------------------
/// @file AppOptions.h
/// @brief Declares application run modes, options, and parser errors.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/SceneLibrary.h"
#include "Render/Renderer.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace lmx::app {

/// Selects the application's interactive or offscreen execution path.
enum class RunMode {
    Windowed,   ///< Interactive editor window.
    Screenshot, ///< Offscreen render written to disk.
};

/// Screenshot-mode temporal opt-in, selected by `--temporal <off|raw|taa>`. Three states rather
/// than a bool: `Off` is the pre-temporal frame, `Raw` and `Taa` both run the temporal path and
/// differ only in `render::ReconstructionMode` (spec section 2).
enum class TemporalMode : uint8_t {
    Off, ///< The pre-temporal frame; `render::TemporalSettings::enabled` is false.
    Raw, ///< Temporal on, `render::ReconstructionMode::Raw`.
    Taa, ///< Temporal on, `render::ReconstructionMode::NativeTaa`. The default.
};

/// Fully parsed, owned application startup configuration.
struct AppOptions {
    RunMode mode = RunMode::Windowed; ///< Execution path selected by command-line options.
    engine::SceneId initialScene = engine::defaultSceneId(); ///< Scene selected at startup.
    std::filesystem::path screenshotPath; ///< Destination used in screenshot mode.
    /// The interactive window opens maximized to the display's usable bounds; `--windowed` keeps
    /// the fixed default size instead. Irrelevant, but accepted, in screenshot mode.
    bool maximized = true;
    /// Screenshot-mode-only: how many frames to render before writing the last one, advancing the
    /// animation by 1/60 s and following the camera track (if any) between them -- the roadmap's
    /// declared warmup and intermediate temporal captures. At least 1; irrelevant, but accepted,
    /// in windowed mode.
    uint32_t frames = 1;
    /// Screenshot-mode-only: which temporal reconstruction the rendered frame(s) run with. Set by
    /// `--temporal <off|raw|taa>`; defaults to `Taa`, matching the editor's default (spec section
    /// 10). Irrelevant, but accepted, in windowed mode.
    TemporalMode temporal = TemporalMode::Taa;
    /// Screenshot-mode-only: which temporal diagnostic, if any, to draw over the display transform.
    render::TemporalDebugView temporalView = render::TemporalDebugView::Off;
};

/// Reports an invalid command-line option with user-facing context.
struct AppOptionsError {
    std::string message; ///< Description suitable for command-line diagnostics.
};

/// Result returned after parsing an application configuration.
using AppOptionsResult = std::expected<AppOptions, AppOptionsError>;

/// Parses arguments after the executable name. Repeated options use the final value; a bare `--`
/// is ignored. An empty screenshot path preserves windowed mode. `--windowed` clears `maximized`.
/// Returned options own argv data.
AppOptionsResult parseAppOptions(std::span<const std::string_view> arguments);

} // namespace lmx::app
