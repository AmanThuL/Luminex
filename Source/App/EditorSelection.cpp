//----------------------------------------------------------------------------------------------------------------------
/// @file EditorSelection.cpp
/// @brief Implements the Scene panel's editor-local selection resolver, transitions, and rows.
//----------------------------------------------------------------------------------------------------------------------

#include "App/EditorSelection.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <iterator>

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
// `index` is meaningful only for DirectionalLight/Object rows; other subjects match on kind alone
// so a Camera/Rendering/None selection is never rejected over an index it does not use.
bool matchesRow(const EditorSelectionRow& row, const EditorSelection& selection) {
    if (row.subject != selection.subject) {
        return false;
    }
    if (row.subject == EditorSubject::DirectionalLight || row.subject == EditorSubject::Object) {
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
EditorSelection resolveSelection(const EditorSelection& current, engine::SceneId activeScene,
                                 const engine::Scene& scene) {
    const EditorSelection healed{
        .sceneId = activeScene, .subject = EditorSubject::None, .index = 0};

    if (current.sceneId != activeScene) {
        return healed;
    }

    switch (current.subject) {
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
    case EditorSubject::None:
    case EditorSubject::Camera:
    case EditorSubject::Rendering:
        break;
    }

    return current;
}

//======================================================================================================================
EditorSelection initialSelection(engine::SceneId sceneId) {
    return EditorSelection{.sceneId = sceneId, .subject = EditorSubject::Camera, .index = 0};
}

//======================================================================================================================
SceneSwitchOutcome sceneSwitchOutcome(bool switchSucceeded, engine::SceneId activeScene,
                                      engine::SceneId requestedScene,
                                      const EditorSelection& currentSelection,
                                      const std::string& currentFilter) {
    if (requestedScene == activeScene || !switchSucceeded) {
        return SceneSwitchOutcome{.selection = currentSelection, .filter = currentFilter};
    }
    return SceneSwitchOutcome{.selection = initialSelection(requestedScene),
                              .filter = std::string{}};
}

//======================================================================================================================
std::vector<EditorSelectionRow> buildSceneSelectionRows(const engine::Scene& scene,
                                                        std::string_view filter) {
    std::vector<EditorSelectionRow> rows;
    rows.reserve(2 + std::size(scene.lights) + scene.objects.size());

    rows.push_back({.subject = EditorSubject::Camera,
                    .index = 0,
                    .displayLabel = "Editor Camera",
                    .group = EditorSelectionGroup::Workspace});
    rows.push_back({.subject = EditorSubject::Rendering,
                    .index = 0,
                    .displayLabel = "Rendering",
                    .group = EditorSelectionGroup::Workspace});

    for (size_t i = 0; i < std::size(scene.lights); ++i) {
        rows.push_back({.subject = EditorSubject::DirectionalLight,
                        .index = i,
                        .displayLabel = std::format("Light {}", i),
                        .group = EditorSelectionGroup::DirectionalLights});
    }

    for (size_t i = 0; i < scene.objects.size(); ++i) {
        rows.push_back({.subject = EditorSubject::Object,
                        .index = i,
                        .displayLabel = scene.objects[i].name,
                        .group = EditorSelectionGroup::Objects});
    }

    if (filter.empty()) {
        return rows;
    }

    const std::string needleLower = toLower(filter);
    std::erase_if(rows, [&](const EditorSelectionRow& row) {
        return !containsCaseInsensitive(row.displayLabel, needleLower);
    });
    return rows;
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
