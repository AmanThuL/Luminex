//----------------------------------------------------------------------------------------------------------------------
/// @file SceneTreeState.cpp
/// @brief Caches the built Hierarchy tree and owns per-scene collapse choices.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Scene/SceneTreeState.h"

#include "App/Model/Scene/SceneSession.h"

namespace lmx::app {
namespace {

//======================================================================================================================
void mix(uint64_t& hash, uint64_t value) {
    hash = (hash ^ value) * 0x100000001b3ull;
}

} // namespace

//======================================================================================================================
uint64_t sceneTreeFingerprint(const engine::LoadedScene& loaded,
                              const scenes::SessionDocumentState& state,
                              const SceneSession* session) {
    const auto& scene = *loaded.scene;
    const auto& binding = loaded.binding;
    uint64_t hash = 0xcbf29ce484222325ull;
    mix(hash, scene.objects.size());
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        mix(hash, scene.objects[i].enabled);
        mix(hash, session ? session->objectEnabled(i) : 1);
    }
    mix(hash, scene.localLights().size());
    for (const auto id : scene.localLights()) {
        const auto* light = scene.light(id);
        mix(hash, engine::sceneLightKey(id));
        mix(hash, light && light->enabled);
        mix(hash, session ? session->localLightEnabled(id) : 1);
    }
    for (uint32_t i = 0; i < loaded.document.nodes.size(); ++i) {
        mix(hash, session ? session->nodeEnabled(i) : state.nodeEnabled[i]);
        mix(hash, session ? session->nodeEffectiveEnabled(i) : 1);
    }
    for (uint32_t i = 0; i < binding.importedNodes.size(); ++i)
        mix(hash, session ? session->importedNodeEnabled(i) : state.importedEnabled[i]);
    return hash;
}

//======================================================================================================================
const SceneTreeView& SceneTreeState::view(const std::string& sceneKey,
                                          const SceneTreeInputs& inputs) {
    auto& expansion = m_expansion[sceneKey];
    for (uint32_t node = 0; node < inputs.loaded.document.nodes.size(); ++node)
        if (inputs.loaded.document.nodes[node].generator &&
            expansion.initialized.insert(node).second) {
            expansion.collapsed.insert(node);
            ++expansion.version;
        }
    Key key{.sceneKey = sceneKey,
            .loaded = &inputs.loaded,
            .sceneGeneration = inputs.sceneGeneration,
            .editGeneration = inputs.editGeneration,
            .fingerprint = sceneTreeFingerprint(inputs.loaded, inputs.state, inputs.session),
            .expansionVersion = expansion.version,
            .filter = std::string(inputs.filter)};
    if (!m_valid || !(key == m_key)) {
        m_view = buildSceneTreeView(inputs.loaded, inputs.state, inputs.filter, expansion.collapsed,
                                    inputs.session);
        m_key = std::move(key);
        m_valid = true;
        ++m_buildCount;
    }
    return m_view;
}

//======================================================================================================================
bool SceneTreeState::collapsed(const std::string& sceneKey, uint32_t key) const {
    const auto found = m_expansion.find(sceneKey);
    return found != m_expansion.end() && found->second.collapsed.contains(key);
}

//======================================================================================================================
void SceneTreeState::setCollapsed(const std::string& sceneKey, uint32_t key, bool collapsed) {
    auto& expansion = m_expansion[sceneKey];
    expansion.initialized.insert(key);
    const bool changed =
        collapsed ? expansion.collapsed.insert(key).second : expansion.collapsed.erase(key) > 0;
    if (changed)
        ++expansion.version;
}

//======================================================================================================================
bool SceneTreeState::rootCollapsed(const std::string& sceneKey) const {
    const auto found = m_expansion.find(sceneKey);
    return found != m_expansion.end() && found->second.rootCollapsed;
}

//======================================================================================================================
void SceneTreeState::setRootCollapsed(const std::string& sceneKey, bool collapsed) {
    m_expansion[sceneKey].rootCollapsed = collapsed;
}

//======================================================================================================================
void SceneTreeState::rekey(const std::string& from, const std::string& to) {
    if (from == to)
        return;
    if (const auto found = m_expansion.find(from); found != m_expansion.end()) {
        auto moved = std::move(found->second);
        m_expansion.erase(found);
        ++moved.version;
        m_expansion[to] = std::move(moved);
    }
    m_valid = false;
}

} // namespace lmx::app
