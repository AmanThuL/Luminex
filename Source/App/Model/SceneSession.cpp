//----------------------------------------------------------------------------------------------------------------------
/// @file SceneSession.cpp
/// @brief Implements shared scene playback clocks and frame motion ownership.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/SceneSession.h"

#include "Asset/SceneAnimation.h"
#include "Core/Assert.h"

namespace lmx::app {

//======================================================================================================================
void SceneSession::activate(scene::Scene& scene, SceneActivationMotion motion) {
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

} // namespace lmx::app
