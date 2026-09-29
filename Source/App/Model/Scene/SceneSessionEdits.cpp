//----------------------------------------------------------------------------------------------------------------------
/// @file SceneSessionEdits.cpp
/// @brief Tracks persistent subject edits and saved reset baselines.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/Scene/SceneSession.h"

#include "App/Model/Scene/EditorSelection.h"
#include "Core/Diagnostics/Assert.h"

#include <algorithm>

namespace lmx::app {

//======================================================================================================================
bool SceneSession::persistentObject(size_t index) const {
    if (isGenerated(EditorSubject::Object, index, {}))
        return false;
    if (m_loaded) {
        const auto imported = m_loaded->binding.objectImportedNode.at(index);
        if (imported != engine::kGeneratedNode)
            return !m_loaded->binding.importedNodes.at(imported).animated;
    }
    return std::ranges::none_of(scene().animation.tracks,
                                [&](const auto& track) { return track.objectIndex == index; });
}

//======================================================================================================================
rojoRHI::Result<void> SceneSession::editLight(size_t index, const engine::DirectionalLight& light) {
    if (index >= 3)
        return std::unexpected(
            rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc, "Light index out of range"});
    auto& current = scene().lights[index];
    std::optional<uint32_t> node;
    if (m_loaded)
        for (uint32_t n = 0; n < m_loaded->binding.nodes.size(); ++n)
            if (m_loaded->binding.nodes[n].directional == index)
                node = n;
    const bool enabledChanged = light.enabled != (node ? nodeEnabled(*node) : current.enabled);
    if (m_measurementActive && enabledChanged)
        return std::unexpected(rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc,
                                              "Enabled edits are unavailable during measurement"});
    const bool fieldsChanged =
        current.direction != light.direction || current.strength != light.strength;
    current.direction = light.direction;
    current.strength = light.strength;
    if (enabledChanged && node) {
        const auto result = setNodeEnabled(*node, light.enabled);
        LMX_ASSERT(result.has_value(), "validated directional enabled edit succeeds");
    } else {
        if (enabledChanged)
            current.enabled = light.enabled;
        if (fieldsChanged || enabledChanged)
            notifyPersistentEdit();
    }
    if (fieldsChanged || enabledChanged)
        m_temporalResetPending = true;
    return {};
}

//======================================================================================================================
void SceneSession::adoptDocumentResetBaseline() {
    LMX_ASSERT(m_loaded, "saved baseline requires a loaded document");
    auto& defaults = m_defaults.at(m_scene);
    defaults.look = m_loaded->document.look;
    for (size_t i = 0; i < scene().objects.size(); ++i)
        if (persistentObject(i)) {
            const auto& object = scene().objects[i];
            defaults.objects[i] = {object.position, object.eulerDegrees, object.scale};
        }
    for (size_t n = 0; n < m_loaded->binding.nodes.size(); ++n) {
        const auto& binding = m_loaded->binding.nodes[n];
        if (const auto slot = binding.directional) {
            defaults.lights[*slot] = scene().lights[*slot];
            defaults.lights[*slot].enabled = m_loaded->document.nodes[n].enabled;
        }
        if (const auto id = binding.light; id && scene().light(*id)) {
            auto light = *scene().light(*id);
            light.enabled = m_loaded->document.nodes[n].enabled;
            defaults.localLights[engine::sceneLightKey(*id)] = light;
        }
    }
}

} // namespace lmx::app
