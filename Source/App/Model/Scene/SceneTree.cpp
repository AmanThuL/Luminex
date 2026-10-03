//----------------------------------------------------------------------------------------------------------------------
/// @file SceneTree.cpp
/// @brief Builds searchable document and imported-source rows for the editor hierarchy.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Scene/SceneTree.h"

#include "App/Model/Scene/SceneSession.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <functional>
#include <unordered_map>

namespace lmx::app {
namespace {

struct Branch {
    SceneTreeRow row;
    std::vector<size_t> children;
};

//======================================================================================================================
bool matches(std::string_view label, std::string_view filter) {
    if (filter.empty())
        return true;
    return std::search(label.begin(), label.end(), filter.begin(), filter.end(),
                       [](unsigned char a, unsigned char b) {
                           return std::tolower(a) == std::tolower(b);
                       }) != label.end();
}

//======================================================================================================================
std::string importedLabel(const engine::ImportedNodeBinding& source) {
    std::string label =
        source.name.empty() ? std::format("Source node {}", source.sourceNode) : source.name;
    if (source.objects.size() > 1)
        label += std::format(" ({} primitives)", source.objects.size());
    return label;
}

//======================================================================================================================
std::string generatedObjectLabel(const engine::Scene& scene, size_t index,
                                 const std::unordered_map<std::string_view, size_t>& nameCounts) {
    const std::string& name = scene.objects[index].name;
    if (name.empty())
        return std::format("Unnamed object [{}]", index);
    return nameCounts.at(name) > 1 ? std::format("{} [object {}]", name, index) : name;
}

} // namespace

//======================================================================================================================
uint32_t sceneTreeImportedKey(uint32_t importedNode) {
    return 0x80000000u | importedNode;
}

//======================================================================================================================
std::string documentNodeLabel(std::string_view name, uint32_t node) {
    return name.empty() ? std::format("Node {}", node) : std::string(name);
}

//======================================================================================================================
std::string documentTitle(std::string_view name, bool dirty) {
    return std::string(name) + (dirty ? "*" : "");
}

//======================================================================================================================
std::span<const SceneTreeRow> sceneTreeVisibleRows(std::span<const SceneTreeRow> rows,
                                                   bool rootCollapsed, std::string_view filter) {
    return rootCollapsed && filter.empty() ? rows.first(std::min<size_t>(1, rows.size())) : rows;
}

//======================================================================================================================
SceneTreeView buildSceneTreeView(const engine::LoadedScene& loaded,
                                 const scenes::SessionDocumentState& state, std::string_view filter,
                                 const std::set<uint32_t>& collapsed, const SceneSession* session) {
    const auto& document = loaded.document;
    const auto& binding = loaded.binding;
    const auto& scene = *loaded.scene;
    std::unordered_map<std::string_view, size_t> nameCounts;
    nameCounts.reserve(scene.objects.size());
    for (const auto& object : scene.objects)
        ++nameCounts[object.name];
    std::vector<Branch> branches;
    branches.reserve(document.nodes.size() + binding.importedNodes.size() + scene.objects.size() +
                     scene.localLights().size() + 2);
    branches.push_back({.row = {.subject = EditorSubject::Group,
                                .label = document.name.empty() ? scene.name : document.name,
                                .group = true}});

    auto add = [&](size_t parent, SceneTreeRow row) {
        const size_t index = branches.size();
        branches.push_back({.row = std::move(row)});
        branches[parent].children.push_back(index);
        branches[parent].row.group = true;
        return index;
    };

    // Generator children are grouped once so each generator node reads only its own population.
    std::unordered_map<uint32_t, std::vector<size_t>> objectsByGenerator;
    for (size_t object = 0; object < binding.objectGeneratorNode.size(); ++object)
        if (binding.objectGeneratorNode[object] != engine::kGeneratedNode)
            objectsByGenerator[binding.objectGeneratorNode[object]].push_back(object);
    std::unordered_map<uint32_t, std::vector<engine::LightId>> lightsByGenerator;
    for (const auto lightId : scene.localLights())
        if (const auto found = binding.lightGeneratorNode.find(engine::sceneLightKey(lightId));
            found != binding.lightGeneratorNode.end())
            lightsByGenerator[found->second].push_back(lightId);

    std::vector<size_t> objectsByNode(document.nodes.size(), scene.objects.size());
    for (size_t object = 0; object < binding.objectNode.size(); ++object) {
        const uint32_t node = binding.objectNode[object];
        if (node < document.nodes.size() && document.nodes[node].mesh)
            objectsByNode[node] = object;
    }

    const auto docEffective = engine::effectiveDocumentEnabled(document, state.nodeEnabled);
    std::vector<bool> importedEffective(binding.importedNodes.size(), true);
    std::unordered_map<uint64_t, uint32_t> importedBySource;
    importedBySource.reserve(binding.importedNodes.size());
    for (uint32_t i = 0; i < binding.importedNodes.size(); ++i) {
        const auto& source = binding.importedNodes[i];
        importedBySource.emplace((uint64_t(source.assetRoot) << 32) | source.sourceNode, i);
    }
    std::vector<std::vector<uint32_t>> importedChildren(binding.importedNodes.size());
    std::vector<std::vector<uint32_t>> importedRoots(document.nodes.size());
    for (uint32_t i = 0; i < binding.importedNodes.size(); ++i) {
        const auto& source = binding.importedNodes[i];
        if (source.parent < 0) {
            importedRoots[source.assetRoot].push_back(i);
        } else {
            const auto key =
                (uint64_t(source.assetRoot) << 32) | static_cast<uint32_t>(source.parent);
            importedChildren[importedBySource.at(key)].push_back(i);
        }
    }

    std::function<void(size_t, uint32_t, uint32_t)> appendImported =
        [&](size_t parent, uint32_t sourceIndex, uint32_t depth) {
            const auto& source = binding.importedNodes[sourceIndex];
            const bool own = session ? session->importedNodeEnabled(sourceIndex)
                                     : state.importedEnabled[sourceIndex];
            const bool parentEffective =
                source.parent < 0
                    ? (session ? session->nodeEffectiveEnabled(source.assetRoot)
                               : docEffective[source.assetRoot])
                    : importedEffective[importedBySource.at((uint64_t(source.assetRoot) << 32) |
                                                            static_cast<uint32_t>(source.parent))];
            const bool effective = own && parentEffective;
            importedEffective[sourceIndex] = effective;
            const bool hasObjects = !source.objects.empty();
            const size_t rowIndex =
                add(parent, {.subject = hasObjects ? EditorSubject::Object : EditorSubject::Group,
                             .index = hasObjects ? source.objects.front() : 0,
                             .node = source.assetRoot,
                             .importedNode = sourceIndex,
                             .depth = depth,
                             .label = importedLabel(source),
                             .enabled = own,
                             .effective = effective});
            for (const uint32_t child : importedChildren[sourceIndex])
                appendImported(rowIndex, child, depth + 1);
        };

    std::function<void(size_t, uint32_t, uint32_t)> appendDocument = [&](size_t parent,
                                                                         uint32_t nodeIndex,
                                                                         uint32_t depth) {
        const auto& node = document.nodes[nodeIndex];
        const auto& bound = binding.nodes[nodeIndex];
        EditorSubject subject = EditorSubject::Group;
        size_t index = 0;
        engine::LightId lightId{};
        if (bound.camera)
            subject = EditorSubject::Camera;
        else if (bound.directional) {
            subject = EditorSubject::DirectionalLight;
            index = *bound.directional;
        } else if (bound.light) {
            subject = EditorSubject::LocalLight;
            lightId = *bound.light;
        } else if (node.mesh && objectsByNode[nodeIndex] < scene.objects.size()) {
            subject = EditorSubject::Object;
            index = objectsByNode[nodeIndex];
        }
        const bool own = session ? session->nodeEnabled(nodeIndex) : state.nodeEnabled[nodeIndex];
        const bool effective =
            session ? session->nodeEffectiveEnabled(nodeIndex) : docEffective[nodeIndex];
        const size_t rowIndex = add(parent, {.subject = subject,
                                             .index = index,
                                             .lightId = lightId,
                                             .node = nodeIndex,
                                             .depth = depth,
                                             .label = documentNodeLabel(node.name, nodeIndex),
                                             .group = node.generator.has_value(),
                                             .enabled = own,
                                             .effective = effective});
        for (const uint32_t child : node.children)
            appendDocument(rowIndex, child, depth + 1);
        for (const uint32_t source : importedRoots[nodeIndex])
            appendImported(rowIndex, source, depth + 1);
        if (node.generator) {
            for (const size_t object : objectsByGenerator[nodeIndex]) {
                const bool objectOwn = session ? session->objectEnabled(object)
                                               : binding.generatedObjectEnabled[object];
                add(rowIndex, {.subject = EditorSubject::Object,
                               .index = object,
                               .node = nodeIndex,
                               .depth = depth + 1,
                               .label = generatedObjectLabel(scene, object, nameCounts),
                               .generated = true,
                               .enabled = objectOwn,
                               .effective = scene.objects[object].enabled});
            }
            for (const auto lightId : lightsByGenerator[nodeIndex]) {
                const bool lightOwn =
                    session ? session->localLightEnabled(lightId)
                            : binding.generatedLightEnabled.at(engine::sceneLightKey(lightId));
                add(rowIndex, {.subject = EditorSubject::LocalLight,
                               .lightId = lightId,
                               .node = nodeIndex,
                               .depth = depth + 1,
                               .label = sceneLocalLightLabel(scene, lightId),
                               .generated = true,
                               .enabled = lightOwn,
                               .effective = scene.light(lightId)->enabled});
            }
        }
    };

    for (const uint32_t root : document.rootNodes)
        appendDocument(0, root, 1);
    add(0, {.subject = EditorSubject::Environment, .depth = 1, .label = "Environment"});
    const size_t totalCount = branches.size() - 1;

    std::vector<bool> retained(branches.size());
    std::function<bool(size_t)> retain = [&](size_t index) {
        bool child = false;
        for (const size_t item : branches[index].children)
            child = retain(item) || child;
        retained[index] = index == 0 || matches(branches[index].row.label, filter) || child;
        return retained[index];
    };
    retain(0);
    const size_t matchedCount =
        static_cast<size_t>(std::count(retained.begin(), retained.end(), true)) - 1;

    std::vector<SceneTreeRow> rows;
    rows.reserve(branches.size());
    std::function<void(size_t)> flatten = [&](size_t index) {
        if (!retained[index])
            return;
        rows.push_back(branches[index].row);
        const auto& row = branches[index].row;
        const uint32_t key = row.importedNode == engine::kGeneratedNode
                                 ? row.node
                                 : sceneTreeImportedKey(row.importedNode);
        if (filter.empty() && index != 0 && collapsed.contains(key))
            return;
        for (const size_t child : branches[index].children)
            flatten(child);
    };
    flatten(0);
    return {.rows = std::move(rows), .matchedCount = matchedCount, .totalCount = totalCount};
}

//======================================================================================================================
std::vector<SceneTreeRow> buildSceneTree(const engine::LoadedScene& loaded,
                                         const scenes::SessionDocumentState& state,
                                         std::string_view filter,
                                         const std::set<uint32_t>& collapsed,
                                         const SceneSession* session) {
    return buildSceneTreeView(loaded, state, filter, collapsed, session).rows;
}

//======================================================================================================================
size_t sceneTreeSubjectCount(std::span<const SceneTreeRow> rows) {
    return rows.empty() ? 0 : rows.size() - 1;
}

//======================================================================================================================
bool sceneTreeRowSelected(const SceneTreeRow& row, const EditorSelection& selection) {
    if (row.subject != selection.subject)
        return false;
    if (row.subject == EditorSubject::LocalLight && row.lightId != selection.lightId)
        return false;
    if ((row.subject == EditorSubject::Object || row.subject == EditorSubject::DirectionalLight) &&
        row.index != selection.index)
        return false;
    if (selection.importedNode != engine::kGeneratedNode || row.subject == EditorSubject::Group)
        return row.importedNode == selection.importedNode && row.node == selection.node;
    return selection.node == engine::kGeneratedNode || row.node == selection.node;
}

//======================================================================================================================
bool sceneTreeSelectionHidden(std::span<const SceneTreeRow> rows, const EditorSelection& selection,
                              std::string_view filter) {
    if (filter.empty() || selection.subject == EditorSubject::None ||
        selection.subject == EditorSubject::Camera ||
        selection.subject == EditorSubject::Environment)
        return false;
    return std::ranges::none_of(
        rows, [&](const auto& row) { return sceneTreeRowSelected(row, selection); });
}

//======================================================================================================================
std::optional<SceneTreeRow> sceneTreeKeyboardTarget(std::span<const SceneTreeRow> rows,
                                                    const EditorSelection& selection, bool down) {
    if (rows.empty())
        return std::nullopt;
    const auto found = std::ranges::find_if(
        rows, [&](const auto& row) { return sceneTreeRowSelected(row, selection); });
    if (found == rows.end())
        return rows.front();
    const size_t current = static_cast<size_t>(found - rows.begin());
    const size_t next =
        down ? std::min(current + 1, rows.size() - 1) : (current == 0 ? 0 : current - 1);
    return rows[next];
}

//======================================================================================================================
bool sceneTreeRootOpen(bool collapsed, std::string_view filter) {
    return !collapsed || !filter.empty();
}

//======================================================================================================================
bool sceneTreeRootCollapsedAfterDraw(bool collapsed, bool opened, std::string_view filter) {
    return filter.empty() ? !opened : collapsed;
}

} // namespace lmx::app
