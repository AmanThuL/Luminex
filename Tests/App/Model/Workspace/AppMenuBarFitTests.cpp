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

//======================================================================================================================
TEST_CASE("menu bar drops the activity verb before readout and zoom", "[app][menu-fit]") {
    MenuBarWidths widths{200, 100, 80, 40, 10};
    widths.activity = 120;
    widths.activityCompact = 30;
    const auto complete = fitMenuBar(580, widths);
    REQUIRE(complete.showActivityVerb);
    REQUIRE(complete.showReadout);
    REQUIRE(complete.showZoom);
    const auto compact = fitMenuBar(579, widths);
    REQUIRE_FALSE(compact.showActivityVerb);
    REQUIRE(compact.showReadout);
    REQUIRE(compact.showZoom);
    REQUIRE(fitMenuBar(490, widths).showReadout);
    const auto noReadout = fitMenuBar(489, widths);
    REQUIRE_FALSE(noReadout.showActivityVerb);
    REQUIRE_FALSE(noReadout.showReadout);
    REQUIRE(noReadout.showZoom);
    REQUIRE(fitMenuBar(400, widths).showZoom);
    const auto mandatory = fitMenuBar(399, widths);
    REQUIRE_FALSE(mandatory.showZoom);
    REQUIRE(mandatory.transportX >= 210);
    REQUIRE(fitMenuBar(0, widths).transportX == 210);
}

//======================================================================================================================
TEST_CASE("menu activity width contracts preserve nondefault and absent groups",
          "[app][menu-fit]") {
    MenuBarWidths widths{17, 23, 31, 11, 3};
    widths.activity = 41;
    widths.activityCompact = 7;
    REQUIRE(fitMenuBar(135, widths).showActivityVerb);
    REQUIRE_FALSE(fitMenuBar(134, widths).showActivityVerb);
    REQUIRE(fitMenuBar(101, widths).showReadout);
    REQUIRE_FALSE(fitMenuBar(100, widths).showReadout);
    REQUIRE(fitMenuBar(67, widths).showZoom);
    REQUIRE_FALSE(fitMenuBar(66, widths).showZoom);
    widths.activity = 0;
    widths.activityCompact = 0;
    REQUIRE_FALSE(fitMenuBar(700, widths).showActivityVerb);
    REQUIRE(fitMenuBar(91, widths).showReadout);
    REQUIRE_FALSE(fitMenuBar(90, widths).showReadout);
    widths.readout = 0;
    widths.zoom = 0;
    widths.spacing = 0;
    widths.activity = 9;
    widths.activityCompact = 5;
    REQUIRE(fitMenuBar(49, widths).showActivityVerb);
    REQUIRE_FALSE(fitMenuBar(48, widths).showActivityVerb);
    REQUIRE(fitMenuBar(0, widths).transportX == 17);
}

//======================================================================================================================
TEST_CASE("menu bar hides gizmo tools before activity verb readout and zoom",
          "[app][menu-fit][gizmo-tools]") {
    MenuBarWidths widths{200, 100, 80, 40, 10};
    widths.activity = 120;
    widths.activityCompact = 30;
    widths.gizmo = 80;
    const auto all = fitMenuBar(670, widths);
    CHECK(all.showGizmo);
    CHECK(all.showActivityVerb);
    CHECK(all.showReadout);
    CHECK(all.showZoom);
    CHECK(all.transportX + widths.buttons + widths.spacing + widths.gizmo + widths.spacing +
              widths.readout + widths.spacing + widths.activity <=
          620);
    const auto noTools = fitMenuBar(669, widths);
    CHECK_FALSE(noTools.showGizmo);
    CHECK(noTools.showActivityVerb);
    CHECK(noTools.showReadout);
    CHECK(noTools.showZoom);
    CHECK(fitMenuBar(580, widths).showActivityVerb);
    CHECK_FALSE(fitMenuBar(579, widths).showActivityVerb);
    CHECK(fitMenuBar(490, widths).showReadout);
    CHECK_FALSE(fitMenuBar(489, widths).showReadout);
    CHECK(fitMenuBar(400, widths).showZoom);
    CHECK_FALSE(fitMenuBar(399, widths).showZoom);
    widths.activity = widths.activityCompact = 0;
    CHECK(fitMenuBar(540, widths).showGizmo);
    CHECK_FALSE(fitMenuBar(539, widths).showGizmo);
    widths.gizmo = 0;
    CHECK_FALSE(fitMenuBar(700, widths).showGizmo);
    CHECK(fitMenuBar(700, widths).transportX == 335);
}
