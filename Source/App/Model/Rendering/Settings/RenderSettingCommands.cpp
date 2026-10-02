//----------------------------------------------------------------------------------------------------------------------
/// @file RenderSettingCommands.cpp
/// @brief Applies panel rendering edits with shared dependent-setting behavior.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Rendering/Settings/RenderSettingCommands.h"

#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <system_error>

namespace lmx::app {

namespace {

constexpr std::array<std::string_view, 10> kNames{
    "temporal",  "render-scale",    "visibility", "classify",     "classify-check",
    "occlusion", "occlusion-check", "submission", "local-lights", "light-check"};

//======================================================================================================================
std::expected<bool, std::string> parseSwitch(std::string_view value) {
    if (value == "on")
        return true;
    if (value == "off")
        return false;
    return std::unexpected("Expected on or off.");
}

} // namespace

//======================================================================================================================
std::string_view renderSettingName(RenderSettingKey key) {
    const auto index = static_cast<size_t>(key);
    return index < kNames.size() ? kNames[index] : std::string_view{};
}

//======================================================================================================================
std::optional<RenderSettingKey> parseRenderSettingName(std::string_view name) {
    for (size_t index = 0; index < kNames.size(); ++index)
        if (name == kNames[index])
            return static_cast<RenderSettingKey>(index);
    return std::nullopt;
}

//======================================================================================================================
std::string renderScaleCommandValue(float scale) {
    return std::format("{}", scale);
}

//======================================================================================================================
std::expected<void, std::string> applyRenderSetting(EditorRenderSettings& settings,
                                                    RenderSettingKey key, std::string_view value) {
    using render::ClassifyMode;
    using render::ReconstructionMode;
    using render::SubmissionMode;
    switch (key) {
    case RenderSettingKey::Temporal:
        if (value == "off")
            settings.temporalEnabled = false;
        else if (value == "raw" || value == "taa" || value == "metalfx") {
            settings.temporalEnabled = true;
            settings.reconstruction = value == "raw"   ? ReconstructionMode::Raw
                                      : value == "taa" ? ReconstructionMode::NativeTaa
                                                       : ReconstructionMode::VendorTemporal;
        } else
            return std::unexpected("Expected off, raw, taa or metalfx.");
        break;
    case RenderSettingKey::RenderScale: {
        float scale = 0.0f;
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), scale);
        if (error != std::errc{} || end != value.data() + value.size() || !std::isfinite(scale) ||
            scale < render::kMinRenderScale || scale > 1.0f)
            return std::unexpected("Render scale must be between 0.5 and 1.0.");
        if (!settings.temporalEnabled)
            return std::unexpected("Enable temporal inputs to edit render scale.");
        if (settings.dynamicResolutionEnabled)
            return std::unexpected("Disable dynamic resolution to edit render scale.");
        settings.renderScale = scale;
        break;
    }
    case RenderSettingKey::Visibility:
        if (value == "cull")
            settings.visibilityEnabled = true;
        else if (value == "off")
            settings.visibilityEnabled = false;
        else
            return std::unexpected("Expected off or cull.");
        if (!settings.visibilityEnabled) {
            settings.occlusionEnabled = false;
            settings.occlusionCheck = false;
        }
        break;
    case RenderSettingKey::Classify:
        if (value == "cpu")
            settings.classifyMode = ClassifyMode::Cpu;
        else if (value == "gpu")
            settings.classifyMode = ClassifyMode::Gpu;
        else
            return std::unexpected("Expected cpu or gpu.");
        if (settings.classifyMode == ClassifyMode::Gpu &&
            settings.submission == SubmissionMode::Direct)
            settings.submission = SubmissionMode::Indirect;
        if (settings.classifyMode == ClassifyMode::Cpu) {
            settings.classifyCheck = false;
            settings.occlusionEnabled = false;
            settings.occlusionCheck = false;
        }
        break;
    case RenderSettingKey::ClassifyCheck: {
        const auto enabled = parseSwitch(value);
        if (!enabled)
            return std::unexpected(enabled.error());
        if (*enabled && settings.classifyMode != ClassifyMode::Gpu)
            return std::unexpected("CPU oracle check requires GPU classification.");
        settings.classifyCheck = *enabled;
        break;
    }
    case RenderSettingKey::Occlusion: {
        const auto enabled = parseSwitch(value);
        if (!enabled)
            return std::unexpected(enabled.error());
        if (*enabled && (settings.classifyMode != ClassifyMode::Gpu || !settings.visibilityEnabled))
            return std::unexpected("Requires GPU classification and frustum culling.");
        settings.occlusionEnabled = *enabled;
        if (!*enabled)
            settings.occlusionCheck = false;
        break;
    }
    case RenderSettingKey::OcclusionCheck: {
        const auto enabled = parseSwitch(value);
        if (!enabled)
            return std::unexpected(enabled.error());
        if (*enabled && !settings.occlusionEnabled)
            return std::unexpected("Independent ID check requires occlusion.");
        settings.occlusionCheck = *enabled;
        break;
    }
    case RenderSettingKey::Submission:
        if (value == "direct")
            settings.submission = SubmissionMode::Direct;
        else if (value == "indirect")
            settings.submission = SubmissionMode::Indirect;
        else if (value == "batched")
            settings.submission = SubmissionMode::Batched;
        else
            return std::unexpected("Expected direct, indirect or batched.");
        if (settings.submission == SubmissionMode::Direct) {
            settings.classifyMode = ClassifyMode::Cpu;
            settings.classifyCheck = false;
            settings.occlusionEnabled = false;
            settings.occlusionCheck = false;
        }
        break;
    case RenderSettingKey::LocalLights:
        if (value == "off")
            settings.localLightMode = engine::LocalLightMode::Off;
        else if (value == "direct")
            settings.localLightMode = engine::LocalLightMode::Direct;
        else if (value == "clustered")
            settings.localLightMode = engine::LocalLightMode::Clustered;
        else
            return std::unexpected("Expected off, direct or clustered.");
        if (settings.localLightMode != engine::LocalLightMode::Clustered)
            settings.lightCheck = false;
        break;
    case RenderSettingKey::LightCheck: {
        const auto enabled = parseSwitch(value);
        if (!enabled)
            return std::unexpected(enabled.error());
        settings.lightCheck = *enabled;
        if (*enabled)
            settings.localLightMode = engine::LocalLightMode::Clustered;
        break;
    }
    default:
        return std::unexpected("Unknown render setting.");
    }
    return {};
}

//======================================================================================================================
std::string renderSettingValue(const EditorRenderSettings& settings, RenderSettingKey key) {
    switch (key) {
    case RenderSettingKey::Temporal:
        if (!settings.temporalEnabled)
            return "off";
        switch (settings.reconstruction) {
        case render::ReconstructionMode::Raw:
            return "raw";
        case render::ReconstructionMode::NativeTaa:
            return "taa";
        case render::ReconstructionMode::VendorTemporal:
            return "metalfx";
        }
        break;
    case RenderSettingKey::RenderScale:
        return renderScaleCommandValue(settings.renderScale);
    case RenderSettingKey::Visibility:
        return settings.visibilityEnabled ? "cull" : "off";
    case RenderSettingKey::Classify:
        return settings.classifyMode == render::ClassifyMode::Gpu ? "gpu" : "cpu";
    case RenderSettingKey::ClassifyCheck:
        return settings.classifyCheck ? "on" : "off";
    case RenderSettingKey::Occlusion:
        return settings.occlusionEnabled ? "on" : "off";
    case RenderSettingKey::OcclusionCheck:
        return settings.occlusionCheck ? "on" : "off";
    case RenderSettingKey::Submission:
        switch (settings.submission) {
        case render::SubmissionMode::Direct:
            return "direct";
        case render::SubmissionMode::Indirect:
            return "indirect";
        case render::SubmissionMode::Batched:
            return "batched";
        }
        break;
    case RenderSettingKey::LocalLights:
        switch (settings.localLightMode) {
        case engine::LocalLightMode::Off:
            return "off";
        case engine::LocalLightMode::Direct:
            return "direct";
        case engine::LocalLightMode::Clustered:
            return "clustered";
        }
        break;
    case RenderSettingKey::LightCheck:
        return settings.lightCheck ? "on" : "off";
    }
    return {};
}

//======================================================================================================================
std::vector<std::string> settingsToArguments(const EditorRenderSettings& settings,
                                             const AppOptions& startup, bool localLightRig) {
    std::vector<std::string> arguments;
    const auto add = [&arguments](std::string_view flag, std::string value) {
        arguments.emplace_back(flag);
        arguments.push_back(std::move(value));
    };
    for (const auto key : {RenderSettingKey::Temporal, RenderSettingKey::Visibility,
                           RenderSettingKey::Classify, RenderSettingKey::Occlusion,
                           RenderSettingKey::Submission, RenderSettingKey::LocalLights}) {
        add(std::format("--{}", renderSettingName(key)), renderSettingValue(settings, key));
    }
    // The editor retains the manual or controller scale while temporal is off. Headless runs
    // rasterize at output resolution until reconstruction is enabled again.
    add("--render-scale",
        settings.temporalEnabled ? renderScaleCommandValue(settings.renderScale) : "1");
    if (settings.classifyCheck)
        arguments.emplace_back("--classify-check");
    if (settings.occlusionCheck)
        arguments.emplace_back("--occlusion-check");
    if (settings.lightCheck)
        arguments.emplace_back("--light-check");
    add("--local-light-rig", localLightRig ? "on" : "off");
    if (startup.generatorOverrides.instances)
        add("--lab-instances", std::to_string(*startup.generatorOverrides.instances));
    if (startup.generatorOverrides.occluders)
        add("--lab-occluders", std::to_string(*startup.generatorOverrides.occluders));
    if (startup.generatorOverrides.lights)
        add("--lab-lights", std::to_string(*startup.generatorOverrides.lights));
    if (startup.generatorOverrides.pile)
        add("--lab-light-pile", std::to_string(*startup.generatorOverrides.pile));
    return arguments;
}

} // namespace lmx::app
