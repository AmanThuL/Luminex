//----------------------------------------------------------------------------------------------------------------------
/// @file RunHooks.h
/// @brief Declares the frame-numbered automation hooks of a bounded windowed run.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace lmx::app {

/// Logical window size in points, the unit SDL_SetWindowSize takes.
struct WindowSize {
    int width = 0;  ///< Points across.
    int height = 0; ///< Points down.

    bool operator==(const WindowSize&) const = default;
};

/// The size the editor window is created at; `--windowed` keeps it, maximized starts from it.
inline constexpr WindowSize kWindowedSize{1280, 720};
/// The size LMX_RESIZE_WINDOW_AT_FRAME shrinks the main window to.
inline constexpr WindowSize kResizeHookSmallSize{800, 600};
/// Frames the shrunk window is held before the hook grows it back to kWindowedSize.
inline constexpr uint64_t kResizeHookHoldFrames = 100;

/// Parses a frame-number environment value: a frame index, or zero to disable the hook. Returns
/// nothing for text that is not a whole decimal number.
std::optional<uint64_t> parseFrameHook(std::string_view text);

/// Returns the size the resize hook sets at `frame` (1-based): kResizeHookSmallSize at
/// `resizeAtFrame`, then kWindowedSize kResizeHookHoldFrames later, so one run drives the swapchain
/// through a shrink and a grow. Nothing at any other frame, and nothing when `resizeAtFrame` is 0.
std::optional<WindowSize> resizeHookSize(uint64_t resizeAtFrame, uint64_t frame);

} // namespace lmx::app
