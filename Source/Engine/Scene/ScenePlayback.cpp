//----------------------------------------------------------------------------------------------------------------------
/// @file ScenePlayback.cpp
/// @brief Implements scene motion, animation, camera-track following and draw items.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Scene/Scene.h"

#include "Core/Diagnostics/Assert.h"
#include "Engine/Asset/Model/SceneAnimation.h"
#include "Engine/Lights/LocalLight.h"
#include "Engine/Scene/DrawItem.h"
#include "Engine/View/Camera.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace lmx::engine {

//======================================================================================================================
void Scene::resetMotion() {
    // Mechanically the same promotion commitFrame performs; the two differ only in why they run.
    commitFrame();
}

//======================================================================================================================
void Scene::commitFrame() {
    for (SceneObject& object : objects) {
        object.previousModel = object.modelMatrix();
    }
}

//======================================================================================================================
void Scene::advanceAnimation(double dt) {
    animationTime += dt;
    if (animation.loop && animation.duration > 0.0) {
        animationTime = std::fmod(animationTime, animation.duration);
        if (animationTime < 0.0) {
            animationTime += animation.duration;
        }
    }
}

//======================================================================================================================
void Scene::animate(double seconds) {
    for (const asset::RigidTrack& track : animation.tracks) {
        LMX_ASSERT(track.objectIndex < objects.size(), "RigidTrack.objectIndex out of range");
        const auto decomposed = decomposeTransform(asset::sampleRigidTrack(track, seconds));
        LMX_ASSERT(decomposed.has_value(), "a rigid track sampled to an indecomposable pose");
        SceneObject& object = objects[track.objectIndex];
        object.position = decomposed->position;
        object.eulerDegrees = decomposed->eulerDegrees;
        object.scale = decomposed->scale;
    }
    for (const asset::EmissiveTrack& track : animation.emissiveTracks) {
        LMX_ASSERT(track.objectIndex < objects.size(), "EmissiveTrack.objectIndex out of range");
        objects[track.objectIndex].emissiveStrength = asset::sampleEmissiveTrack(track, seconds);
    }
    for (const asset::LightOrbitTrack& track : animation.lightTracks) {
        const auto id = animationLightId(track.light);
        LMX_ASSERT(id.has_value(), "LightOrbitTrack.light out of range");
        const engine::LocalLight* current = light(*id);
        if (!current) {
            continue; // The authored light was removed; its track keeps its reserved index.
        }
        engine::LocalLight moved = *current;
        moved.position = asset::sampleOrbit(track, static_cast<float>(seconds));
        const auto updated = updateLight(*id, moved);
        LMX_ASSERT(updated.has_value(), "an orbit-sampled light must remain valid");
    }
}

//======================================================================================================================
void Scene::followCameraTrack(engine::Camera& camera) const {
    LMX_ASSERT(!animation.cameraTrack.empty(),
               "following a camera track requires at least one key");
    const asset::CameraKey pose = asset::sampleCameraTrack(animation.cameraTrack, animationTime);
    camera.position = pose.position;
    camera.yaw = pose.yaw;
    camera.pitch = pose.pitch;
}

//======================================================================================================================
void Scene::fillDrawItems(std::vector<engine::DrawItem>& items) const {
    validateObjects();
    items.clear();
    items.reserve(objects.size());
    for (const SceneObject& object : objects) {
        const auto* mesh = tryMesh(object.mesh);
        LMX_ASSERT(mesh, "SceneObject mesh identity is invalid");
        const MaterialRecord& factors = material(object.material);
        const auto texture = [this](std::optional<TextureId> id) -> rojoRHI::Texture* {
            if (!id) {
                return nullptr;
            }
            rojoRHI::Texture* resolved = tryTexture(*id);
            LMX_ASSERT(resolved, "material texture identity is invalid");
            return resolved;
        };
        items.push_back({.instanceRow = object.id.slot,
                         .mesh = *mesh,
                         .diffuse = texture(factors.diffuse),
                         .normalMap = texture(factors.normalMap),
                         .metallicRoughness = texture(factors.metallicRoughness),
                         .occlusion = texture(factors.occlusion),
                         .emissiveMap = texture(factors.emissiveMap),
                         .alphaMode = factors.alphaMode,
                         .doubleSided = factors.doubleSided});
        items.back().instanceIdentity =
            uint64_t{object.id.store} << 48 | uint64_t{object.id.generation} << 32 | object.id.slot;
    }
}

} // namespace lmx::engine
