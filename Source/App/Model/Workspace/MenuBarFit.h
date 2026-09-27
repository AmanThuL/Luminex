//----------------------------------------------------------------------------------------------------------------------
/// @file MenuBarFit.h
/// @brief Fits the editor transport between menus and the optional zoom readout.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

namespace lmx::app {

/// Measured widths in one common coordinate system, including internal button spacing.
struct MenuBarWidths {
    /// Occupied left edge through the last menu, excluding any trailing item spacing the layout
    /// already advanced past it; fitMenuBar adds `spacing` itself.
    float menus;
    float buttons; ///< Transport buttons, including spacing between buttons.
    float readout; ///< Time or measurement progress text.
    float zoom;    ///< Clickable zoom percentage.
    float spacing; ///< Minimum gap between occupied groups.
};

/// A single-row placement; transport buttons always remain present.
struct MenuBarFit {
    bool showReadout; ///< Whether the transport has room for time or progress text.
    bool showZoom;    ///< Whether the right edge has room for the zoom percentage.
    float transportX; ///< Left edge of the transport, after the menus and minimum gap.
};

/// Centres the transport in the remaining interval, dropping readout before zoom when tight.
MenuBarFit fitMenuBar(float available, const MenuBarWidths& widths);

} // namespace lmx::app
