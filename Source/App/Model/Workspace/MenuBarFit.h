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
    float buttons;      ///< Transport buttons, including spacing between buttons.
    float readout;      ///< Time or measurement progress text.
    float zoom;         ///< Clickable zoom percentage.
    float spacing;      ///< Minimum gap between occupied groups.
    float activity = 0; ///< Full strip width including mark, verb, progress and optional Stop.
    /// Contracted strip width: mark, progress and optional Stop; zero only when activity is absent.
    float activityCompact = 0;
};

/// A single-row placement; transport buttons always remain present.
struct MenuBarFit {
    bool showReadout;      ///< Whether the transport has room for time or progress text.
    bool showZoom;         ///< Whether the right edge has room for the zoom percentage.
    bool showActivityVerb; ///< False when absent or contracted; an active mark always remains.
    float transportX;      ///< Left edge of the transport, after the menus and minimum gap.
};

/// Centers transport and activity together; drops activity verb, readout, then zoom when tight.
/// activity >= activityCompact >= 0; both are zero when absent. Buttons and the contracted strip
/// always remain, even if their mandatory minimum exceeds available. Widths use common units.
MenuBarFit fitMenuBar(float available, const MenuBarWidths& widths);

} // namespace lmx::app
