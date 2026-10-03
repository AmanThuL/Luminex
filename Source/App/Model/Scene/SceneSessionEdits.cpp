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
std::string_view poseLockReason(PoseLock lock) {
    switch (lock) {
    case PoseLock::None:
        return "";
    case PoseLock::Static:
        return "Static: mobility is authored in the scene file";
    case PoseLock::Generated:
        return "Generated objects are placed by their generator";
    case PoseLock::Animated:
        return "Animation owns this transform";
    case PoseLock::Measuring:
        return "Pose edits are unavailable during measurement";
    }
    return "Static: mobility is authored in the scene file";
}

//======================================================================================================================
PoseLock SceneSession::objectPoseLock(size_t index) const {
    if (!m_scene || index >= scene().objects.size())
        return PoseLock::Static;
    if (m_measurementActive)
        return PoseLock::Measuring;
    if (isGenerated(EditorSubject::Object, index, {}))
        return PoseLock::Generated;
    if (!persistentObject(index))
        return PoseLock::Animated;
    return m_loaded && index < m_loaded->objectMobility.size() &&
                   m_loaded->objectMobility[index] == asset::DocMobility::Movable
               ? PoseLock::None
               : PoseLock::Static;
}

//======================================================================================================================
PoseLock SceneSession::lightPoseLock(EditorSubject subject, size_t index,
                                     engine::LightId id) const {
    if (!m_scene ||
        (subject != EditorSubject::LocalLight && subject != EditorSubject::DirectionalLight) ||
        (subject == EditorSubject::LocalLight ? !scene().light(id) : index >= 3))
        return PoseLock::Static;
    if (m_measurementActive)
        return PoseLock::Measuring;
    if (isGenerated(subject, index, id))
        return PoseLock::None;
    if (subject == EditorSubject::LocalLight)
        for (const auto& track : scene().animation.lightTracks)
            if (scene().animationLightId(track.light) == id)
                return PoseLock::Animated;
    if (m_loaded) {
        size_t mobility = 0;
        for (const auto& node : m_loaded->binding.nodes) {
            if (!node.directional && !node.light)
                continue;
            const bool matches = subject == EditorSubject::DirectionalLight
                                     ? node.directional == index
                                     : node.light == id;
            if (matches)
                return mobility < m_loaded->lightMobility.size() &&
                               m_loaded->lightMobility[mobility] == asset::DocMobility::Movable
                           ? PoseLock::None
                           : PoseLock::Static;
            ++mobility;
        }
    }
    return PoseLock::Static;
}

//======================================================================================================================
bool SceneSession::persistentObject(size_t index) const {
    if (!m_scene || index >= scene().objects.size() ||
        isGenerated(EditorSubject::Object, index, {}))
        return false;
    if (m_loaded && index < m_loaded->binding.objectImportedNode.size()) {
        const auto imported = m_loaded->binding.objectImportedNode[index];
        if (imported != engine::kGeneratedNode &&
            (imported >= m_loaded->binding.importedNodes.size() ||
             m_loaded->binding.importedNodes[imported].animated))
            return false;
    }
    for (const auto& asset : scene().assetAnimations)
        for (const auto& node : asset.nodes)
            if (node.animated)
                for (const auto instance : node.instances)
                    if (instance < asset.instances.size() &&
                        asset.instances[instance] == scene().objects[index].id)
                        return false;
    return std::ranges::none_of(scene().animation.tracks,
                                [&](const auto& track) { return track.objectIndex == index; });
}

//======================================================================================================================
rojoRHI::Result<void> SceneSession::editLight(size_t index, const engine::DirectionalLight& light) {
    if (!m_scene || index >= 3)
        return std::unexpected(
            rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc, "Light index out of range"});
    auto& current = scene().lights[index];
    const auto lock = lightPoseLock(EditorSubject::DirectionalLight, index, {});
    if (lock != PoseLock::None && current.direction != light.direction)
        return std::unexpected(
            rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc, std::string(poseLockReason(lock))});
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
    if (lock == PoseLock::None)
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
