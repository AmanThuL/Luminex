//----------------------------------------------------------------------------------------------------------------------
/// @file ScenePlayback.cpp
/// @brief Implements scene motion, animation, camera-track following and draw items.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Scene/Scene.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Diagnostics/Log.h"
#include "Engine/Asset/Model/SceneAnimation.h"
#include "Engine/Lights/LocalLight.h"
#include "Engine/Scene/DrawItem.h"
#include "Engine/View/Camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace lmx::engine {

namespace {

//======================================================================================================================
double clipTime(double seconds, double duration) {
    if (duration <= 0.0) {
        return seconds;
    }
    double wrapped = std::fmod(seconds, duration);
    if (wrapped < 0.0) {
        wrapped += duration;
    }
    return wrapped;
}

//======================================================================================================================
glm::vec4 sampleChannel(const asset::GltfAnimationChannel& channel, double seconds) {
    LMX_ASSERT(!channel.keys.empty(), "an asset animation channel needs keys");
    const auto& keys = channel.keys;
    if (seconds <= keys.front().time) {
        return keys.front().value;
    }
    if (seconds >= keys.back().time) {
        return keys.back().value;
    }
    size_t earlier = 0;
    while (earlier + 1 < keys.size() && keys[earlier + 1].time <= seconds) {
        ++earlier;
    }
    if (channel.step || earlier + 1 == keys.size()) {
        return keys[earlier].value;
    }
    const auto& a = keys[earlier];
    const auto& b = keys[earlier + 1];
    const float weight = static_cast<float>((seconds - a.time) / (b.time - a.time));
    if (channel.path == asset::GltfAnimationPath::Rotation) {
        const glm::quat qa(a.value.w, a.value.x, a.value.y, a.value.z);
        const glm::quat qb(b.value.w, b.value.x, b.value.y, b.value.z);
        const glm::quat q = glm::normalize(glm::slerp(qa, qb, weight));
        return {q.x, q.y, q.z, q.w};
    }
    return glm::mix(a.value, b.value, weight);
}

//======================================================================================================================
template <class Consume>
void sampleAsset(const AssetClipPlayback& asset, double seconds, Consume&& consume) {
    struct LocalPose {
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
    };
    std::vector<LocalPose> poses;
    poses.reserve(asset.nodes.size());
    for (const auto& node : asset.nodes) {
        poses.push_back({node.translation, node.rotation, node.scale});
    }
    // Source order is significant: later clips and channels replace only the same local
    // component. Each clip samples its own clock, even after the scene's clock wraps.
    for (const auto& clip : asset.clips) {
        const double localTime = clipTime(seconds, clip.duration);
        for (const auto& channel : clip.channels) {
            LMX_ASSERT(channel.node < poses.size(), "asset channel node index out of range");
            const glm::vec4 value = sampleChannel(channel, localTime);
            auto& pose = poses[channel.node];
            switch (channel.path) {
            case asset::GltfAnimationPath::Translation:
                pose.translation = glm::vec3(value);
                break;
            case asset::GltfAnimationPath::Rotation:
                pose.rotation = glm::normalize(glm::quat(value.w, value.x, value.y, value.z));
                break;
            case asset::GltfAnimationPath::Scale:
                pose.scale = glm::vec3(value);
                break;
            }
        }
    }
    std::vector<glm::mat4> world(asset.nodes.size(), glm::mat4(1.0f));
    std::vector<bool> resolved(asset.nodes.size(), false);
    const auto resolve = [&](auto&& self, size_t nodeIndex) -> const glm::mat4& {
        if (resolved[nodeIndex]) {
            return world[nodeIndex];
        }
        const auto& node = asset.nodes[nodeIndex];
        const auto& pose = poses[nodeIndex];
        glm::mat4 local = node.matrix.value_or(glm::scale(
            glm::translate(glm::mat4(1.0f), pose.translation) * glm::mat4_cast(pose.rotation),
            pose.scale));
        if (node.parent >= 0) {
            LMX_ASSERT(static_cast<size_t>(node.parent) < asset.nodes.size(),
                       "asset source parent index out of range");
            world[nodeIndex] = self(self, static_cast<size_t>(node.parent)) * local;
        } else {
            world[nodeIndex] = asset.rootWorld * local;
        }
        resolved[nodeIndex] = true;
        return world[nodeIndex];
    };
    for (size_t n = 0; n < asset.nodes.size(); ++n) {
        const auto& node = asset.nodes[n];
        if (!node.animated || node.instances.empty()) {
            continue;
        }
        const glm::mat4& matrix = resolve(resolve, n);
        const auto decomposed = decomposeTransform(matrix);
        for (uint32_t instance : node.instances) {
            LMX_ASSERT(instance < asset.instances.size(), "asset clip instance index out of range");
            consume(asset.instances[instance], decomposed ? &*decomposed : nullptr);
        }
    }
}

} // namespace

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
    unwrappedAnimationTime += dt;
    if (animation.loop && animation.duration > 0.0) {
        animationTime = std::fmod(animationTime, animation.duration);
        if (animationTime < 0.0) {
            animationTime += animation.duration;
        }
    }
}

//======================================================================================================================
void Scene::animate(double seconds) {
    animate(seconds, seconds);
}

//======================================================================================================================
void Scene::animate(double seconds, double assetSeconds) {
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
    for (const AssetClipPlayback& asset : assetAnimations) {
        sampleAsset(asset, assetSeconds, [this](InstanceId id, const DecomposedTransform* pose) {
            SceneObject* object = tryObject(id);
            if (!object) {
                return;
            }
            if (!pose) {
                // Asset data can sample to a singular pose; hold the last valid one.
                const uint64_t key =
                    uint64_t{id.store} << 48 | uint64_t{id.generation} << 32 | id.slot;
                if (m_indecomposableWarned.insert(key).second) {
                    LMX_LOG_WARN("scene '{}': asset clip sampled an indecomposable pose for "
                                 "object '{}'; holding its previous pose",
                                 name, object->name);
                }
                return;
            }
            object->position = pose->position;
            object->eulerDegrees = pose->eulerDegrees;
            object->scale = pose->scale;
        });
    }
}

//======================================================================================================================
std::optional<DecomposedTransform> Scene::authoredAssetPose(InstanceId id, double seconds) const {
    if (!tryObject(id)) {
        return std::nullopt;
    }
    std::optional<DecomposedTransform> result;
    for (const AssetClipPlayback& asset : assetAnimations) {
        sampleAsset(asset, seconds, [&](InstanceId sampled, const DecomposedTransform* pose) {
            if (sampled == id) {
                result = pose ? std::optional(*pose) : std::nullopt;
            }
        });
    }
    return result;
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
