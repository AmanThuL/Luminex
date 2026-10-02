//----------------------------------------------------------------------------------------------------------------------
/// @file SessionApply.cpp
/// @brief Validates approved command arguments without editor or filesystem side effects.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Session/SessionApply.h"

#include "App/Model/Rendering/Settings/RenderSettingCommands.h"
#include "Core/Util/String.h"

#include <array>
#include <charconv>
#include <ctime>
#include <format>
#include <limits>

namespace lmx::app {
namespace {

//======================================================================================================================
std::expected<std::string, std::string> requiredString(const asset::JsonNode& args,
                                                       std::string_view key) {
    const auto node = args.find(key);
    const auto value = node ? node->asString()
                            : std::expected<std::string, std::string>{std::unexpected("missing")};
    if (!value || value->empty())
        return std::unexpected(std::format("{} must be a nonempty string", key));
    return *value;
}

//======================================================================================================================
std::expected<uint32_t, std::string> requiredCount(const asset::JsonNode& args,
                                                   std::string_view key, bool allowZero) {
    const auto node = args.find(key);
    const auto value =
        node ? node->asUInt() : std::expected<uint64_t, std::string>{std::unexpected("missing")};
    if (!value || *value > std::numeric_limits<uint32_t>::max() || (!allowZero && *value == 0))
        return std::unexpected(std::format("{} must be {} 32-bit integer", key,
                                           allowZero ? "a nonnegative" : "a positive"));
    return static_cast<uint32_t>(*value);
}

//======================================================================================================================
std::expected<void, std::string> validateStep(SessionCommand command, const asset::JsonNode& args) {
    if (!args.isObject())
        return std::unexpected("Apply arguments must be an object");
    if (const auto name = args.find("name"); name && name->isString()) {
        const auto value = name->asString();
        if (value && toLowerAscii(*value) == "session.json")
            return std::unexpected("session.json is reserved for Session Export");
    }
    switch (command) {
    case SessionCommand::SettingsSet:
        if (args.size() != 1)
            return std::unexpected("settings.set needs exactly one setting");
        if (args.memberName(0) != "local-light-rig" && !parseRenderSettingName(args.memberName(0)))
            return std::unexpected("Unknown rendering setting");
        if (!args.memberValue(0).isString())
            return std::unexpected("Setting value must be a string");
        if (args.memberName(0) == "local-light-rig") {
            const auto value = args.memberValue(0).asString();
            if (!value || (*value != "on" && *value != "off"))
                return std::unexpected("local-light-rig must be on or off");
            return {};
        }
        if (const auto key = parseRenderSettingName(args.memberName(0))) {
            auto permissive = EditorRenderSettings{};
            permissive.temporalEnabled = true;
            permissive.dynamicResolutionEnabled = false;
            permissive.visibilityEnabled = true;
            permissive.classifyMode = render::ClassifyMode::Gpu;
            permissive.occlusionEnabled = true;
            permissive.localLightMode = engine::LocalLightMode::Clustered;
            const auto value = args.memberValue(0).asString();
            const auto valid = applyRenderSetting(permissive, *key, *value);
            if (!valid)
                return std::unexpected(valid.error());
        }
        return {};
    case SessionCommand::DebugViewSet:
        return sessionDebugView(args) ? std::expected<void, std::string>{}
                                      : std::unexpected("Invalid debug view");
    case SessionCommand::SceneOpen:
        if (!requiredString(args, "scene"))
            return std::unexpected("scene.open needs a nonempty scene");
        return {};
    case SessionCommand::MeasureRun:
        if (!requiredCount(args, "warmup", true) || !requiredCount(args, "frames", false))
            return std::unexpected("measure.run needs bounded warmup and frames");
        if (uint64_t(*requiredCount(args, "warmup", true)) +
                uint64_t(*requiredCount(args, "frames", false)) >
            std::numeric_limits<uint32_t>::max())
            return std::unexpected("warmup plus frames exceeds the measurement range");
        if (const auto name = requiredString(args, "name"); !name || !evidenceName(*name))
            return std::unexpected("measure.run needs a safe output name");
        return {};
    case SessionCommand::GraphDump:
        if (const auto name = requiredString(args, "name"); !name || !evidenceName(*name))
            return std::unexpected("graph.dump needs a safe output name");
        return {};
    case SessionCommand::CaptureGpu:
        return args.size() == 0 ? std::expected<void, std::string>{}
                                : std::unexpected("capture.gpu takes no arguments");
    case SessionCommand::CaptureScreenshot:
    case SessionCommand::CaptureSequence:
        if (const auto name = requiredString(args, "name"); !name || !evidenceName(*name))
            return std::unexpected("capture needs a safe output name");
        if (!requiredCount(args, "frames", false))
            return std::unexpected("capture needs positive frames");
        if (command == SessionCommand::CaptureSequence && !requiredCount(args, "warmup", true))
            return std::unexpected("capture.sequence needs nonnegative warmup");
        return {};
    default:
        return std::unexpected("Only Apply commands may be approved");
    }
}

} // namespace

//======================================================================================================================
std::expected<std::optional<DebugView>, std::string> sessionDebugView(const asset::JsonNode& args) {
    if (!args.isObject())
        return std::unexpected("debugview.set needs object args");
    if (args.size() == 0)
        return std::optional<DebugView>{};
    const auto topic = requiredString(args, "topic");
    const auto value = requiredString(args, "value");
    if (args.size() != 2 || !topic || !value)
        return std::unexpected("debugview.set needs topic and value strings, or empty args");
    constexpr std::array<std::string_view, 6> temporal{"motion",    "reprojection", "reprojected",
                                                       "rejection", "weight",       "age"};
    constexpr std::array<std::string_view, 3> lighting{"count", "overflow", "missed"};
    if (*topic == "temporal") {
        for (uint8_t index = 0; index < temporal.size(); ++index)
            if (*value == temporal[index])
                return DebugView{DebugViewTopic::Temporal, static_cast<uint8_t>(index + 1)};
    } else if (*topic == "lighting") {
        for (uint8_t index = 0; index < lighting.size(); ++index)
            if (*value == lighting[index])
                return DebugView{DebugViewTopic::Lighting, static_cast<uint8_t>(index + 1)};
    } else if (*topic == "occlusion") {
        uint32_t level = 0;
        const auto [end, error] =
            std::from_chars(value->data(), value->data() + value->size(), level);
        if (error == std::errc{} && end == value->data() + value->size() && level <= 30)
            return DebugView{DebugViewTopic::Occlusion, static_cast<uint8_t>(level)};
    }
    return std::unexpected("Unknown or out-of-range debug view");
}

//======================================================================================================================
std::expected<ParsedApplyRequest, std::string> parseApplyRequest(SessionCommand command,
                                                                 const asset::JsonNode& args) {
    ParsedApplyRequest parsed;
    if (!args.isObject())
        return std::unexpected("Apply arguments must be an object");
    if (command != SessionCommand::PlanSubmit) {
        if (const auto valid = validateStep(command, args); !valid)
            return std::unexpected(valid.error());
        for (const auto& candidate : sessionCommands())
            if (candidate.command == command)
                parsed.summary = std::string(candidate.name);
        parsed.steps.push_back({command, std::string(args.sourceJson())});
        return parsed;
    }
    const auto summary = requiredString(args, "summary");
    const auto steps = args.find("steps");
    if (!summary || summary->size() > 1024 || !steps || !steps->isArray() || steps->size() == 0 ||
        steps->size() > 32)
        return std::unexpected("plan.submit needs a summary and one to 32 steps");
    parsed.summary = *summary;
    for (const auto& item : steps->elements()) {
        if (!item.isObject())
            return std::unexpected("Plan step must be an object");
        const auto name = requiredString(item, "command");
        const auto arguments = item.find("args");
        const auto* spec = name ? findCommand(*name) : nullptr;
        if (!spec || spec->tier != SessionTier::Apply ||
            spec->command == SessionCommand::PlanSubmit || !arguments || !arguments->isObject())
            return std::unexpected("Plan step must name an Apply command and object args");
        if (const auto valid = validateStep(spec->command, *arguments); !valid)
            return std::unexpected(valid.error());
        parsed.steps.push_back({spec->command, std::string(arguments->sourceJson())});
    }
    return parsed;
}

//======================================================================================================================
std::string sessionDirectoryName(int64_t utcSeconds, uint32_t processId) {
    const auto time = static_cast<std::time_t>(utcSeconds);
    std::tm utc{};
    gmtime_r(&time, &utc);
    return std::format("session/{:04}{:02}{:02}-{:02}{:02}{:02}-{}", utc.tm_year + 1900,
                       utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec, processId);
}

//======================================================================================================================
std::string captureGpuOutputName(uint64_t approval, size_t zeroBasedStep) {
    return std::format("capture-{}-{}.gputrace", approval, zeroBasedStep + 1);
}

//======================================================================================================================
bool sessionCapturePathsAvailable(const std::filesystem::path& trace) {
    const std::array paths{trace, std::filesystem::path(trace.string() + ".schema.json"),
                           std::filesystem::path(trace.string() + ".schema.json.tmp")};
    for (const auto& path : paths) {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(path, error);
        if (error && error != std::errc::no_such_file_or_directory)
            return false;
        if (!error && status.type() != std::filesystem::file_type::not_found)
            return false;
    }
    return true;
}

//======================================================================================================================
bool sessionSceneOpenAllowed(EditorSubject subject, bool dirty, bool stopped, bool documentIdle,
                             bool measuring) {
    return !dirty && stopped && documentIdle && !measuring &&
           (subject == EditorSubject::None || subject == EditorSubject::Camera ||
            subject == EditorSubject::Environment);
}

} // namespace lmx::app
