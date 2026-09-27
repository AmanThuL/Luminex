//----------------------------------------------------------------------------------------------------------------------
/// @file DebugView.cpp
/// @brief Implements diagnostic availability, exclusive selection and invalidation notices.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Rendering/Settings/DebugView.h"

#include "Core/Diagnostics/Assert.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace lmx::app {
namespace {
constexpr uint32_t kMaxHzbLevels = 31;
constexpr std::array<std::string_view, 6> kTemporalLabels{
    "Motion vectors", "Reprojection error", "Reprojected history",
    "Rejection mask", "Blend weight",       "History age"};
constexpr std::array<std::string_view, 3> kLightingLabels{"Count", "Overflow", "Missed"};

//======================================================================================================================
bool validView(DebugView view) {
    switch (view.topic) {
    case DebugViewTopic::Temporal:
        return view.value >= 1 && view.value <= kTemporalLabels.size();
    case DebugViewTopic::Lighting:
        return view.value >= 1 && view.value <= kLightingLabels.size();
    case DebugViewTopic::Occlusion:
        return view.value < kMaxHzbLevels;
    }
    return false;
}

//======================================================================================================================
std::string unavailableReason(const EditorRenderSettings& settings, DebugView view,
                              render::ReconstructionMode effectiveReconstruction) {
    if (settings.occlusionEnabled &&
        (settings.classifyMode != render::ClassifyMode::Gpu || !settings.visibilityEnabled))
        return "Occlusion needs the GPU Classifier and Frustum culling in Rendering > Visibility.";
    switch (view.topic) {
    case DebugViewTopic::Temporal:
        if (!settings.temporalEnabled)
            return "Enable temporal inputs in Rendering > Reconstruction.";
        if (effectiveReconstruction == render::ReconstructionMode::VendorTemporal &&
            render::nativeOnlyTemporalView(static_cast<render::TemporalDebugView>(view.value)))
            return "MetalFX vendor reconstruction is active; choose Raw or Native TAA in "
                   "Rendering > Reconstruction.";
        break;
    case DebugViewTopic::Lighting:
        if (settings.localLightMode != engine::LocalLightMode::Clustered)
            return "Choose Clustered in Rendering > Lighting.";
        break;
    case DebugViewTopic::Occlusion:
        if (!settings.occlusionEnabled)
            return "Enable Occlusion in Rendering > Occlusion; it needs the GPU Classifier and "
                   "Frustum culling in Rendering > Visibility.";
        break;
    }
    return {};
}
} // namespace

//======================================================================================================================
std::string hzbLevelLabel(uint32_t level) {
    return "HZB level " + std::to_string(level);
}

//======================================================================================================================
std::vector<DebugViewEntry> debugViewEntries(const EditorRenderSettings& settings,
                                             uint32_t hzbLevels,
                                             render::ReconstructionMode effectiveReconstruction) {
    std::vector<DebugViewEntry> entries;
    const auto levelCount = std::clamp(hzbLevels, 1u, kMaxHzbLevels);
    entries.reserve(kTemporalLabels.size() + kLightingLabels.size() + levelCount);
    const auto append = [&](DebugView view, std::string label) {
        auto reason = unavailableReason(settings, view, effectiveReconstruction);
        if (reason.empty() && view.topic == DebugViewTopic::Occlusion && hzbLevels == 0)
            reason = "The HZB pyramid has no available levels yet.";
        const bool available = reason.empty();
        entries.push_back({view, std::move(label), available, std::move(reason)});
    };
    for (uint8_t index = 0; index < kTemporalLabels.size(); ++index)
        append({DebugViewTopic::Temporal, static_cast<uint8_t>(index + 1)},
               std::string(kTemporalLabels[index]));
    for (uint8_t index = 0; index < kLightingLabels.size(); ++index)
        append({DebugViewTopic::Lighting, static_cast<uint8_t>(index + 1)},
               std::string(kLightingLabels[index]));
    for (uint8_t level = 0; level < levelCount; ++level)
        append({DebugViewTopic::Occlusion, level}, hzbLevelLabel(level));
    return entries;
}

//======================================================================================================================
std::optional<DebugView> activeDebugView(const EditorRenderSettings& settings) {
    const bool temporal = settings.temporalDebugView != render::TemporalDebugView::Off;
    const bool lighting = settings.lightDebugView != engine::LightDebugView::Off;
    const bool occlusion = settings.hzbDebugLevel != -1;
    if (temporal + lighting + occlusion != 1)
        return std::nullopt;
    DebugView view;
    if (temporal)
        view = {DebugViewTopic::Temporal, static_cast<uint8_t>(settings.temporalDebugView)};
    else if (lighting)
        view = {DebugViewTopic::Lighting, static_cast<uint8_t>(settings.lightDebugView)};
    else {
        if (settings.hzbDebugLevel < 0 || settings.hzbDebugLevel >= int32_t{kMaxHzbLevels})
            return std::nullopt;
        view = {DebugViewTopic::Occlusion, static_cast<uint8_t>(settings.hzbDebugLevel)};
    }
    return validView(view) ? std::optional(view) : std::nullopt;
}

//======================================================================================================================
void selectDebugView(EditorRenderSettings& settings, std::optional<DebugView> view) {
    LMX_ASSERT(!view || validView(*view), "invalid Debug View topic or value");
    settings.temporalDebugView = render::TemporalDebugView::Off;
    settings.lightDebugView = engine::LightDebugView::Off;
    settings.hzbDebugLevel = -1;
    if (!view)
        return;
    switch (view->topic) {
    case DebugViewTopic::Temporal:
        settings.temporalDebugView = static_cast<render::TemporalDebugView>(view->value);
        break;
    case DebugViewTopic::Lighting:
        settings.lightDebugView = static_cast<engine::LightDebugView>(view->value);
        break;
    case DebugViewTopic::Occlusion:
        settings.hzbDebugLevel = view->value;
        break;
    }
}

//======================================================================================================================
std::optional<std::string> reconcileDebugView(EditorRenderSettings& settings,
                                              render::ReconstructionMode effectiveReconstruction) {
    if (settings.temporalDebugView == render::TemporalDebugView::Off &&
        settings.lightDebugView == engine::LightDebugView::Off && settings.hzbDebugLevel == -1)
        return std::nullopt;
    const auto view = activeDebugView(settings);
    const auto reason = view ? unavailableReason(settings, *view, effectiveReconstruction)
                             : "Diagnostic requests conflict or contain an unsupported value.";
    if (reason.empty())
        return std::nullopt;
    selectDebugView(settings, std::nullopt);
    return "Debug View returned to Final. " + reason;
}

} // namespace lmx::app
