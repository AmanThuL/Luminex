//----------------------------------------------------------------------------------------------------------------------
/// @file SceneSession.cpp
/// @brief Implements shared scene playback clocks and frame motion ownership.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Scene/SceneSession.h"

#include "App/Model/Scene/EditorSelection.h"
#include "Core/Diagnostics/Assert.h"
#include "Engine/Asset/Model/SceneAnimation.h"
#include "Render/Renderer/SceneViewBuilder.h"
#include "Scenes/LightLab.h"

#include <algorithm>
#include <iterator>

namespace lmx::app {
namespace {

//======================================================================================================================
uint64_t lightKey(engine::LightId id) {
    return (uint64_t{id.store} << 48) | (uint64_t{id.generation} << 32) | id.slot;
}

} // namespace

//======================================================================================================================
void SceneSession::activate(engine::Scene& scene, SceneActivationMotion motion) {
    m_loaded = nullptr;
    auto [entry, inserted] = m_defaults.try_emplace(&scene);
    if (inserted) {
        entry->second.look = scene.look;
        for (const auto& object : scene.objects) {
            entry->second.objects.push_back({object.position, object.eulerDegrees, object.scale});
            entry->second.objectOwnEnabled.push_back(object.enabled);
        }
        std::copy(std::begin(scene.lights), std::end(scene.lights), entry->second.lights.begin());
    }
    if (inserted && !scene.lightLabPopulations.empty())
        entry->second.pileLights = scene.lightLabPopulations.front().pile;
    m_scene = &scene;
    m_temporalResetPending = false;
    rememberLocalLightDefaults();
    m_camera = engine::cameraFromScene(scene.initialCamera);
    if (motion == SceneActivationMotion::Reset) {
        resetMotion();
    }
}

//======================================================================================================================
void SceneSession::activate(engine::LoadedScene& loaded, SceneActivationMotion motion) {
    const bool firstActivation = !m_defaults.contains(loaded.scene.get());
    activate(*loaded.scene, motion);
    if (firstActivation)
        m_defaults.at(m_scene).look = loaded.document.look;
    m_loaded = &loaded;
    m_documentStates.try_emplace(loaded.scene.get(), scenes::initialDocumentState(loaded));
    if (firstActivation) {
        auto& defaults = m_defaults.at(m_scene);
        for (size_t i = 0; i < loaded.binding.objectGeneratorNode.size(); ++i)
            if (loaded.binding.objectGeneratorNode[i] != engine::kGeneratedNode)
                defaults.objectOwnEnabled[i] = loaded.binding.generatedObjectEnabled.at(i);
        for (const auto& [key, enabled] : loaded.binding.generatedLightEnabled) {
            defaults.lightOwnEnabled[key] = enabled;
            defaults.localLights.at(key).enabled = enabled;
        }
        for (const auto& [key, node] : loaded.binding.lightNode)
            defaults.localLights.at(key).enabled = loaded.document.nodes[node].enabled;
        for (size_t n = 0; n < loaded.binding.nodes.size(); ++n)
            if (const auto slot = loaded.binding.nodes[n].directional)
                defaults.lights[*slot].enabled = loaded.document.nodes[n].enabled;
    }
}

//======================================================================================================================
void SceneSession::invalidate(const engine::Scene& old) {
    m_defaults.erase(&old);
    m_documentStates.erase(&old);
    if (m_scene == &old) {
        m_scene = nullptr;
        m_loaded = nullptr;
    }
}

//======================================================================================================================
const scenes::SessionDocumentState& SceneSession::documentState() const {
    LMX_ASSERT(m_loaded, "document state requires a loaded document");
    return m_documentStates.at(m_scene);
}

//======================================================================================================================
engine::Scene& SceneSession::scene() const {
    LMX_ASSERT(m_scene != nullptr, "SceneSession requires an active scene");
    return *m_scene;
}

//======================================================================================================================
const asset::SceneLook& SceneSession::look() const {
    return scene().look;
}

//======================================================================================================================
const asset::SceneLook& SceneSession::lookDefault() const {
    return m_defaults.at(m_scene).look;
}

//======================================================================================================================
void SceneSession::editLook(const asset::SceneLook& look) {
    if (scene().look == look)
        return;
    scene().look = look;
    notifyPersistentEdit();
}

//======================================================================================================================
void SceneSession::adoptLookResetBaseline(const asset::SceneLook& saved) {
    m_defaults.at(m_scene).look = saved;
}

//======================================================================================================================
uint64_t SceneSession::editGeneration() const {
    return m_defaults.at(m_scene).editGeneration;
}

//======================================================================================================================
void SceneSession::notifyPersistentEdit() {
    ++m_defaults.at(m_scene).editGeneration;
}

//======================================================================================================================
void SceneSession::advanceEditorFrame(bool playing, bool followCamera, bool flyCameraOverride) {
    if (playing && asset::hasAnimationTracks(scene().animation)) {
        stepAnimation();
    }
    if (followCamera && !flyCameraOverride) {
        followCameraTrack();
    }
}

//======================================================================================================================
void SceneSession::prepareScreenshotFrame(uint32_t frame) {
    if (frame > 0 && asset::hasAnimationTracks(scene().animation)) {
        stepAnimation();
    }
    followCameraTrack();
}

//======================================================================================================================
void SceneSession::prepareSequenceFrame(uint32_t frame) {
    scene().animationTime = static_cast<double>(frame) / asset::kAnimationBakeRate;
    scene().unwrappedAnimationTime = scene().animationTime;
    scene().animate(scene().animationTime, scene().unwrappedAnimationTime);
    followCameraTrack();
}

//======================================================================================================================
void SceneSession::stepAnimation() {
    scene().advanceAnimation(1.0 / asset::kAnimationBakeRate);
    scene().animate(scene().animationTime, scene().unwrappedAnimationTime);
}

//======================================================================================================================
void SceneSession::rewindAnimation() {
    scene().animationTime = 0.0;
    scene().unwrappedAnimationTime = 0.0;
    scene().animate(0.0);
    resetMotion();
}

//======================================================================================================================
rojoRHI::Result<void> SceneSession::prepareFrame(uint64_t frameNumber) {
    return scene().prepareFrame(frameNumber);
}

//======================================================================================================================
engine::SceneTableStats SceneSession::tableStats() const {
    return scene().tableStats();
}

//======================================================================================================================
render::SceneView SceneSession::view(std::vector<engine::DrawItem>& items, bool wireframe) const {
    return render::buildSceneView(scene(), items, wireframe);
}

//======================================================================================================================
void SceneSession::resetMotion() {
    scene().resetMotion();
}

//======================================================================================================================
void SceneSession::commitFrame() {
    scene().commitFrame();
}

//======================================================================================================================
void SceneSession::followCameraTrack() {
    if (!scene().animation.cameraTrack.empty()) {
        scene().followCameraTrack(m_camera);
    }
}

//======================================================================================================================
DecomposedTransform SceneSession::objectDefault(size_t index) const {
    LMX_ASSERT(index < scene().objects.size(), "Object index out of range");
    if (const auto pose =
            scene().authoredAssetPose(scene().objects[index].id, scene().unwrappedAnimationTime)) {
        return *pose;
    }
    for (const auto& track : scene().animation.tracks) {
        if (track.objectIndex == index) {
            const auto pose =
                decomposeTransform(asset::sampleRigidTrack(track, scene().animationTime));
            LMX_ASSERT(pose.has_value(), "Authored track pose must decompose");
            return *pose;
        }
    }
    return m_defaults.at(m_scene).objects.at(index);
}

//======================================================================================================================
bool SceneSession::objectChanged(size_t index) const {
    const auto original = objectDefault(index);
    const auto& object = scene().objects[index];
    return object.position != original.position || object.eulerDegrees != original.eulerDegrees ||
           object.scale != original.scale;
}

//======================================================================================================================
void SceneSession::editObject(size_t index, const DecomposedTransform& transform) {
    LMX_ASSERT(index < scene().objects.size(), "Object index out of range");
    const auto apply = [&](size_t target) {
        auto& object = scene().objects[target];
        const bool changed = object.position != transform.position ||
                             object.eulerDegrees != transform.eulerDegrees ||
                             object.scale != transform.scale;
        object.position = transform.position;
        object.eulerDegrees = transform.eulerDegrees;
        object.scale = transform.scale;
        object.previousModel = object.modelMatrix();
        return changed;
    };
    bool changed = false;
    const auto imported =
        m_loaded ? m_loaded->binding.objectImportedNode.at(index) : engine::kGeneratedNode;
    if (imported != engine::kGeneratedNode) {
        for (size_t target : m_loaded->binding.importedNodes.at(imported).objects)
            changed |= apply(target);
    } else {
        changed = apply(index);
    }
    if (changed && persistentObject(index))
        notifyPersistentEdit();
}

//======================================================================================================================
void SceneSession::resetObject(size_t index) {
    editObject(index, objectDefault(index));
}

//======================================================================================================================
const engine::DirectionalLight& SceneSession::lightDefault(size_t index) const {
    LMX_ASSERT(index < 3, "Light index out of range");
    return m_defaults.at(m_scene).lights[index];
}

//======================================================================================================================
rojoRHI::Result<void> SceneSession::resetLight(size_t index) {
    return editLight(index, lightDefault(index));
}

//======================================================================================================================
bool SceneSession::lightChanged(size_t index) const {
    const auto& original = lightDefault(index);
    const auto& light = scene().lights[index];
    return original.direction != light.direction || original.strength != light.strength ||
           original.enabled != ([&] {
               if (m_loaded)
                   for (size_t n = 0; n < m_loaded->binding.nodes.size(); ++n)
                       if (m_loaded->binding.nodes[n].directional == index)
                           return nodeEnabled(static_cast<uint32_t>(n));
               return light.enabled;
           })();
}

//======================================================================================================================
bool SceneSession::localLightRigAvailable() const {
    return m_loaded && m_loaded->binding.localLightGroup.has_value();
}

//======================================================================================================================
bool SceneSession::localLightRigEnabled() const {
    if (!localLightRigAvailable())
        return false;
    for (const auto id : scene().rigLightIds()) {
        const auto* light = scene().light(id);
        if (light && light->enabled)
            return true;
    }
    return false;
}

//======================================================================================================================
void SceneSession::rememberLocalLightDefaults() {
    auto& defaults = m_defaults.at(m_scene).localLights;
    std::erase_if(defaults, [&](const auto& entry) {
        const auto key = entry.first;
        return scene().light({.slot = static_cast<uint32_t>(key),
                              .generation = static_cast<uint16_t>(key >> 32),
                              .store = static_cast<uint16_t>(key >> 48)}) == nullptr;
    });
    auto& own = m_defaults.at(m_scene).lightOwnEnabled;
    std::erase_if(own, [&](const auto& entry) { return !defaults.contains(entry.first); });
    for (const auto id : scene().localLights()) {
        defaults.try_emplace(lightKey(id), *scene().light(id));
        own.try_emplace(lightKey(id), scene().light(id)->enabled);
    }
}

//======================================================================================================================
std::optional<engine::LocalLight> SceneSession::localLightDefault(engine::LightId id) const {
    const auto* current = scene().light(id);
    if (!current)
        return std::nullopt;
    const auto& defaults = m_defaults.at(m_scene).localLights;
    const auto found = defaults.find(lightKey(id));
    auto result = found == defaults.end() ? *current : found->second;
    for (const auto& track : scene().animation.lightTracks) {
        if (scene().animationLightId(track.light) == id) {
            result.position = asset::sampleOrbit(track, static_cast<float>(scene().animationTime));
            break;
        }
    }
    return result;
}

//======================================================================================================================
bool SceneSession::localLightChanged(engine::LightId id) const {
    const auto original = localLightDefault(id);
    const auto* current = scene().light(id);
    return original && current &&
           (original->enabled != localLightEnabled(id) || original->type != current->type ||
            original->position != current->position || original->colour != current->colour ||
            original->intensity != current->intensity || original->range != current->range ||
            original->direction != current->direction ||
            original->innerCone != current->innerCone || original->outerCone != current->outerCone);
}

//======================================================================================================================
rojoRHI::Result<void> SceneSession::editLocalLight(engine::LightId id,
                                                   const engine::LocalLight& light) {
    const auto* current = scene().light(id);
    if (!current)
        return std::unexpected(
            rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc, "Light no longer exists"});
    if (m_measurementActive && light.enabled != localLightEnabled(id))
        return std::unexpected(rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc,
                                              "Enabled edits are unavailable during measurement"});
    m_defaults.at(m_scene).localLights.try_emplace(lightKey(id), *current);
    const bool fieldsChanged =
        current->type != light.type || current->position != light.position ||
        current->colour != light.colour || current->intensity != light.intensity ||
        current->range != light.range || current->direction != light.direction ||
        current->innerCone != light.innerCone || current->outerCone != light.outerCone;
    const bool enabledChanged = light.enabled != localLightEnabled(id);
    auto effective = light;
    effective.enabled = current->enabled;
    if (auto result = scene().updateLight(id, effective); !result)
        return result;
    if (enabledChanged) {
        const auto result = setLocalLightEnabled(id, light.enabled);
        LMX_ASSERT(result.has_value(), "validated local light enabled edit succeeds");
    }
    if (fieldsChanged && !enabledChanged && !isGenerated(EditorSubject::LocalLight, 0, id))
        notifyPersistentEdit();
    if (fieldsChanged)
        m_temporalResetPending = true;
    return {};
}

//======================================================================================================================
rojoRHI::Result<void> SceneSession::resetLocalLight(engine::LightId id) {
    const auto original = localLightDefault(id);
    if (!original)
        return std::unexpected(
            rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc, "Light no longer exists"});
    return editLocalLight(id, *original);
}

//======================================================================================================================
bool SceneSession::lightLabPileAvailable() const {
    return m_scene && !m_scene->lightLabPopulations.empty();
}

//======================================================================================================================
uint32_t SceneSession::lightLabPileCount() const {
    if (!lightLabPileAvailable())
        return 0;
    const auto& pile = m_defaults.at(m_scene).pileLights;
    return static_cast<uint32_t>(std::count_if(
        pile.begin(), pile.end(), [&](auto id) { return scene().light(id) != nullptr; }));
}

//======================================================================================================================
uint32_t SceneSession::lightLabPileCapacity() const {
    if (!lightLabPileAvailable())
        return 0;
    return std::min(engine::kMaxLocalLights - 1,
                    engine::kMaxLocalLights - (static_cast<uint32_t>(scene().localLights().size()) -
                                               lightLabPileCount()));
}

//======================================================================================================================
rojoRHI::Result<void> SceneSession::setLightLabPile(uint32_t count) {
    if (m_measurementActive || !lightLabPileAvailable() || count > lightLabPileCapacity())
        return std::unexpected(rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc,
                                              "Pile exceeds available LightLab light capacity"});
    auto& pile = m_defaults.at(m_scene).pileLights;
    std::erase_if(pile, [&](auto id) { return scene().light(id) == nullptr; });
    const bool populationChanged = pile.size() != count;
    std::vector<engine::LightId> added;
    if (count > pile.size()) {
        // The immutable authored grid count reserves at least one slot, so count <= 4095 and this
        // helper's one unused grid light plus the requested pile obey the generator's 4096 limit.
        auto authored = scenes::lightLabLights(1, count);
        const auto owner = scene().lightLabPopulations.front().documentNode;
        if (m_loaded && owner < m_loaded->document.nodes.size()) {
            const auto effective = effectiveNodes();
            for (auto& light : authored)
                light.enabled = effective[owner];
        }
        for (size_t i = pile.size(); i < count; ++i) {
            const auto id = scene().addLight(authored[i + 1]);
            if (!id) {
                for (auto addedId : added)
                    scene().removeLight(addedId);
                return std::unexpected(id.error());
            }
            added.push_back(*id);
        }
        pile.insert(pile.end(), added.begin(), added.end());
        for (auto id : added) {
            m_defaults.at(m_scene).lightOwnEnabled[lightKey(id)] = true;
            auto original = *scene().light(id);
            original.enabled = true;
            m_defaults.at(m_scene).localLights[lightKey(id)] = original;
            if (m_loaded) {
                m_loaded->binding.lightGeneratorNode.emplace(lightKey(id), owner);
                m_loaded->binding.generatedLightEnabled.emplace(lightKey(id), true);
            }
        }
    }
    while (pile.size() > count) {
        if (m_loaded) {
            m_loaded->binding.lightGeneratorNode.erase(engine::sceneLightKey(pile.back()));
            m_loaded->binding.generatedLightEnabled.erase(engine::sceneLightKey(pile.back()));
        }
        scene().removeLight(pile.back());
        pile.pop_back();
    }
    scene().lightLabPopulations.front().pile = pile;
    if (populationChanged)
        m_temporalResetPending = true;
    rememberLocalLightDefaults();
    return {};
}

} // namespace lmx::app
