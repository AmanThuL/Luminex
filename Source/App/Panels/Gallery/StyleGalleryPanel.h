//----------------------------------------------------------------------------------------------------------------------
/// @file StyleGalleryPanel.h
/// @brief Declares the detached, session-only Style Gallery.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

namespace lmx::app {

inline constexpr const char* kStyleGalleryWindowName = "Style Gallery";

struct StyleGalleryPanelState {
    bool open = false;
    int palette = 0;
};

void drawStyleGalleryPanel(StyleGalleryPanelState& state);

} // namespace lmx::app
