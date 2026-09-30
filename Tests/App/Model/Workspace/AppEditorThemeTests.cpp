#include "App/Model/Workspace/EditorTheme.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
using namespace lmx::app;
//======================================================================================================================
TEST_CASE("appearance resolves and explicit choices clear session overrides", "[app][theme]") {
    REQUIRE(resolveTheme(Appearance::Auto, SystemTheme::Unknown) == ThemeKind::Dark);
    REQUIRE(resolveTheme(Appearance::Auto, SystemTheme::Light) == ThemeKind::Light);
    REQUIRE(resolveTheme(Appearance::Auto, SystemTheme::Dark) == ThemeKind::Dark);
    for (auto system : {SystemTheme::Unknown, SystemTheme::Light, SystemTheme::Dark}) {
        REQUIRE(resolveTheme(Appearance::Light, system) == ThemeKind::Light);
        REQUIRE(resolveTheme(Appearance::Dark, system) == ThemeKind::Dark);
    }
    REQUIRE_FALSE(forcedWindowAppearance(Appearance::Auto));
    REQUIRE(forcedWindowAppearance(Appearance::Light) == ThemeKind::Light);
    REQUIRE(forcedWindowAppearance(Appearance::Dark) == ThemeKind::Dark);
    AppearanceState state{Appearance::Auto, Appearance::Dark};
    REQUIRE(state.effective() == Appearance::Dark);
    state.choose(Appearance::Light);
    REQUIRE(state.persisted == Appearance::Light);
    REQUIRE_FALSE(state.override);
    REQUIRE(state.effective() == Appearance::Light);
}
//======================================================================================================================
TEST_CASE("theme names and density metrics retain exact storage values", "[app][theme]") {
    for (auto value : {Appearance::Auto, Appearance::Light, Appearance::Dark})
        REQUIRE(parseAppearance(appearanceName(value)) == value);
    for (auto value : {Density::Comfortable, Density::Compact})
        REQUIRE(parseDensity(densityName(value)) == value);
    REQUIRE(appearanceName(Appearance::Auto) == "auto");
    REQUIRE(appearanceName(Appearance::Light) == "light");
    REQUIRE(appearanceName(Appearance::Dark) == "dark");
    REQUIRE(densityName(Density::Comfortable) == "comfortable");
    REQUIRE(densityName(Density::Compact) == "compact");
    for (auto invalid : {"", "Auto", " light", "unknown"})
        REQUIRE_FALSE(parseAppearance(invalid));
    for (auto invalid : {"", "Compact", " compact", "unknown"})
        REQUIRE_FALSE(parseDensity(invalid));
    const auto comfortable = densityMetrics(Density::Comfortable);
    const auto compact = densityMetrics(Density::Compact);
    REQUIRE(comfortable.framePaddingX == 8);
    REQUIRE(comfortable.framePaddingY == 5);
    REQUIRE(comfortable.itemSpacingX == 8);
    REQUIRE(comfortable.itemSpacingY == 8);
    REQUIRE(comfortable.windowPadding == 12);
    REQUIRE(compact.framePaddingX == 6);
    REQUIRE(compact.framePaddingY == 3);
    REQUIRE(compact.itemSpacingX == 6);
    REQUIRE(compact.itemSpacingY == 4);
    REQUIRE(compact.windowPadding == 8);
    REQUIRE(&themePalette(ThemeKind::Dark) == &kDarkPalette);
    REQUIRE(&themePalette(ThemeKind::Light) == &kLightPalette);
}
//======================================================================================================================
TEST_CASE("theme fades finish at 160 ms and restart without a jump", "[app][theme]") {
    ThemeTransition fade;
    fade.start(kDarkPalette, kLightPalette, 0.0, false);
    for (std::size_t i = 0; i < kThemeRoleCount; ++i) {
        for (auto channel : {&ThemeColor::r, &ThemeColor::g, &ThemeColor::b, &ThemeColor::a}) {
            REQUIRE(fade.sample(0)[i].*channel == kDarkPalette[i].*channel);
            REQUIRE(fade.sample(0.160)[i].*channel == kLightPalette[i].*channel);
            REQUIRE(fade.sample(1)[i].*channel == kLightPalette[i].*channel);
        }
        REQUIRE(fade.sample(0.080)[i].g ==
                Catch::Approx((kDarkPalette[i].g + kLightPalette[i].g) / 2));
    }
    REQUIRE(fade.active(0.159));
    REQUIRE_FALSE(fade.active(0.160));
    const auto middle = fade.sample(0.080);
    fade.start(middle, kDarkPalette, 0.080, false);
    for (std::size_t i = 0; i < kThemeRoleCount; ++i) {
        for (auto channel : {&ThemeColor::r, &ThemeColor::g, &ThemeColor::b, &ThemeColor::a}) {
            REQUIRE(fade.sample(0.080)[i].*channel == middle[i].*channel);
            REQUIRE(fade.sample(0.240)[i].*channel == kDarkPalette[i].*channel);
        }
    }
    REQUIRE_FALSE(fade.active(0.240));
    fade.start(kDarkPalette, kLightPalette, 2.0, true);
    REQUIRE_FALSE(fade.active(2.0));
    for (std::size_t i = 0; i < kThemeRoleCount; ++i)
        for (auto channel : {&ThemeColor::r, &ThemeColor::g, &ThemeColor::b, &ThemeColor::a})
            REQUIRE(fade.sample(2.0)[i].*channel == kLightPalette[i].*channel);
}
