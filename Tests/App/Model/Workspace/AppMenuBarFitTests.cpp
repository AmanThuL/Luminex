#include "App/Model/Workspace/MenuBarFit.h"

#include <catch2/catch_test_macros.hpp>

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("menu bar centres the complete transport between menus and zoom", "[app][menu-fit]") {
    const MenuBarWidths widths{200, 100, 80, 40, 10};
    const auto fit = fitMenuBar(700, widths);
    REQUIRE(fit.showReadout);
    REQUIRE(fit.showZoom);
    REQUIRE(fit.transportX == 335);
    REQUIRE(fit.transportX - 210 == 650 - (fit.transportX + 190));
}

//======================================================================================================================
TEST_CASE("menu bar drops readout before zoom and never drops buttons", "[app][menu-fit]") {
    const MenuBarWidths widths{200, 100, 80, 40, 10};
    const auto complete = fitMenuBar(450, widths);
    REQUIRE(complete.showReadout);
    REQUIRE(complete.showZoom);
    const auto noReadout = fitMenuBar(449, widths);
    REQUIRE_FALSE(noReadout.showReadout);
    REQUIRE(noReadout.showZoom);
    const auto zoomFits = fitMenuBar(360, widths);
    REQUIRE_FALSE(zoomFits.showReadout);
    REQUIRE(zoomFits.showZoom);
    const auto buttonsOnly = fitMenuBar(359, widths);
    REQUIRE_FALSE(buttonsOnly.showReadout);
    REQUIRE_FALSE(buttonsOnly.showZoom);
    for (float available : {700.0f, 450.0f, 449.0f, 360.0f, 359.0f, 310.0f, 100.0f, 0.0f}) {
        REQUIRE(fitMenuBar(available, widths).transportX >= widths.menus + widths.spacing);
    }
    REQUIRE(fitMenuBar(100, widths).transportX == 210);
}

//======================================================================================================================
TEST_CASE("1280 point menu bar retains all controls at 150 percent", "[app][menu-fit]") {
    const MenuBarWidths widths{435, 150, 180, 70, 12};
    const auto fit = fitMenuBar(1280, widths);
    REQUIRE(fit.showReadout);
    REQUIRE(fit.showZoom);
    REQUIRE(fit.transportX >= widths.menus + widths.spacing);
    REQUIRE(fit.transportX + widths.buttons + widths.spacing + widths.readout <=
            1280 - widths.zoom - widths.spacing);
}
