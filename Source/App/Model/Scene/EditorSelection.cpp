//----------------------------------------------------------------------------------------------------------------------
/// @file EditorSelection.cpp
/// @brief Implements the Scene panel's editor-local selection resolver, transitions, and rows.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Scene/EditorSelection.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <iterator>
#include <unordered_map>

namespace lmx::app {
namespace {

//======================================================================================================================
std::string toLower(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    std::transform(text.begin(), text.end(), std::back_inserter(result),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

//======================================================================================================================
// `needleLower` is already lowercased; `haystack` is compared case-insensitively without a second
// allocation per row.
bool containsCaseInsensitive(std::string_view haystack, std::string_view needleLower) {
    if (needleLower.empty()) {
        return true;
    }
    const auto it =
        std::search(haystack.begin(), haystack.end(), needleLower.begin(), needleLower.end(),
                    [](unsigned char a, unsigned char b) { return std::tolower(a) == b; });
    return it != haystack.end();
}

//======================================================================================================================
bool matchesRow(const EditorSelectionRow& row, const EditorSelection& selection) {
    if (row.subject != selection.subject) {
        return false;
    }
    if (row.subject == EditorSubject::LocalLight)
        return row.lightId == selection.lightId;
    if (row.subject == EditorSubject::Rendering || row.subject == EditorSubject::DirectionalLight ||
        row.subject == EditorSubject::Object) {
        return row.index == selection.index;
    }
    return true;
}

//======================================================================================================================
std::optional<size_t> visibleRowIndex(std::span<const EditorSelectionRow> rows,
                                      const EditorSelection& selection) {
    for (size_t i = 0; i < rows.size(); ++i) {
        if (matchesRow(rows[i], selection)) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace

//======================================================================================================================
std::string_view renderingCategoryLabel(RenderingCategory category) {
    switch (category) {
    case RenderingCategory::Overview:
        return "Rendering";
    case RenderingCategory::Reconstruction:
        return "Reconstruction";
    case RenderingCategory::Resolution:
        return "Resolution";
    case RenderingCategory::Visibility:
        return "Visibility";
    case RenderingCategory::Occlusion:
        return "Occlusion";
    case RenderingCategory::Submission:
        return "Submission";
    case RenderingCategory::Lighting:
        return "Lighting";
    case RenderingCategory::Exposure:
        return "Exposure";
    case RenderingCategory::Bloom:
        return "Bloom";
    case RenderingCategory::Shadows:
        return "Shadows";
    case RenderingCategory::Display:
        return "Display";
    case RenderingCategory::SceneTables:
        return "Scene tables";
    case RenderingCategory::Count:
        break;
    }
    return "Unavailable";
}

//======================================================================================================================
EditorSelection resolveSelection(const EditorSelection& current, scenes::SceneId activeScene,
                                 const engine::Scene& scene) {
    const EditorSelection healed{
        .sceneId = activeScene, .subject = EditorSubject::None, .index = 0};

    if (current.sceneId != activeScene) {
        return healed;
    }

    switch (current.subject) {
    case EditorSubject::LocalLight:
        if (!scene.light(current.lightId))
            return healed;
        break;
    case EditorSubject::DirectionalLight:
        if (current.index >= std::size(scene.lights)) {
            return healed;
        }
        break;
    case EditorSubject::Object:
        if (current.index >= scene.objects.size()) {
            return healed;
        }
        break;
    case EditorSubject::Rendering:
        if (current.index >= static_cast<size_t>(RenderingCategory::Count)) {
            return healed;
        }
        break;
    case EditorSubject::None:
    case EditorSubject::Camera:
        break;
    }

    return current;
}

//======================================================================================================================
EditorSelection initialSelection(scenes::SceneId sceneId) {
    return EditorSelection{.sceneId = sceneId, .subject = EditorSubject::Camera, .index = 0};
}

//======================================================================================================================
SceneSwitchOutcome sceneSwitchOutcome(bool switchSucceeded, scenes::SceneId activeScene,
                                      scenes::SceneId requestedScene,
                                      const EditorSelection& currentSelection,
                                      const std::string& currentFilter) {
    if (requestedScene == activeScene || !switchSucceeded) {
        return SceneSwitchOutcome{.selection = currentSelection, .filter = currentFilter};
    }
    return SceneSwitchOutcome{.selection = initialSelection(requestedScene),
                              .filter = std::string{}};
}

//======================================================================================================================
std::string sceneLocalLightLabel(const engine::Scene& scene, engine::LightId id) {
    const auto* light = scene.light(id);
    if (!light)
        return "Unavailable light";
    return std::format("{} light {}",
                       light->type == engine::LocalLightType::Point ? "Point" : "Spot", id.slot);
}

//======================================================================================================================
std::string sceneObjectLabel(const engine::Scene& scene, size_t index) {
    if (index >= scene.objects.size()) {
        return "Unavailable object";
    }
    const std::string& name = scene.objects[index].name;
    if (name.empty()) {
        return std::format("Unnamed object [{}]", index);
    }
    const auto count = std::count_if(scene.objects.begin(), scene.objects.end(),
                                     [&](const auto& object) { return object.name == name; });
    return count > 1 ? std::format("{} [object {}]", name, index) : name;
}

//======================================================================================================================
bool selectionHiddenByFilter(const engine::Scene& scene, const EditorSelection& selection,
                             std::string_view filter) {
    if (filter.empty() || selection.subject == EditorSubject::None) {
        return false;
    }
    std::string label;
    std::string compact;
    switch (selection.subject) {
    case EditorSubject::Camera:
        label = "Editor Camera";
        break;
    case EditorSubject::Rendering:
        label = renderingCategoryLabel(static_cast<RenderingCategory>(selection.index));
        compact = "Rendering";
        break;
    case EditorSubject::LocalLight:
        label = sceneLocalLightLabel(scene, selection.lightId);
        break;
    case EditorSubject::DirectionalLight:
        label = std::format("Light {}", selection.index);
        break;
    case EditorSubject::Object:
        label = sceneObjectLabel(scene, selection.index);
        if (selection.index < scene.objects.size()) {
            const auto& object = scene.objects[selection.index];
            compact = object.materialQualifier.empty() ? object.name : object.materialQualifier;
            const auto duplicates = std::count_if(
                scene.objects.begin(), scene.objects.end(), [&](const auto& candidate) {
                    return (candidate.materialQualifier.empty()
                                ? candidate.name
                                : candidate.materialQualifier) == compact;
                });
            if (duplicates > 1) {
                compact = std::format("{} [object {}]", compact, selection.index);
            }
        }
        break;
    case EditorSubject::None:
        return false;
    }
    const std::string needle = toLower(filter);
    return !containsCaseInsensitive(label, needle) && !containsCaseInsensitive(compact, needle);
}

//======================================================================================================================
std::vector<EditorSelectionRow> buildSceneSelectionRows(const engine::Scene& scene,
                                                        std::string_view filter) {
    std::vector<EditorSelectionRow> rows;
    rows.reserve(1 + static_cast<size_t>(RenderingCategory::Count) + std::size(scene.lights) +
                 scene.localLights().size() + scene.objects.size());

    rows.push_back({.subject = EditorSubject::Camera,
                    .index = 0,
                    .displayLabel = "Editor Camera",
                    .group = EditorSelectionGroup::Workspace});
    for (size_t i = 0; i < static_cast<size_t>(RenderingCategory::Count); ++i) {
        rows.push_back(
            {.subject = EditorSubject::Rendering,
             .index = i,
             .displayLabel = std::string(renderingCategoryLabel(static_cast<RenderingCategory>(i))),
             .group = EditorSelectionGroup::Workspace});
    }

    for (size_t i = 0; i < std::size(scene.lights); ++i) {
        rows.push_back({.subject = EditorSubject::DirectionalLight,
                        .index = i,
                        .displayLabel = std::format("Light {}", i),
                        .group = EditorSelectionGroup::DirectionalLights});
    }

    std::unordered_map<std::string_view, size_t> nameCounts;
    std::unordered_map<std::string_view, size_t> displayCounts;
    std::unordered_map<std::string_view, size_t> sourceCounts;
    for (const auto& object : scene.objects) {
        ++nameCounts[object.name];
        if (!object.sourceName.empty() && !object.materialQualifier.empty()) {
            ++sourceCounts[object.sourceName];
        }
        ++displayCounts[object.materialQualifier.empty() ? object.name : object.materialQualifier];
    }
    for (const auto id : scene.localLights()) {
        rows.push_back({.subject = EditorSubject::LocalLight,
                        .index = id.slot,
                        .displayLabel = sceneLocalLightLabel(scene, id),
                        .group = EditorSelectionGroup::LocalLights,
                        .lightId = id});
    }

    for (size_t i = 0; i < scene.objects.size(); ++i) {
        const auto& object = scene.objects[i];
        const std::string& name = object.name;
        const std::string& compact =
            object.materialQualifier.empty() ? name : object.materialQualifier;
        const std::string label = compact.empty() ? std::format("Unnamed object [{}]", i)
                                  : displayCounts[compact] > 1
                                      ? std::format("{} [object {}]", compact, i)
                                      : compact;
        const std::string full = name.empty()           ? std::format("Unnamed object [{}]", i)
                                 : nameCounts[name] > 1 ? std::format("{} [object {}]", name, i)
                                                        : name;
        rows.push_back(
            {.subject = EditorSubject::Object,
             .index = i,
             .displayLabel = label,
             .group = EditorSelectionGroup::Objects,
             .detailLabel = full,
             .sourceGroup = !object.materialQualifier.empty() && sourceCounts[object.sourceName] > 1
                                ? object.sourceName
                                : ""});
    }

    if (filter.empty()) {
        return rows;
    }

    const std::string needleLower = toLower(filter);
    std::erase_if(rows, [&](const EditorSelectionRow& row) {
        if (row.subject == EditorSubject::Rendering &&
            containsCaseInsensitive("Rendering", needleLower)) {
            return false;
        }
        return !containsCaseInsensitive(row.displayLabel, needleLower) &&
               !containsCaseInsensitive(row.detailLabel, needleLower);
    });
    return rows;
}

//======================================================================================================================
std::vector<EditorObjectGroup> groupSceneObjectRows(std::span<const EditorSelectionRow> rows) {
    std::vector<EditorObjectGroup> groups;
    std::unordered_map<std::string_view, size_t> groupIndices;
    for (const auto& row : rows) {
        if (row.subject != EditorSubject::Object) {
            continue;
        }
        const auto [entry, inserted] = groupIndices.try_emplace(row.sourceGroup, groups.size());
        if (inserted) {
            groups.push_back({.sourceName = row.sourceGroup, .rows = {}});
        }
        groups[entry->second].rows.push_back(row);
    }
    return groups;
}

//======================================================================================================================
std::optional<EditorSelectionRow> nextVisibleRow(std::span<const EditorSelectionRow> rows,
                                                 const EditorSelection& current) {
    if (rows.empty()) {
        return std::nullopt;
    }
    const std::optional<size_t> at = visibleRowIndex(rows, current);
    if (!at) {
        return rows.front();
    }
    const size_t next = std::min(*at + 1, rows.size() - 1);
    return rows[next];
}

//======================================================================================================================
std::optional<EditorSelectionRow> previousVisibleRow(std::span<const EditorSelectionRow> rows,
                                                     const EditorSelection& current) {
    if (rows.empty()) {
        return std::nullopt;
    }
    const std::optional<size_t> at = visibleRowIndex(rows, current);
    if (!at) {
        return rows.front();
    }
    const size_t previous = (*at == 0) ? 0 : *at - 1;
    return rows[previous];
}

} // namespace lmx::app
