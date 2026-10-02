#include "App/Model/Rendering/Settings/RenderSettingCommands.h"

#include "App/Model/Options/AppOptions.h"
#include "Render/Passes/Temporal/Temporal.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>
#include <tuple>
#include <vector>

using namespace lmx::app;

namespace {

struct SettingCase {
    RenderSettingKey key;
    std::string_view name;
    std::vector<std::string_view> values;
};

const std::array<SettingCase, 10> kCases{{
    {RenderSettingKey::Temporal, "temporal", {"off", "raw", "taa", "metalfx"}},
    {RenderSettingKey::RenderScale, "render-scale", {"0.5", "0.75", "1"}},
    {RenderSettingKey::Visibility, "visibility", {"off", "cull"}},
    {RenderSettingKey::Classify, "classify", {"cpu", "gpu"}},
    {RenderSettingKey::ClassifyCheck, "classify-check", {"off", "on"}},
    {RenderSettingKey::Occlusion, "occlusion", {"off", "on"}},
    {RenderSettingKey::OcclusionCheck, "occlusion-check", {"off", "on"}},
    {RenderSettingKey::Submission, "submission", {"direct", "indirect", "batched"}},
    {RenderSettingKey::LocalLights, "local-lights", {"off", "direct", "clustered"}},
    {RenderSettingKey::LightCheck, "light-check", {"off", "on"}},
}};

//======================================================================================================================
// This is the panel's pre-extraction edit behavior, expressed independently of the command layer.
bool panelEdit(EditorRenderSettings& s, RenderSettingKey key, std::string_view value) {
    using lmx::render::ClassifyMode;
    using lmx::render::ReconstructionMode;
    using lmx::render::SubmissionMode;
    switch (key) {
    case RenderSettingKey::Temporal:
        if (value == "off")
            s.temporalEnabled = false;
        else {
            s.temporalEnabled = true;
            s.reconstruction = value == "raw"   ? ReconstructionMode::Raw
                               : value == "taa" ? ReconstructionMode::NativeTaa
                                                : ReconstructionMode::VendorTemporal;
        }
        return true;
    case RenderSettingKey::RenderScale:
        if (!s.temporalEnabled || s.dynamicResolutionEnabled)
            return false;
        s.renderScale = value == "0.5" ? 0.5f : value == "0.75" ? 0.75f : 1.0f;
        return true;
    case RenderSettingKey::Visibility:
        s.visibilityEnabled = value == "cull";
        if (!s.visibilityEnabled) {
            s.occlusionEnabled = false;
            s.occlusionCheck = false;
        }
        return true;
    case RenderSettingKey::Classify:
        s.classifyMode = value == "gpu" ? ClassifyMode::Gpu : ClassifyMode::Cpu;
        if (s.classifyMode == ClassifyMode::Gpu && s.submission == SubmissionMode::Direct)
            s.submission = SubmissionMode::Indirect;
        if (s.classifyMode == ClassifyMode::Cpu) {
            s.classifyCheck = false;
            s.occlusionEnabled = false;
            s.occlusionCheck = false;
        }
        return true;
    case RenderSettingKey::ClassifyCheck:
        if (value == "on" && s.classifyMode != ClassifyMode::Gpu)
            return false;
        s.classifyCheck = value == "on";
        return true;
    case RenderSettingKey::Occlusion:
        if (value == "on" && (s.classifyMode != ClassifyMode::Gpu || !s.visibilityEnabled))
            return false;
        s.occlusionEnabled = value == "on";
        if (!s.occlusionEnabled)
            s.occlusionCheck = false;
        return true;
    case RenderSettingKey::OcclusionCheck:
        if (value == "on" && !s.occlusionEnabled)
            return false;
        s.occlusionCheck = value == "on";
        return true;
    case RenderSettingKey::Submission:
        s.submission = value == "direct"     ? SubmissionMode::Direct
                       : value == "indirect" ? SubmissionMode::Indirect
                                             : SubmissionMode::Batched;
        if (s.submission == SubmissionMode::Direct) {
            s.classifyMode = ClassifyMode::Cpu;
            s.classifyCheck = false;
            s.occlusionEnabled = false;
            s.occlusionCheck = false;
        }
        return true;
    case RenderSettingKey::LocalLights:
        s.localLightMode = value == "off"      ? lmx::engine::LocalLightMode::Off
                           : value == "direct" ? lmx::engine::LocalLightMode::Direct
                                               : lmx::engine::LocalLightMode::Clustered;
        if (s.localLightMode != lmx::engine::LocalLightMode::Clustered)
            s.lightCheck = false;
        return true;
    case RenderSettingKey::LightCheck:
        s.lightCheck = value == "on";
        if (s.lightCheck)
            s.localLightMode = lmx::engine::LocalLightMode::Clustered;
        return true;
    }
    return false;
}

//======================================================================================================================
auto fields(const EditorRenderSettings& s) {
    return std::tuple{
        s.localLightMode,       s.lightDebugView,    s.lightCheck,     s.visibilityEnabled,
        s.classifyMode,         s.occlusionEnabled,  s.occlusionCheck, s.hzbDebugLevel,
        s.showOcclusionBounds,  s.classifyCheck,     s.submission,     s.wireframe,
        s.poolingEnabled,       s.temporalEnabled,   s.jitterEnabled,  s.reconstruction,
        s.temporalDebugView,    s.followCameraTrack, s.renderScale,    s.dynamicResolutionEnabled,
        s.gpuBudgetMilliseconds};
}

} // namespace

//======================================================================================================================
TEST_CASE("render setting commands preserve every panel cascade across 24 visibility states",
          "[app][render-setting-commands]") {
    for (int classify = 0; classify < 2; ++classify)
        for (int visibility = 0; visibility < 2; ++visibility)
            for (int occlusion = 0; occlusion < 2; ++occlusion)
                for (int submission = 0; submission < 3; ++submission)
                    for (const auto& item : kCases)
                        for (const auto value : item.values) {
                            EditorRenderSettings before;
                            before.classifyMode = static_cast<lmx::render::ClassifyMode>(classify);
                            before.visibilityEnabled = visibility != 0;
                            before.occlusionEnabled = occlusion != 0;
                            before.occlusionCheck = occlusion != 0;
                            before.classifyCheck = classify != 0;
                            before.submission =
                                static_cast<lmx::render::SubmissionMode>(submission);
                            before.lightCheck = true;
                            auto expected = before;
                            auto actual = before;
                            const bool available = panelEdit(expected, item.key, value);
                            const auto result = applyRenderSetting(actual, item.key, value);
                            INFO(item.name << '=' << value << " from " << classify << visibility
                                           << occlusion << submission);
                            CHECK(result.has_value() == available);
                            if (!result)
                                CHECK_FALSE(result.error().empty());
                            CHECK(fields(actual) == fields(available ? expected : before));
                        }
}

//======================================================================================================================
TEST_CASE("render setting names, values and errors round-trip", "[app][render-setting-commands]") {
    for (const auto& item : kCases) {
        CHECK(renderSettingName(item.key) == item.name);
        CHECK(parseRenderSettingName(item.name) == item.key);
        for (const auto value : item.values) {
            EditorRenderSettings settings;
            if (item.key == RenderSettingKey::ClassifyCheck ||
                item.key == RenderSettingKey::Occlusion ||
                item.key == RenderSettingKey::OcclusionCheck)
                settings.classifyMode = lmx::render::ClassifyMode::Gpu;
            if (item.key == RenderSettingKey::OcclusionCheck)
                settings.occlusionEnabled = true;
            const auto applied = applyRenderSetting(settings, item.key, value);
            REQUIRE(applied.has_value());
            const auto encoded = renderSettingValue(settings, item.key);
            auto copy = settings;
            const auto roundTrip = applyRenderSetting(copy, item.key, encoded);
            REQUIRE(roundTrip.has_value());
            CHECK(fields(copy) == fields(settings));
        }
        EditorRenderSettings settings;
        const auto before = fields(settings);
        const auto bad = applyRenderSetting(settings, item.key, "definitely-invalid");
        REQUIRE_FALSE(bad.has_value());
        CHECK_FALSE(bad.error().empty());
        CHECK(fields(settings) == before);
    }
    CHECK_FALSE(parseRenderSettingName("local-light-rig"));
    CHECK_FALSE(parseRenderSettingName("unknown"));
}

//======================================================================================================================
TEST_CASE("panel render-scale text preserves the exact float and render extent",
          "[app][render-setting-commands]") {
    constexpr float requested = 0.5002604f;
    EditorRenderSettings settings;
    REQUIRE(applyRenderSetting(settings, RenderSettingKey::RenderScale,
                               renderScaleCommandValue(requested)));
    CHECK(settings.renderScale == requested);
    CHECK(lmx::render::renderExtentsForScale(1920, 1080, settings.renderScale).renderWidth ==
          lmx::render::renderExtentsForScale(1920, 1080, requested).renderWidth);
    CHECK(lmx::render::renderExtentsForScale(1920, 1080, requested).renderWidth == 961);
    CHECK(lmx::render::renderExtentsForScale(1920, 1080, std::stof(std::to_string(requested)))
              .renderWidth == 960);
}

//======================================================================================================================
TEST_CASE("unavailable edits reject without mutation and cascades produce CLI-valid states",
          "[app][render-setting-commands]") {
    const auto unavailable = [](EditorRenderSettings settings, RenderSettingKey key,
                                std::string_view value) {
        const auto before = fields(settings);
        const auto result = applyRenderSetting(settings, key, value);
        REQUIRE_FALSE(result.has_value());
        CHECK_FALSE(result.error().empty());
        CHECK(fields(settings) == before);
    };
    unavailable({}, RenderSettingKey::ClassifyCheck, "on");
    unavailable({}, RenderSettingKey::Occlusion, "on");
    unavailable({}, RenderSettingKey::OcclusionCheck, "on");
    EditorRenderSettings staleCheck;
    staleCheck.occlusionEnabled = false;
    staleCheck.occlusionCheck = true;
    REQUIRE(applyRenderSetting(staleCheck, RenderSettingKey::OcclusionCheck, "off"));
    CHECK_FALSE(staleCheck.occlusionCheck);
    unavailable({}, RenderSettingKey::Temporal, "invalid");
    for (const auto value : {"nan", "inf", "-inf", "abc", "0.75x"})
        unavailable({}, RenderSettingKey::RenderScale, value);
    EditorRenderSettings settings;
    REQUIRE(applyRenderSetting(settings, RenderSettingKey::Temporal, "off"));
    unavailable(settings, RenderSettingKey::RenderScale, "0.75");
    settings.temporalEnabled = true;
    settings.dynamicResolutionEnabled = true;
    unavailable(settings, RenderSettingKey::RenderScale, "0.75");

    settings = {};
    REQUIRE(applyRenderSetting(settings, RenderSettingKey::Submission, "direct"));
    REQUIRE(applyRenderSetting(settings, RenderSettingKey::Classify, "gpu"));
    CHECK(settings.submission == lmx::render::SubmissionMode::Indirect);
    REQUIRE(applyRenderSetting(settings, RenderSettingKey::Occlusion, "on"));
    REQUIRE(applyRenderSetting(settings, RenderSettingKey::Submission, "direct"));
    CHECK(settings.classifyMode == lmx::render::ClassifyMode::Cpu);
    CHECK_FALSE(settings.occlusionEnabled);
    REQUIRE(applyRenderSetting(settings, RenderSettingKey::Classify, "gpu"));
    REQUIRE(applyRenderSetting(settings, RenderSettingKey::Occlusion, "on"));
    REQUIRE(applyRenderSetting(settings, RenderSettingKey::Visibility, "off"));
    CHECK_FALSE(settings.occlusionEnabled);
    REQUIRE(applyRenderSetting(settings, RenderSettingKey::LocalLights, "direct"));
    REQUIRE(applyRenderSetting(settings, RenderSettingKey::LightCheck, "on"));
    CHECK(settings.localLightMode == lmx::engine::LocalLightMode::Clustered);

    const auto cliConstraintsHold = [](const EditorRenderSettings& value) {
        const auto temporal = renderSettingValue(value, RenderSettingKey::Temporal);
        const auto submission = renderSettingValue(value, RenderSettingKey::Submission);
        const auto localLights = renderSettingValue(value, RenderSettingKey::LocalLights);
        std::vector<std::string_view> args{
            "--temporal",     temporal,
            "--visibility",   value.visibilityEnabled ? "cull" : "off",
            "--classify",     value.classifyMode == lmx::render::ClassifyMode::Gpu ? "gpu" : "cpu",
            "--occlusion",    value.occlusionEnabled ? "on" : "off",
            "--submission",   submission,
            "--local-lights", localLights,
        };
        if (value.classifyCheck)
            args.push_back("--classify-check");
        if (value.occlusionCheck)
            args.push_back("--occlusion-check");
        if (value.lightCheck)
            args.push_back("--light-check");
        return parseAppOptions(args).has_value();
    };
    CHECK(cliConstraintsHold(settings));
    for (const auto& item : kCases)
        for (const auto value : item.values) {
            EditorRenderSettings reachable;
            if (item.key == RenderSettingKey::Occlusion ||
                item.key == RenderSettingKey::OcclusionCheck)
                REQUIRE(applyRenderSetting(reachable, RenderSettingKey::Classify, "gpu"));
            if (item.key == RenderSettingKey::OcclusionCheck)
                REQUIRE(applyRenderSetting(reachable, RenderSettingKey::Occlusion, "on"));
            if (applyRenderSetting(reachable, item.key, value))
                CHECK(cliConstraintsHold(reachable));
        }
}
