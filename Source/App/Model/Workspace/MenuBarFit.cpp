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
    const bool hasActivity = widths.activity > 0;
    const float activityGap = hasActivity ? widths.spacing : 0;
    const float complete =
        left + widths.buttons + widths.spacing + widths.readout + widths.spacing + widths.zoom;
    const bool showGizmo =
        widths.gizmo > 0 &&
        available >= complete + activityGap + widths.activity + widths.spacing + widths.gizmo;
    const float gizmo = showGizmo ? widths.spacing + widths.gizmo : 0;
    const bool showActivityVerb =
        hasActivity && available >= complete + gizmo + activityGap + widths.activity;
    const float activity =
        hasActivity ? activityGap + (showActivityVerb ? widths.activity : widths.activityCompact)
                    : 0;
    const bool showReadout = available >= complete + gizmo + activity;
    const bool showZoom =
        available >= left + widths.buttons + gizmo + activity + widths.spacing + widths.zoom;
    const float transport =
        widths.buttons + gizmo + activity + (showReadout ? widths.spacing + widths.readout : 0);
    const float right = available - (showZoom ? widths.zoom + widths.spacing : 0);
    return {showReadout, showZoom, showActivityVerb, showGizmo,
            std::max(left, (left + right - transport) * 0.5f)};
}

} // namespace lmx::app
