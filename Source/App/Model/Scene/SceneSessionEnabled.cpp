//----------------------------------------------------------------------------------------------------------------------
/// @file SceneSessionEnabled.cpp
/// @brief Applies own enabled flags through document and imported hierarchies.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/Scene/SceneSession.h"

#include "App/Model/Scene/EditorSelection.h"
#include "Core/Diagnostics/Assert.h"

#include <algorithm>

namespace lmx::app {
namespace {
//======================================================================================================================
rojoRHI::Result<void> unavailableEnabledEdit() {
    return std::unexpected(
        rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc,
                       "Enabled edits require a valid subject and no active measurement"});
}
} // namespace

//======================================================================================================================
std::vector<bool> SceneSession::effectiveNodes() const {
    auto own = documentState().nodeEnabled;
    const auto& defaults = m_defaults.at(m_scene);
    if (defaults.rigOverride && m_loaded->binding.localLightGroup)
        own[*m_loaded->binding.localLightGroup] = *defaults.rigOverride;
    return engine::effectiveDocumentEnabled(m_loaded->document, own);
}

//======================================================================================================================
std::vector<bool> SceneSession::effectiveImported(const std::vector<bool>& nodes) const {
    const auto& imported = m_loaded->binding.importedNodes;
    const auto& own = documentState().importedEnabled;
    std::vector<bool> effective(imported.size());
    for (size_t i = 0; i < imported.size(); ++i) {
        bool value = nodes.at(imported[i].assetRoot) && own.at(i);
        int32_t parent = imported[i].parent;
        while (value && parent >= 0) {
            const auto found = std::ranges::find_if(imported, [&](const auto& candidate) {
                return candidate.assetRoot == imported[i].assetRoot &&
                       candidate.sourceNode == static_cast<uint32_t>(parent);
            });
            LMX_ASSERT(found != imported.end(), "imported parent requires a retained binding");
            value = own.at(static_cast<size_t>(found - imported.begin()));
            parent = found->parent;
        }
        effective[i] = value;
    }
    return effective;
}

//======================================================================================================================
bool SceneSession::nodeEnabled(uint32_t node) const {
    return documentState().nodeEnabled.at(node);
}

//======================================================================================================================
bool SceneSession::nodeEffectiveEnabled(uint32_t node) const {
    return effectiveNodes().at(node);
}

//======================================================================================================================
bool SceneSession::importedNodeEnabled(uint32_t imported) const {
    return documentState().importedEnabled.at(imported);
}

//======================================================================================================================
bool SceneSession::importedNodeEffectiveEnabled(uint32_t imported) const {
    return effectiveImported(effectiveNodes()).at(imported);
}

//======================================================================================================================
bool SceneSession::objectEnabled(size_t index) const {
    if (m_loaded) {
        const auto imported = m_loaded->binding.objectImportedNode.at(index);
        if (imported != engine::kGeneratedNode)
            return importedNodeEnabled(imported);
    }
    return m_defaults.at(m_scene).objectOwnEnabled.at(index);
}

//======================================================================================================================
bool SceneSession::localLightEnabled(engine::LightId id) const {
    if (!scene().light(id))
        return false;
    const auto key = engine::sceneLightKey(id);
    if (m_loaded) {
        const auto node = m_loaded->binding.lightNode.find(key);
        if (node != m_loaded->binding.lightNode.end())
            return nodeEnabled(node->second);
    }
    const auto& own = m_defaults.at(m_scene).lightOwnEnabled;
    const auto found = own.find(key);
    return found == own.end() ? scene().light(id)->enabled : found->second;
}

//======================================================================================================================
bool SceneSession::isGenerated(EditorSubject subject, size_t index, engine::LightId id) const {
    if (!m_loaded)
        return false;
    if (subject == EditorSubject::Object)
        return index < m_loaded->binding.objectGeneratorNode.size() &&
               m_loaded->binding.objectGeneratorNode[index] != engine::kGeneratedNode;
    return subject == EditorSubject::LocalLight && scene().light(id) &&
           m_loaded->binding.lightGeneratorNode.contains(engine::sceneLightKey(id));
}

//======================================================================================================================
void SceneSession::applyEnabled() {
    auto& defaults = m_defaults.at(m_scene);
    const auto nodes = m_loaded ? effectiveNodes() : std::vector<bool>{};
    const auto imported = m_loaded ? effectiveImported(nodes) : std::vector<bool>{};
    for (size_t i = 0; i < scene().objects.size(); ++i) {
        bool enabled = defaults.objectOwnEnabled.at(i);
        if (m_loaded) {
            const auto source = m_loaded->binding.objectImportedNode.at(i);
            const auto generator = m_loaded->binding.objectGeneratorNode.at(i);
            if (source != engine::kGeneratedNode)
                enabled = imported.at(source);
            else if (generator != engine::kGeneratedNode)
                enabled = enabled && nodes.at(generator);
        }
        scene().setObjectEnabled(i, enabled);
    }
    for (auto id : scene().localLights()) {
        auto light = *scene().light(id);
        const auto key = engine::sceneLightKey(id);
        light.enabled = localLightEnabled(id);
        if (m_loaded) {
            const auto bound = m_loaded->binding.lightNode.find(key);
            const auto generated = m_loaded->binding.lightGeneratorNode.find(key);
            if (bound != m_loaded->binding.lightNode.end())
                light.enabled = nodes.at(bound->second);
            else if (generated != m_loaded->binding.lightGeneratorNode.end())
                light.enabled = light.enabled && nodes.at(generated->second);
        }
        const auto result = scene().updateLight(id, light);
        LMX_ASSERT(result.has_value(), "valid live light permits enabled-only updates");
    }
    if (m_loaded)
        for (size_t n = 0; n < m_loaded->binding.nodes.size(); ++n)
            if (const auto slot = m_loaded->binding.nodes[n].directional)
                scene().lights[*slot].enabled = nodes.at(n);
}

//======================================================================================================================
rojoRHI::Result<void> SceneSession::setNodeEnabled(uint32_t node, bool enabled) {
    if (m_measurementActive || !m_loaded || node >= documentState().nodeEnabled.size())
        return unavailableEnabledEdit();
    auto& own = m_documentStates.at(m_scene).nodeEnabled;
    auto& override = m_defaults.at(m_scene).rigOverride;
    const bool clearOverride = m_loaded->binding.localLightGroup == node && override.has_value();
    if (own[node] == enabled && !clearOverride)
        return {};
    own[node] = enabled;
    if (clearOverride)
        override.reset();
    applyEnabled();
    notifyPersistentEdit();
    m_temporalResetPending = true;
    return {};
}

//======================================================================================================================
rojoRHI::Result<void> SceneSession::setImportedNodeEnabled(uint32_t imported, bool enabled) {
    if (m_measurementActive || !m_loaded || imported >= documentState().importedEnabled.size())
        return unavailableEnabledEdit();
    auto& own = m_documentStates.at(m_scene).importedEnabled;
    if (own[imported] == enabled)
        return {};
    own[imported] = enabled;
    applyEnabled();
    notifyPersistentEdit();
    m_temporalResetPending = true;
    return {};
}

//======================================================================================================================
rojoRHI::Result<void> SceneSession::setObjectEnabled(size_t index, bool enabled) {
    if (m_measurementActive || index >= scene().objects.size())
        return unavailableEnabledEdit();
    if (m_loaded) {
        const auto imported = m_loaded->binding.objectImportedNode.at(index);
        if (imported != engine::kGeneratedNode)
            return setImportedNodeEnabled(imported, enabled);
    }
    auto& own = m_defaults.at(m_scene).objectOwnEnabled;
    if (own.at(index) == enabled)
        return {};
    own[index] = enabled;
    applyEnabled();
    if (!isGenerated(EditorSubject::Object, index, {}))
        notifyPersistentEdit();
    m_temporalResetPending = true;
    return {};
}

//======================================================================================================================
rojoRHI::Result<void> SceneSession::setLocalLightEnabled(engine::LightId id, bool enabled) {
    if (m_measurementActive || !scene().light(id))
        return unavailableEnabledEdit();
    const auto key = engine::sceneLightKey(id);
    if (m_loaded) {
        const auto node = m_loaded->binding.lightNode.find(key);
        if (node != m_loaded->binding.lightNode.end())
            return setNodeEnabled(node->second, enabled);
    }
    if (localLightEnabled(id) == enabled)
        return {};
    m_defaults.at(m_scene).lightOwnEnabled[key] = enabled;
    applyEnabled();
    if (!isGenerated(EditorSubject::LocalLight, 0, id))
        notifyPersistentEdit();
    m_temporalResetPending = true;
    return {};
}

//======================================================================================================================
rojoRHI::Result<void> SceneSession::setLocalLightRig(bool enabled) {
    if (m_measurementActive)
        return unavailableEnabledEdit();
    if (!localLightRigAvailable())
        return {};
    auto& override = m_defaults.at(m_scene).rigOverride;
    if (override == enabled)
        return {};
    override = enabled;
    applyEnabled();
    m_temporalResetPending = true;
    return {};
}

//======================================================================================================================
bool SceneSession::consumeTemporalReset() {
    const bool reset = m_temporalResetPending;
    m_temporalResetPending = false;
    return reset;
}

} // namespace lmx::app
