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

/// Startup reconstruction selected by `--temporal <off|raw|taa|metalfx>` in both run modes.
enum class TemporalMode : uint8_t {
    Off,    ///< The pre-temporal frame; `render::TemporalSettings::enabled` is false.
    Raw,    ///< Temporal on, `render::ReconstructionMode::Raw`.
    Taa,    ///< Temporal on, `render::ReconstructionMode::NativeTaa`. The default.
    Vendor, ///< Temporal on, the capability-selected vendor scaler with native fallback.
};

/// Maps a startup mode to reconstruction; Off disables temporal inputs separately.
render::ReconstructionMode temporalReconstructionMode(TemporalMode mode);

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
    /// Initial reconstruction in either run mode; defaults to Native TAA.
    TemporalMode temporal = TemporalMode::Taa;
    /// Initial temporal diagnostic, if any, drawn over the display transform.
    render::TemporalDebugView temporalView = render::TemporalDebugView::Off;
    /// Initial fraction of the output extent to rasterise at, within [0.5, 1.0]. Set
    /// by `--render-scale <value>`; conflicts with `--temporal off` like a debug view, since a
    /// scale below 1 has nothing to reconstruct without the temporal path running.
    float renderScale = 1.0f;
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
