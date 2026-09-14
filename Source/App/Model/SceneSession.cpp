//----------------------------------------------------------------------------------------------------------------------
/// @file SceneSession.cpp
/// @brief Implements shared scene playback clocks and frame motion ownership.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/SceneSession.h"

#include "Asset/SceneAnimation.h"
#include "Core/Assert.h"

#include <algorithm>
#include <iterator>

namespace lmx::app {

//======================================================================================================================
void SceneSession::activate(scene::Scene& scene, SceneActivationMotion motion) {
    auto [entry, inserted] = m_defaults.try_emplace(&scene);
    if (inserted) {
        for (const auto& object : scene.objects) {
            entry->second.objects.push_back({object.position, object.eulerDegrees, object.scale});
        }
        std::copy(std::begin(scene.lights), std::end(scene.lights), entry->second.lights.begin());
    }
    m_scene = &scene;
    m_camera = scene::cameraFromScene(scene.initialCamera);
    if (motion == SceneActivationMotion::Reset) {
        resetMotion();
    }
}

//======================================================================================================================
scene::Scene& SceneSession::scene() const {
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
render::SceneView SceneSession::view(std::vector<render::DrawItem>& items,
                                     render::ShadowFilter filter, bool wireframe) const {
    return scene().view(items, filter, wireframe);
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
asset::DecomposedTransform SceneSession::objectDefault(size_t index) const {
    LMX_ASSERT(index < scene().objects.size(), "Object index out of range");
    for (const auto& track : scene().animation.tracks) {
        if (track.objectIndex == index) {
            const auto pose =
                asset::decomposeTransform(asset::sampleRigidTrack(track, scene().animationTime));
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
void SceneSession::editObject(size_t index, const asset::DecomposedTransform& transform) {
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
const render::DirectionalLight& SceneSession::lightDefault(size_t index) const {
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

} // namespace lmx::app
