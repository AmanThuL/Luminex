//----------------------------------------------------------------------------------------------------------------------
/// @file SceneSession.cpp
/// @brief Implements shared scene playback clocks and frame motion ownership.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/SceneSession.h"

#include "Core/Diagnostics/Assert.h"
#include "Engine/Asset/Model/SceneAnimation.h"
#include "Engine/Catalog/LightLab.h"
#include "Render/SceneViewBuilder.h"

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
    auto [entry, inserted] = m_defaults.try_emplace(&scene);
    if (inserted) {
        for (const auto& object : scene.objects) {
            entry->second.objects.push_back({object.position, object.eulerDegrees, object.scale});
        }
        std::copy(std::begin(scene.lights), std::end(scene.lights), entry->second.lights.begin());
    }
    if (inserted && scene.lightLabGridCount > 0) {
        LMX_ASSERT(scene.lightLabGridCount <= scene.localLights().size(),
                   "LightLab grid count exceeds authored population");
        const auto pile = scene.localLights().subspan(scene.lightLabGridCount);
        entry->second.pileLights.assign(pile.begin(), pile.end());
    }
    m_scene = &scene;
    rememberLocalLightDefaults();
    m_camera = engine::cameraFromScene(scene.initialCamera);
    if (motion == SceneActivationMotion::Reset) {
        resetMotion();
    }
}

//======================================================================================================================
engine::Scene& SceneSession::scene() const {
    LMX_ASSERT(m_scene != nullptr, "SceneSession requires an active scene");
    return *m_scene;
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
    scene().animate(scene().animationTime);
    followCameraTrack();
}

//======================================================================================================================
void SceneSession::stepAnimation() {
    scene().advanceAnimation(1.0 / asset::kAnimationBakeRate);
    scene().animate(scene().animationTime);
}

//======================================================================================================================
void SceneSession::rewindAnimation() {
    scene().animationTime = 0.0;
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
render::SceneView SceneSession::view(std::vector<engine::DrawItem>& items,
                                     render::ShadowFilter filter, bool wireframe) const {
    return render::buildSceneView(scene(), items, filter, wireframe);
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
    auto& object = scene().objects[index];
    object.position = transform.position;
    object.eulerDegrees = transform.eulerDegrees;
    object.scale = transform.scale;
    object.previousModel = object.modelMatrix();
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
void SceneSession::resetLight(size_t index) {
    scene().lights[index] = lightDefault(index);
}

//======================================================================================================================
bool SceneSession::lightChanged(size_t index) const {
    const auto& original = lightDefault(index);
    const auto& light = scene().lights[index];
    return original.direction != light.direction || original.strength != light.strength;
}

//======================================================================================================================
bool SceneSession::localLightRigAvailable() const {
    return m_scene && m_scene->name == "Sponza";
}

//======================================================================================================================
bool SceneSession::localLightRigEnabled() const {
    if (!localLightRigAvailable())
        return false;
    for (const auto id : m_scene->sponzaLightIds()) {
        if (const auto* light = m_scene->light(id); light && light->enabled)
            return true;
    }
    return false;
}

//======================================================================================================================
rojoRHI::Result<void> SceneSession::setLocalLightRig(bool enabled) {
    if (!localLightRigAvailable()) {
        return std::unexpected(rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc,
                                              "Local-light rig is available only in Sponza"});
    }
    auto result = m_lightRigs[m_scene].setEnabled(scene(), enabled);
    if (result)
        rememberLocalLightDefaults();
    return result;
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
    for (const auto id : scene().localLights())
        defaults.try_emplace(lightKey(id), *scene().light(id));
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
           (original->enabled != current->enabled || original->type != current->type ||
            original->position != current->position || original->colour != current->colour ||
            original->intensity != current->intensity || original->range != current->range ||
            original->direction != current->direction ||
            original->innerCone != current->innerCone || original->outerCone != current->outerCone);
}

//======================================================================================================================
rojoRHI::Result<void> SceneSession::editLocalLight(engine::LightId id,
                                                   const engine::LocalLight& light) {
    if (const auto* current = scene().light(id))
        m_defaults.at(m_scene).localLights.try_emplace(lightKey(id), *current);
    return scene().updateLight(id, light);
}

//======================================================================================================================
rojoRHI::Result<void> SceneSession::resetLocalLight(engine::LightId id) {
    const auto original = localLightDefault(id);
    if (!original)
        return std::unexpected(
            rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc, "Light no longer exists"});
    return scene().updateLight(id, *original);
}

//======================================================================================================================
bool SceneSession::lightLabPileAvailable() const {
    return m_scene && m_scene->lightLabGridCount > 0;
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
    return std::min(engine::kMaxLocalLights - scene().lightLabGridCount,
                    engine::kMaxLocalLights - (static_cast<uint32_t>(scene().localLights().size()) -
                                               lightLabPileCount()));
}

//======================================================================================================================
rojoRHI::Result<void> SceneSession::setLightLabPile(uint32_t count) {
    if (!lightLabPileAvailable() || count > lightLabPileCapacity())
        return std::unexpected(rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc,
                                              "Pile exceeds available LightLab light capacity"});
    auto& pile = m_defaults.at(m_scene).pileLights;
    std::erase_if(pile, [&](auto id) { return scene().light(id) == nullptr; });
    std::vector<engine::LightId> added;
    if (count > pile.size()) {
        // The immutable authored grid count reserves at least one slot, so count <= 4095 and this
        // helper's one unused grid light plus the requested pile obey the generator's 4096 limit.
        const auto authored = engine::lightLabLights(1, count);
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
    }
    while (pile.size() > count) {
        scene().removeLight(pile.back());
        pile.pop_back();
    }
    rememberLocalLightDefaults();
    return {};
}

} // namespace lmx::app
