//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorSubject.cpp
/// @brief Resolves Inspector ownership and routes enabled edits through SceneSession.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Scene/InspectorSubject.h"
#include "App/Model/Rendering/Settings/EditorRenderDefaults.h"
#include "App/Model/Scene/SceneTree.h"

#include <algorithm>
#include <format>

namespace lmx::app {
namespace {

//======================================================================================================================
rojoRHI::Result<void> staleSubject() {
    return std::unexpected(
        rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc, "Selected subject is no longer available"});
}

//======================================================================================================================
std::string generatedBy(const engine::LoadedScene& loaded, uint32_t node) {
    return node < loaded.document.nodes.size() && loaded.document.nodes[node].generator
               ? loaded.document.nodes[node].generator->name
               : std::string{};
}

//======================================================================================================================
std::string importedName(const engine::ImportedNodeBinding& source) {
    return source.name.empty() ? std::format("Source node {}", source.sourceNode) : source.name;
}

//======================================================================================================================
bool importedBaseline(const engine::LoadedScene& loaded,
                      const engine::ImportedNodeBinding& source) {
    const auto& overrides = loaded.document.nodes.at(source.assetRoot).overrides;
    const auto found = std::ranges::find(overrides, source.sourceNode, &asset::DocOverride::node);
    return found == overrides.end() ? source.enabled : found->enabled.value_or(source.enabled);
}

} // namespace

//======================================================================================================================
std::optional<bool> inspectorIsStatic(const engine::LoadedScene& loaded,
                                      const EditorSelection& selection) {
    const auto& binding = loaded.binding;
    if (selection.subject == EditorSubject::Object) {
        const auto index = selection.index;
        if (index >= loaded.scene->objects.size() || index >= binding.objectNode.size() ||
            binding.objectNode[index] >= loaded.document.nodes.size() ||
            (index < binding.objectGeneratorNode.size() &&
             binding.objectGeneratorNode[index] != engine::kGeneratedNode))
            return std::nullopt;
        return index >= loaded.objectMobility.size() ||
               loaded.objectMobility[index] == asset::DocMobility::Static;
    }
    if (selection.subject != EditorSubject::DirectionalLight &&
        selection.subject != EditorSubject::LocalLight)
        return std::nullopt;
    size_t mobility = 0;
    for (const auto& node : binding.nodes) {
        if (!node.directional && !node.light)
            continue;
        if (selection.subject == EditorSubject::DirectionalLight
                ? node.directional == selection.index
                : node.light == selection.lightId)
            return mobility >= loaded.lightMobility.size() ||
                   loaded.lightMobility[mobility] == asset::DocMobility::Static;
        ++mobility;
    }
    return std::nullopt;
}

//======================================================================================================================
std::optional<bool> inspectorIsStatic(const SceneSession& session,
                                      const EditorSelection& selection) {
    return session.loadedScene() ? inspectorIsStatic(*session.loadedScene(), selection)
                                 : std::nullopt;
}

//======================================================================================================================
std::optional<InspectorEnabledState> inspectorEnabledState(const SceneSession& session,
                                                           const EditorSelection& selection) {
    const auto* loaded = session.loadedScene();
    if (!loaded)
        return std::nullopt;
    const auto& binding = loaded->binding;
    const auto& document = loaded->document;
    if (selection.subject == EditorSubject::Group) {
        if (selection.importedNode != engine::kGeneratedNode) {
            if (selection.importedNode >= binding.importedNodes.size())
                return std::nullopt;
            const auto& source = binding.importedNodes[selection.importedNode];
            return InspectorEnabledState{
                .label = importedName(source),
                .kind = "Source group",
                .own = session.importedNodeEnabled(selection.importedNode),
                .effective = session.importedNodeEffectiveEnabled(selection.importedNode),
                .baseline = importedBaseline(*loaded, source),
                .primitiveCount = source.objects.size()};
        }
        if (selection.node >= document.nodes.size())
            return std::nullopt;
        const auto& node = document.nodes[selection.node];
        return InspectorEnabledState{.label = documentNodeLabel(node.name, selection.node),
                                     .kind = node.generator ? "Generator"
                                             : node.asset   ? "Asset"
                                                            : "Group",
                                     .own = session.nodeEnabled(selection.node),
                                     .effective = session.nodeEffectiveEnabled(selection.node),
                                     .baseline = node.enabled};
    }
    if (selection.subject == EditorSubject::Object) {
        if (selection.index >= session.scene().objects.size())
            return std::nullopt;
        const uint32_t imported = binding.objectImportedNode.at(selection.index);
        if (imported != engine::kGeneratedNode) {
            const auto& source = binding.importedNodes.at(imported);
            return InspectorEnabledState{.label = importedName(source),
                                         .kind = "Source object",
                                         .own = session.objectEnabled(selection.index),
                                         .effective =
                                             session.importedNodeEffectiveEnabled(imported),
                                         .baseline = importedBaseline(*loaded, source),
                                         .primitiveCount = source.objects.size()};
        }
        const uint32_t generator = binding.objectGeneratorNode.at(selection.index);
        const uint32_t node = binding.objectNode.at(selection.index);
        return InspectorEnabledState{.label = sceneObjectLabel(session.scene(), selection.index),
                                     .kind = "Object",
                                     .generatedBy = generatedBy(*loaded, generator),
                                     .own = session.objectEnabled(selection.index),
                                     .effective = session.scene().objects[selection.index].enabled,
                                     .baseline =
                                         node < document.nodes.size() && document.nodes[node].mesh
                                             ? document.nodes[node].enabled
                                             : binding.generatedObjectEnabled.at(selection.index)};
    }
    if (selection.subject == EditorSubject::DirectionalLight) {
        if (selection.index >= std::size(session.scene().lights))
            return std::nullopt;
        uint32_t node = selection.node;
        if (node >= binding.nodes.size() || binding.nodes[node].directional != selection.index) {
            node = engine::kGeneratedNode;
            for (uint32_t n = 0; n < binding.nodes.size(); ++n)
                if (binding.nodes[n].directional == selection.index)
                    node = n;
        }
        if (node >= document.nodes.size())
            return std::nullopt;
        return InspectorEnabledState{.label = documentNodeLabel(document.nodes[node].name, node),
                                     .kind = "Directional",
                                     .own = session.nodeEnabled(node),
                                     .effective = session.scene().lights[selection.index].enabled,
                                     .baseline = document.nodes[node].enabled};
    }
    if (selection.subject == EditorSubject::LocalLight) {
        const auto* light = session.scene().light(selection.lightId);
        if (!light)
            return std::nullopt;
        const auto key = engine::sceneLightKey(selection.lightId);
        if (const auto found = binding.lightNode.find(key); found != binding.lightNode.end()) {
            const uint32_t node = found->second;
            return InspectorEnabledState{
                .label = documentNodeLabel(document.nodes[node].name, node),
                .kind = light->type == engine::LocalLightType::Point ? "Point" : "Spot",
                .own = session.localLightEnabled(selection.lightId),
                .effective = light->enabled,
                .baseline = document.nodes[node].enabled};
        }
        const auto generator = binding.lightGeneratorNode.find(key);
        return InspectorEnabledState{
            .label = sceneLocalLightLabel(session.scene(), selection.lightId),
            .kind = light->type == engine::LocalLightType::Point ? "Point" : "Spot",
            .generatedBy = generator == binding.lightGeneratorNode.end()
                               ? std::string{}
                               : generatedBy(*loaded, generator->second),
            .own = session.localLightEnabled(selection.lightId),
            .effective = light->enabled,
            .baseline = binding.generatedLightEnabled.contains(key)
                            ? binding.generatedLightEnabled.at(key)
                            : true};
    }
    return std::nullopt;
}

//======================================================================================================================
bool inspectorSubjectEdited(const SceneSession& session, const EditorSelection& selection) {
    const auto enabled = inspectorEnabledState(session, selection);
    const bool flagChanged = enabled && enabled->own != enabled->baseline;
    switch (selection.subject) {
    case EditorSubject::Object:
        return flagChanged || session.objectChanged(selection.index);
    case EditorSubject::DirectionalLight:
        return flagChanged || session.lightChanged(selection.index);
    case EditorSubject::LocalLight:
        return flagChanged || session.localLightChanged(selection.lightId);
    case EditorSubject::Environment:
        return sceneLookChanged(session);
    case EditorSubject::Group:
        return flagChanged;
    case EditorSubject::Camera: {
        const auto* loaded = session.loadedScene();
        if (!loaded)
            return false;
        const auto& document = loaded->document;
        const uint32_t node =
            selection.node < document.nodes.size() && document.nodes[selection.node].camera
                ? selection.node
                : document.camera;
        const bool ownChanged = node < document.nodes.size() &&
                                session.nodeEnabled(node) != document.nodes[node].enabled;
        const auto current = session.authoredSceneCamera();
        const auto& original = session.scene().initialCamera;
        return ownChanged || current.position != original.position || current.yaw != original.yaw ||
               current.pitch != original.pitch || current.fovY != original.fovY ||
               current.nearZ != original.nearZ || current.farZ != original.farZ;
    }
    case EditorSubject::None:
        return false;
    }
    return false;
}

//======================================================================================================================
rojoRHI::Result<void> setInspectorEnabled(SceneSession& session, const EditorSelection& selection,
                                          bool enabled) {
    if (!inspectorEnabledState(session, selection))
        return staleSubject();
    switch (selection.subject) {
    case EditorSubject::Group:
        return selection.importedNode == engine::kGeneratedNode
                   ? session.setNodeEnabled(selection.node, enabled)
                   : session.setImportedNodeEnabled(selection.importedNode, enabled);
    case EditorSubject::Object:
        return session.setObjectEnabled(selection.index, enabled);
    case EditorSubject::DirectionalLight: {
        const auto* loaded = session.loadedScene();
        for (uint32_t n = 0; n < loaded->binding.nodes.size(); ++n)
            if (loaded->binding.nodes[n].directional == selection.index)
                return session.setNodeEnabled(n, enabled);
        return staleSubject();
    }
    case EditorSubject::LocalLight:
        return session.setLocalLightEnabled(selection.lightId, enabled);
    case EditorSubject::None:
    case EditorSubject::Camera:
    case EditorSubject::Environment:
        break;
    }
    return staleSubject();
}

//======================================================================================================================
rojoRHI::Result<void> resetInspectorEnabled(SceneSession& session,
                                            const EditorSelection& selection) {
    const auto state = inspectorEnabledState(session, selection);
    return state ? setInspectorEnabled(session, selection, state->baseline) : staleSubject();
}

} // namespace lmx::app
