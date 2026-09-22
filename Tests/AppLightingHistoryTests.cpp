#include "App/Model/LightingHistory.h"
#include "App/Model/Rendering/Settings/EditorRenderDefaults.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

//======================================================================================================================
TEST_CASE("zero-live lighting mode changes preserve usable temporal history",
          "[app][lighting-history]") {
    using namespace lmx;
    constexpr std::array modes{engine::LocalLightMode::Off, engine::LocalLightMode::Direct,
                               engine::LocalLightMode::Clustered};
    for (const auto before : modes) {
        for (const auto after : modes) {
            CAPTURE(before, after);
            CHECK_FALSE(app::lightingChangeNeedsHistoryReset(before, after, 0, false));
            CHECK(app::lightingChangeNeedsHistoryReset(before, after, 16, false) ==
                  (before != after));
            CHECK(app::lightingChangeNeedsHistoryReset(before, after, 0, true));
            CHECK(app::lightingChangeNeedsHistoryReset(before, after, 16, true));
        }
    }
}

//======================================================================================================================
TEST_CASE("lighting reset and implicit clustered selection use effective content",
          "[app][lighting-history]") {
    using namespace lmx;
    app::EditorRenderSettings settings;
    settings.localLightMode = engine::LocalLightMode::Off;
    const auto previous = settings.localLightMode;
    app::resetRenderingGroup(settings, app::EditorRenderGroup::Lighting);
    CHECK_FALSE(app::lightingChangeNeedsHistoryReset(previous, settings.localLightMode, 0, false));
    CHECK(app::lightingChangeNeedsHistoryReset(previous, settings.localLightMode, 16, false));
    CHECK_FALSE(app::lightingChangeNeedsHistoryReset(engine::LocalLightMode::Direct,
                                                     engine::LocalLightMode::Clustered, 0, false));
    CHECK(app::lightingChangeNeedsHistoryReset(engine::LocalLightMode::Clustered,
                                               engine::LocalLightMode::Clustered, 0, true));
}
