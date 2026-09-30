#include "App/Model/Workspace/EditorTheme.h"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
using namespace lmx::app;
//======================================================================================================================
static double independentLuminance(ThemeColor c) {
    const auto decode = [](double v) {
        return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * decode(c.r) + 0.7152 * decode(c.g) + 0.0722 * decode(c.b);
}
//======================================================================================================================
TEST_CASE("both theme palettes meet all independent WCAG contrast contracts", "[app][theme]") {
    for (auto kind : {ThemeKind::Dark, ThemeKind::Light}) {
        const auto& palette = themePalette(kind);
        const auto& pairs = kind == ThemeKind::Dark ? kDarkContrastPairs : kLightContrastPairs;
        REQUIRE(pairs.size() == 24);
        for (const auto& pair : pairs) {
            INFO(pair.label);
            auto bg = palette[static_cast<std::size_t>(pair.bg)];
            if (pair.underlay) {
                const auto base = palette[static_cast<std::size_t>(*pair.underlay)];
                bg = {bg.r * bg.a + base.r * (1 - bg.a), bg.g * bg.a + base.g * (1 - bg.a),
                      bg.b * bg.a + base.b * (1 - bg.a), 1};
            }
            const auto fg = palette[static_cast<std::size_t>(pair.fg)];
            const double a = independentLuminance(fg), b = independentLuminance(bg);
            const double ratio = (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
            REQUIRE(ratio >= pair.minimum);
            REQUIRE(contrastRatio(fg, bg) == Catch::Approx(ratio).epsilon(1e-12));
            if (pair.underlay) {
                const auto actual = composite(palette[static_cast<std::size_t>(pair.bg)],
                                              palette[static_cast<std::size_t>(*pair.underlay)]);
                REQUIRE(actual.r == bg.r);
                REQUIRE(actual.g == bg.g);
                REQUIRE(actual.b == bg.b);
                REQUIRE(actual.a == 1);
            }
        }
    }
}
//======================================================================================================================
TEST_CASE("straight alpha composites encoded RGB and contrast uses WCAG endpoints",
          "[app][theme]") {
    auto c = composite({1, 0, 0, 0.5f}, {0, 0, 1, 0.5f});
    REQUIRE(c.r == Catch::Approx(2.0 / 3));
    REQUIRE(c.b == Catch::Approx(1.0 / 3));
    REQUIRE(c.a == 0.75f);
    c = composite({1, 1, 1, 0}, {0, 0, 0, 0});
    REQUIRE(c.a == 0);
    REQUIRE(c.r == 0);
    REQUIRE(contrastRatio({0, 0, 0, 1}, {1, 1, 1, 1}) == Catch::Approx(21));
    REQUIRE(contrastRatio({0.04045f, 0.04045f, 0.04045f, 1}, {0, 0, 0, 1}) ==
            Catch::Approx((independentLuminance({0.04045f, 0.04045f, 0.04045f, 1}) + 0.05) / 0.05));
}
