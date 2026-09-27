//----------------------------------------------------------------------------------------------------------------------
/// @file MenuBarFit.cpp
/// @brief Applies the menu bar's fixed readout drop order without hiding transport buttons.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/Workspace/MenuBarFit.h"

#include <algorithm>

namespace lmx::app {

//======================================================================================================================
MenuBarFit fitMenuBar(float available, const MenuBarWidths& widths) {
    const float left = widths.menus + widths.spacing;
    const bool showReadout = available >= left + widths.buttons + widths.spacing + widths.readout +
                                              widths.spacing + widths.zoom;
    const bool showZoom = available >= left + widths.buttons + widths.spacing + widths.zoom;
    const float transport = widths.buttons + (showReadout ? widths.spacing + widths.readout : 0);
    const float right = available - (showZoom ? widths.zoom + widths.spacing : 0);
    return {showReadout, showZoom, std::max(left, (left + right - transport) * 0.5f)};
}

} // namespace lmx::app
