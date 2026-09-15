//----------------------------------------------------------------------------------------------------------------------
/// @file WorkspaceModel.cpp
/// @brief Implements panel visibility and the workspace persistence schema decision.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/WorkspaceModel.h"

#include "Core/Assert.h"
#include "Core/Parse.h"

namespace lmx::app {

namespace {

constexpr std::string_view kSchemaKey = "Schema";
constexpr std::string_view kUiScaleKey = "UiScalePercent";
constexpr std::string_view kSceneKey = "Scene";
constexpr std::string_view kViewportKey = "Viewport";
constexpr std::string_view kInspectorKey = "Inspector";
constexpr std::string_view kPerformanceKey = "Performance";
constexpr std::string_view kConsoleKey = "Console";
constexpr std::string_view kRenderGraphKey = "RenderGraph";

//======================================================================================================================
// A `Key=Value` line, sans any trailing '\r' a Windows-authored ini might carry. No
// leading/trailing whitespace trimming beyond that: the writer emits none, and tolerating stray
// whitespace within a hand-edited key or value would only invite silent misreads.
std::string_view stripCarriageReturn(std::string_view line) {
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return line;
}

//======================================================================================================================
// `0`/`1` only, matching what writeWorkspaceSettings emits. Anything else leaves `value` untouched
// so the field keeps its WorkspaceVisibility default, the same tolerance parseWorkspaceSettings
// documents for unknown keys.
void applyBoolValue(std::string_view text, bool& value) {
    if (text == "1") {
        value = true;
    } else if (text == "0") {
        value = false;
    }
}

} // namespace

//======================================================================================================================
uint32_t normalizedUiScalePercent(uint32_t percent) {
    return percent >= kUiScalePresets.front() && percent <= kUiScalePresets.back()
               ? percent
               : kDefaultUiScalePercent;
}

//======================================================================================================================
uint32_t stepUiScalePercent(uint32_t percent, bool zoomIn) {
    percent = normalizedUiScalePercent(percent);
    if (zoomIn) {
        for (const uint32_t preset : kUiScalePresets) {
            if (preset > percent) {
                return preset;
            }
        }
        return kUiScalePresets.back();
    }
    for (auto preset = kUiScalePresets.rbegin(); preset != kUiScalePresets.rend(); ++preset) {
        if (*preset < percent) {
            return *preset;
        }
    }
    return kUiScalePresets.front();
}

//======================================================================================================================
bool WorkspaceVisibility::isVisible(EditorPanel panel) const {
    switch (panel) {
    case EditorPanel::Scene:
        return scene;
    case EditorPanel::Viewport:
        return viewport;
    case EditorPanel::Inspector:
        return inspector;
    case EditorPanel::Performance:
        return performance;
    case EditorPanel::Console:
        return console;
    case EditorPanel::RenderGraph:
        return renderGraph;
    }
    LMX_ASSERT(false, "WorkspaceVisibility::isVisible: unknown EditorPanel");
    return false;
}

//======================================================================================================================
void WorkspaceVisibility::setVisible(EditorPanel panel, bool visible) {
    switch (panel) {
    case EditorPanel::Scene:
        scene = visible;
        return;
    case EditorPanel::Viewport:
        viewport = visible;
        return;
    case EditorPanel::Inspector:
        inspector = visible;
        return;
    case EditorPanel::Performance:
        performance = visible;
        return;
    case EditorPanel::Console:
        console = visible;
        return;
    case EditorPanel::RenderGraph:
        renderGraph = visible;
        return;
    }
    LMX_ASSERT(false, "WorkspaceVisibility::setVisible: unknown EditorPanel");
}

//======================================================================================================================
ParsedWorkspaceSettings parseWorkspaceSettings(std::string_view sectionText) {
    // `parsed.schemaState` starts at its default, `Absent`; the `Schema` key branch below is the
    // only place that changes it, so a section with no such key leaves it correctly unmodified.
    ParsedWorkspaceSettings parsed;

    size_t cursor = 0;
    while (cursor <= sectionText.size()) {
        const size_t newline = sectionText.find('\n', cursor);
        const size_t end = newline == std::string_view::npos ? sectionText.size() : newline;
        const std::string_view line = stripCarriageReturn(sectionText.substr(cursor, end - cursor));
        cursor = newline == std::string_view::npos ? sectionText.size() + 1 : newline + 1;

        const size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            continue; // Blank line or a line with no '=': not a key we understand.
        }
        const std::string_view key = line.substr(0, equals);
        const std::string_view value = line.substr(equals + 1);

        if (key == kSchemaKey) {
            uint32_t version = 0;
            if (parseNumber(value, version)) {
                parsed.schemaState = WorkspaceSchemaState::Present;
                parsed.schemaVersion = version;
            } else {
                parsed.schemaState = WorkspaceSchemaState::Unparseable;
            }
        } else if (key == kUiScaleKey) {
            uint32_t percent = kDefaultUiScalePercent;
            parsed.uiScalePercent = parseNumber(value, percent) ? normalizedUiScalePercent(percent)
                                                                : kDefaultUiScalePercent;
        } else if (key == kSceneKey) {
            applyBoolValue(value, parsed.visibility.scene);
        } else if (key == kViewportKey) {
            applyBoolValue(value, parsed.visibility.viewport);
        } else if (key == kInspectorKey) {
            applyBoolValue(value, parsed.visibility.inspector);
        } else if (key == kPerformanceKey) {
            applyBoolValue(value, parsed.visibility.performance);
        } else if (key == kConsoleKey) {
            applyBoolValue(value, parsed.visibility.console);
        } else if (key == kRenderGraphKey) {
            applyBoolValue(value, parsed.visibility.renderGraph);
        }
        // Unknown keys are ignored, per this function's documented contract.
    }

    return parsed;
}

//======================================================================================================================
std::string writeWorkspaceSettings(uint32_t schemaVersion, const WorkspaceVisibility& visibility,
                                   uint32_t uiScalePercent) {
    std::string text;
    text += kSchemaKey;
    text += '=';
    text += std::to_string(schemaVersion);
    text += '\n';

    const auto writeBool = [&text](std::string_view key, bool value) {
        text += key;
        text += '=';
        text += value ? '1' : '0';
        text += '\n';
    };
    writeBool(kSceneKey, visibility.scene);
    writeBool(kViewportKey, visibility.viewport);
    writeBool(kInspectorKey, visibility.inspector);
    writeBool(kPerformanceKey, visibility.performance);
    writeBool(kRenderGraphKey, visibility.renderGraph);
    writeBool(kConsoleKey, visibility.console);
    text += kUiScaleKey;
    text += '=';
    text += std::to_string(normalizedUiScalePercent(uiScalePercent));
    text += '\n';
    return text;
}

//======================================================================================================================
WorkspaceDecision decideWorkspace(const std::optional<ParsedWorkspaceSettings>& parsed) {
    if (parsed.has_value() && parsed->schemaState == WorkspaceSchemaState::Present &&
        parsed->schemaVersion == kWorkspaceSchemaVersion) {
        return WorkspaceDecision{.kind = WorkspaceDecisionKind::Restore,
                                 .visibility = parsed->visibility,
                                 .uiScalePercent =
                                     normalizedUiScalePercent(parsed->uiScalePercent)};
    }
    if (parsed.has_value() && parsed->schemaState == WorkspaceSchemaState::Present &&
        parsed->schemaVersion == 2) {
        return WorkspaceDecision{.kind = WorkspaceDecisionKind::BuildDefault,
                                 .visibility = resetWorkspaceVisibility(),
                                 .uiScalePercent =
                                     normalizedUiScalePercent(parsed->uiScalePercent)};
    }
    // Unknown schemas cannot establish a compatible layout or preference contract.
    return WorkspaceDecision{.kind = WorkspaceDecisionKind::BuildDefault,
                             .visibility = resetWorkspaceVisibility()};
}

//======================================================================================================================
WorkspaceVisibility resetWorkspaceVisibility() {
    return WorkspaceVisibility{};
}

} // namespace lmx::app
