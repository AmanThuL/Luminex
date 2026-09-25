#include "App/Model/Options/AppOptions.h"
#include "App/Model/Rendering/Settings/DebugView.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

using namespace lmx;
using namespace lmx::app;

namespace {
struct ViewCase {
    DebugView view;
    std::string_view flag;
    std::string_view value;
};
constexpr std::array kViews{
    ViewCase{{DebugViewTopic::Temporal, 1}, "--temporal-view", "motion"},
    ViewCase{{DebugViewTopic::Temporal, 2}, "--temporal-view", "reprojection"},
    ViewCase{{DebugViewTopic::Temporal, 3}, "--temporal-view", "reprojected"},
    ViewCase{{DebugViewTopic::Temporal, 4}, "--temporal-view", "rejection"},
    ViewCase{{DebugViewTopic::Temporal, 5}, "--temporal-view", "weight"},
    ViewCase{{DebugViewTopic::Temporal, 6}, "--temporal-view", "age"},
    ViewCase{{DebugViewTopic::Lighting, 1}, "--light-view", "count"},
    ViewCase{{DebugViewTopic::Lighting, 2}, "--light-view", "overflow"},
    ViewCase{{DebugViewTopic::Lighting, 3}, "--light-view", "missed"},
    ViewCase{{DebugViewTopic::Occlusion, 0}, "--hzb-level", "0"},
    ViewCase{{DebugViewTopic::Occlusion, 1}, "--hzb-level", "1"},
    ViewCase{{DebugViewTopic::Occlusion, 2}, "--hzb-level", "2"},
};

//======================================================================================================================
void requireFinal(const EditorRenderSettings& settings) {
    REQUIRE(settings.temporalDebugView == render::TemporalDebugView::Off);
    REQUIRE(settings.lightDebugView == engine::LightDebugView::Off);
    REQUIRE(settings.hzbDebugLevel == -1);
    REQUIRE_FALSE(activeDebugView(settings));
}
} // namespace

//======================================================================================================================
TEST_CASE("Debug View availability matches the complete CLI conflict matrix", "[app][debug-view]") {
    constexpr std::array temporalNames{"off", "raw", "taa", "metalfx"};
    constexpr std::array temporalModes{TemporalMode::Off, TemporalMode::Raw, TemporalMode::Taa,
                                       TemporalMode::Vendor};
    constexpr std::array lightNames{"off", "direct", "clustered"};
    constexpr std::array lightModes{engine::LocalLightMode::Off, engine::LocalLightMode::Direct,
                                    engine::LocalLightMode::Clustered};
    for (size_t temporal = 0; temporal < temporalNames.size(); ++temporal) {
        for (size_t light = 0; light < lightNames.size(); ++light) {
            for (const bool occlusion : {false, true}) {
                for (const bool gpu : {false, true}) {
                    EditorRenderSettings settings;
                    settings.temporalEnabled = temporalModes[temporal] != TemporalMode::Off;
                    settings.reconstruction = temporalReconstructionMode(temporalModes[temporal]);
                    settings.localLightMode = lightModes[light];
                    settings.occlusionEnabled = occlusion;
                    settings.classifyMode =
                        gpu ? render::ClassifyMode::Gpu : render::ClassifyMode::Cpu;
                    const auto entries = debugViewEntries(settings, 31);
                    REQUIRE(entries.size() == 40);
                    for (size_t i = 0; i < entries.size(); ++i) {
                        const auto level = std::to_string(i >= 9 ? i - 9 : 0);
                        const auto view =
                            i < 9
                                ? kViews[i]
                                : ViewCase{{DebugViewTopic::Occlusion, static_cast<uint8_t>(i - 9)},
                                           "--hzb-level",
                                           level};
                        CAPTURE(temporalNames[temporal], lightNames[light], occlusion, gpu,
                                view.flag, view.value);
                        const std::array<std::string_view, 14> args{
                            "--scene",        "sponza",
                            "--screenshot",   "o.png",
                            "--temporal",     temporalNames[temporal],
                            "--local-lights", lightNames[light],
                            "--occlusion",    occlusion ? "on" : "off",
                            "--classify",     gpu ? "gpu" : "cpu",
                            view.flag,        view.value};
                        CHECK(entries[i].view.topic == view.view.topic);
                        CHECK(entries[i].view.value == view.view.value);
                        CHECK_FALSE(entries[i].label.empty());
                        CHECK(entries[i].available == parseAppOptions(args).has_value());
                        CHECK(entries[i].reason.empty() == entries[i].available);
                        auto selected = settings;
                        selectDebugView(selected, view.view);
                        CHECK(reconcileDebugView(selected).has_value() == !entries[i].available);
                        CHECK(activeDebugView(selected).has_value() == entries[i].available);
                        CHECK_FALSE(reconcileDebugView(selected));
                    }
                }
            }
        }
    }
}

//======================================================================================================================
TEST_CASE("Debug View selection replaces all diagnostics without changing render settings",
          "[app][debug-view]") {
    EditorRenderSettings settings;
    settings.occlusionEnabled = true;
    settings.classifyMode = render::ClassifyMode::Gpu;
    settings.renderScale = 0.7f;
    settings.lightCheck = true;
    settings.occlusionCheck = true;
    settings.classifyCheck = true;
    for (const auto& view : kViews) {
        settings.temporalDebugView = render::TemporalDebugView::MotionVectors;
        settings.lightDebugView = engine::LightDebugView::Missed;
        settings.hzbDebugLevel = 2;
        selectDebugView(settings, view.view);
        const auto active = activeDebugView(settings);
        REQUIRE(active);
        CHECK(active->topic == view.view.topic);
        CHECK(active->value == view.view.value);
        CHECK((settings.temporalDebugView != render::TemporalDebugView::Off) +
                  (settings.lightDebugView != engine::LightDebugView::Off) +
                  (settings.hzbDebugLevel >= 0) ==
              1);
        CHECK(settings.renderScale == 0.7f);
        CHECK(settings.lightCheck);
        CHECK(settings.occlusionCheck);
        CHECK(settings.classifyCheck);
        CHECK(settings.occlusionEnabled);
        CHECK(settings.classifyMode == render::ClassifyMode::Gpu);
        CHECK(settings.localLightMode == engine::LocalLightMode::Clustered);
        CHECK(settings.reconstruction == render::ReconstructionMode::NativeTaa);
        CHECK_FALSE(reconcileDebugView(settings));
        // A current view must not disable the replacements that clear it.
        for (const auto& entry : debugViewEntries(settings, 3)) {
            CHECK(entry.available);
            CHECK(entry.reason.empty());
        }
    }
    selectDebugView(settings, std::nullopt);
    requireFinal(settings);
    CHECK_FALSE(reconcileDebugView(settings));
}

//======================================================================================================================
TEST_CASE("Debug View reconciles changed prerequisites to Final once", "[app][debug-view]") {
    EditorRenderSettings settings;
    settings.occlusionEnabled = true;
    settings.classifyMode = render::ClassifyMode::Gpu;
    SECTION("MetalFX replaces native reconstruction under Rejection") {
        selectDebugView(settings, DebugView{DebugViewTopic::Temporal, 4});
        settings.reconstruction = render::ReconstructionMode::VendorTemporal;
    }
    SECTION("temporal is disabled under Motion") {
        selectDebugView(settings, DebugView{DebugViewTopic::Temporal, 1});
        settings.temporalEnabled = false;
    }
    SECTION("occlusion is disabled under HZB") {
        selectDebugView(settings, DebugView{DebugViewTopic::Occlusion, 1});
        settings.occlusionEnabled = false;
    }
    SECTION("classifier becomes CPU under HZB") {
        selectDebugView(settings, DebugView{DebugViewTopic::Occlusion, 1});
        settings.classifyMode = render::ClassifyMode::Cpu;
    }
    SECTION("culling is disabled under HZB") {
        selectDebugView(settings, DebugView{DebugViewTopic::Occlusion, 1});
        settings.visibilityEnabled = false;
    }
    SECTION("Clustered becomes Direct under light Count") {
        selectDebugView(settings, DebugView{DebugViewTopic::Lighting, 1});
        settings.localLightMode = engine::LocalLightMode::Direct;
    }
    SECTION("conflicting diagnostic fields have no valid active view") {
        settings.temporalDebugView = render::TemporalDebugView::MotionVectors;
        settings.lightDebugView = engine::LightDebugView::Count;
    }
    SECTION("out of range temporal value") {
        settings.temporalDebugView = static_cast<render::TemporalDebugView>(255);
    }
    SECTION("out of range lighting value") {
        settings.lightDebugView = static_cast<engine::LightDebugView>(255);
    }
    SECTION("out of range HZB value cannot wrap to a valid level") {
        settings.hzbDebugLevel = 256;
    }
    const auto notice = reconcileDebugView(settings);
    REQUIRE(notice);
    CHECK_FALSE(notice->empty());
    CHECK(notice->find("Final") != std::string::npos);
    requireFinal(settings);
    CHECK_FALSE(reconcileDebugView(settings));
    // Restoring prerequisites does not silently re-enter a diagnostic.
    settings.temporalEnabled = true;
    settings.reconstruction = render::ReconstructionMode::NativeTaa;
    settings.occlusionEnabled = true;
    settings.visibilityEnabled = true;
    settings.classifyMode = render::ClassifyMode::Gpu;
    settings.localLightMode = engine::LocalLightMode::Clustered;
    CHECK_FALSE(reconcileDebugView(settings));
    requireFinal(settings);
}

//======================================================================================================================
TEST_CASE("Debug View keeps supported diagnostics across reconstruction changes",
          "[app][debug-view]") {
    EditorRenderSettings settings;
    for (const auto mode : {render::ReconstructionMode::Raw, render::ReconstructionMode::NativeTaa,
                            render::ReconstructionMode::VendorTemporal}) {
        settings.reconstruction = mode;
        for (const uint8_t value : {1, 2, 3}) {
            selectDebugView(settings, DebugView{DebugViewTopic::Temporal, value});
            CHECK_FALSE(reconcileDebugView(settings));
            REQUIRE(activeDebugView(settings));
            CHECK(activeDebugView(settings)->value == value);
        }
    }
    for (const uint8_t value : {4, 5, 6}) {
        settings.reconstruction = render::ReconstructionMode::Raw;
        selectDebugView(settings, DebugView{DebugViewTopic::Temporal, value});
        CHECK_FALSE(reconcileDebugView(settings));
        settings.reconstruction = render::ReconstructionMode::VendorTemporal;
        REQUIRE(reconcileDebugView(settings));
        requireFinal(settings);
    }
}

//======================================================================================================================
TEST_CASE("Debug View exposes bounded HZB levels and reports missing prerequisites",
          "[app][debug-view]") {
    EditorRenderSettings settings;
    auto entries = debugViewEntries(settings, 0);
    REQUIRE(entries.size() == 10);
    CHECK(entries.back().view.topic == DebugViewTopic::Occlusion);
    CHECK_FALSE(entries.back().available);
    CHECK_FALSE(entries.back().reason.empty());
    settings.occlusionEnabled = true;
    settings.classifyMode = render::ClassifyMode::Gpu;
    entries = debugViewEntries(settings, 0);
    CHECK_FALSE(entries.back().available);
    CHECK_FALSE(entries.back().reason.empty());
    entries = debugViewEntries(settings, 1000);
    REQUIRE(entries.size() == 40);
    CHECK(entries.back().view.value == 30);
    CHECK(entries.back().available);
    settings.visibilityEnabled = false;
    for (const auto& entry : debugViewEntries(settings, 3)) {
        CHECK_FALSE(entry.available);
        CHECK_FALSE(entry.reason.empty());
    }
}
