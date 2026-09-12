//----------------------------------------------------------------------------------------------------------------------
/// @file EdrProbe.h
/// @brief Declares display-only experimental headroom observation.
//----------------------------------------------------------------------------------------------------------------------
#pragma once
struct SDL_Window;
namespace lmx::experimental::edr {
/// Display headroom observations in multiples of SDR white.
struct Headroom {
    bool enabled = false;    ///< SDL HDR state.
    float white = 1;         ///< SDL SDR white value.
    float current = 1;       ///< SDL current headroom.
    float nativeCurrent = 1; ///< NSScreen current headroom.
    float potential = 1;     ///< NSScreen potential headroom.
    float reference = 1;     ///< NSScreen reference headroom.
};
/// Tags an SDL Metal layer for extended-linear sRGB presentation; returns acceptance.
bool configure(void* layer, bool extended);
/// Reads SDL and NSScreen values for this window on the main thread.
Headroom observe(SDL_Window* window);
} // namespace lmx::experimental::edr
