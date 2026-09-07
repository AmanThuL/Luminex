//----------------------------------------------------------------------------------------------------------------------
/// @file Scene.cpp
/// @brief Implements scene transforms, views, and fetched-scene construction.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Scene.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Engine/Color.h"
#include "Engine/DdsLoader.h"
#include "Engine/GeometryGenerator.h"
#include "Engine/GltfLoader.h"
#include "Engine/Ibl.h"
#include "Engine/SceneEnvironment.h"
#include "Engine/TextureBake.h"

#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace lmx::engine {

namespace {

//======================================================================================================================
// Executables run below the repository root, so asset lookup walks through parent directories.
std::optional<std::filesystem::path> findRepoAsset(std::string_view relative) {
    std::filesystem::path dir = std::filesystem::current_path();
    for (int i = 0; i < 8; ++i) {
        if (std::filesystem::path candidate = dir / relative; std::filesystem::exists(candidate)) {
            return candidate;
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir) {
            break;
        }
        dir = dir.parent_path();
    }
    return std::nullopt;
}

//======================================================================================================================
// The one translate-rotate-scale composition SceneObject::modelMatrix and decomposeTransform's
// round-trip check both go through, so the two can never disagree about the order.
glm::mat4 composeTransform(const DecomposedTransform& transform) {
    glm::mat4 model = glm::translate(glm::mat4{1.0f}, transform.position);
    model = glm::rotate(model, glm::radians(transform.eulerDegrees.y), glm::vec3{0.0f, 1.0f, 0.0f});
    model = glm::rotate(model, glm::radians(transform.eulerDegrees.x), glm::vec3{1.0f, 0.0f, 0.0f});
    model = glm::rotate(model, glm::radians(transform.eulerDegrees.z), glm::vec3{0.0f, 0.0f, 1.0f});
    return glm::scale(model, transform.scale);
}

//======================================================================================================================
AssetError missingAsset(std::string_view sceneName, std::string_view relative) {
    return AssetError{AssetErrorCode::NotFound,
                      std::string(sceneName) + " scene: " + std::string(relative) +
                          " not found from this working directory -- run `xmake setup`"};
}

//======================================================================================================================
AssetError uploadFailure(rhi::Error error) {
    return AssetError{AssetErrorCode::UploadFailed, std::move(error.message)};
}

//======================================================================================================================
// The offline bake (Tools/TextureBake, wired into `xmake setup`) writes each referenced image's
// full mip chain to a sibling "Baked/image<N>.dds" beside the glTF file, keyed by the image's
// index in the glTF/GLB "images" array -- the same index cgltf assigns and this loader already
// threads through as `imageIndex`. Naming by index rather than by source filename is what lets one
// scheme cover both Sponza (external, uniquely-named PNGs) and DamagedHelmet (a single .glb with
// unnamed embedded images) without GltfImage having to carry a source filename at all.
std::filesystem::path bakedDdsPath(const std::filesystem::path& gltfPath, size_t imageIndex) {
    return gltfPath.parent_path() / "Baked" / ("image" + std::to_string(imageIndex) + ".dds");
}

//======================================================================================================================
// Resolves a catalog scene's repo-relative asset before handing it to the public loader, so a
// missing fetch reports the `xmake setup` hint instead of a bare file-not-found.
AssetResult<std::unique_ptr<Scene>> loadCatalogGltfScene(rhi::Device& device,
                                                         std::string_view relativeAssetPath,
                                                         std::string_view sceneName) {
    const auto path = findRepoAsset(relativeAssetPath);
    if (!path) {
        return std::unexpected(missingAsset(sceneName, relativeAssetPath));
    }
    return loadGltfScene(device, path->string(), sceneName);
}

} // namespace

//======================================================================================================================
AssetResult<std::unique_ptr<Scene>> loadGltfScene(rhi::Device& device, std::string_view assetPath,
                                                  std::string_view sceneName) {
    const std::filesystem::path path(assetPath);
    auto loaded = loadGltf(path.string());
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    GltfScene gltfScene = std::move(*loaded);

    auto scene = std::make_unique<Scene>();
    scene->name = std::string(sceneName);

    // Materials share an uploaded texture only when both source image and color space match.
    // Metal texture formats carry the sRGB decode, so one image used in color and data slots
    // needs two views rather than forcing the normal-map read through an sRGB format.
    std::vector<rhi::Texture*> uploadedColor(gltfScene.images.size(), nullptr);
    std::vector<rhi::Texture*> uploadedLinear(gltfScene.images.size(), nullptr);
    // Set the first time the fallback path below actually runs, so a scene with several unbaked
    // images logs the warning once per build, not once per image.
    bool warnedUnbakedFallback = false;
    const auto ensureUploaded = [&](int imageIndex, bool srgb) -> AssetResult<rhi::Texture*> {
        if (imageIndex < 0 || static_cast<size_t>(imageIndex) >= gltfScene.images.size()) {
            return std::unexpected(AssetError{AssetErrorCode::Malformed,
                                              std::string(sceneName) +
                                                  " scene: material image index is out of range"});
        }
        const size_t index = static_cast<size_t>(imageIndex);
        std::vector<rhi::Texture*>& uploaded = srgb ? uploadedColor : uploadedLinear;
        if (uploaded[index] != nullptr) {
            return uploaded[index];
        }
        const std::string label = std::string(sceneName) + ".image" + std::to_string(index) +
                                  (srgb ? ".srgb" : ".linear");

        // The baked DDS carries a deterministic, correctly-filtered full mip chain; prefer it
        // whenever `xmake setup` has produced one.
        const std::filesystem::path baked = bakedDdsPath(path, index);
        if (std::filesystem::exists(baked)) {
            auto texture = createTextureFromDds(device, baked.string(), srgb, label);
            if (!texture) {
                return std::unexpected(texture.error());
            }
            rhi::Texture* ptr = texture->get();
            scene->textures.push_back(std::move(*texture));
            uploaded[index] = ptr;
            return ptr;
        }

        const GltfImage& image = gltfScene.images[index];
        if (image.width == 0 || image.height == 0 || image.rgba8.empty()) {
            return std::unexpected(
                AssetError{AssetErrorCode::Malformed,
                           std::string(sceneName) + " scene: referenced image was not decoded"});
        }
        if (!warnedUnbakedFallback) {
            LMX_LOG_WARN("{} scene: no baked mip chain beside '{}' -- computing mips at load "
                         "time instead of using `xmake setup`'s offline bake (slower startup; "
                         "matches the offline bake for color/data images, but a normal map here "
                         "skips the offline bake's per-level renormalization)",
                         sceneName, path.string());
            warnedUnbakedFallback = true;
        }
        // Same box filter the offline bake uses, just run in-process. srgb selects the
        // colour-space transform; a data image (srgb == false) has no normal-map flag reaching
        // this lambda, so Linear -- filter raw bytes, no transform -- is the choice made here,
        // matching how generateMipmaps used to treat every non-colour texture before this
        // fallback replaced it. That choice is exact for base color and other color/data images,
        // but not for normal maps: the offline bake's `--normal-map` role renormalizes each
        // generated level (see BakeMode::NormalMap), and this fallback has no way to request
        // that mode, so a normal map computed here diverges from its offline bake. Both shipped
        // scenes' normal maps are pre-baked by `xmake setup`, so the divergence is latent.
        const std::span<const uint8_t> rgba8(reinterpret_cast<const uint8_t*>(image.rgba8.data()),
                                             image.rgba8.size());
        const BakedMipChain bakedChain =
            bakeMips(rgba8, image.width, image.height, srgb ? BakeMode::Srgb : BakeMode::Linear);
        auto texture = device.createTexture(
            {.width = bakedChain.width,
             .height = bakedChain.height,
             .format = srgb ? rhi::Format::RGBA8Unorm_sRGB : rhi::Format::RGBA8Unorm,
             .mipLevels = bakedChain.mipLevels,
             .sampled = true,
             .label = label},
            bakedChain.mips);
        if (!texture) {
            return std::unexpected(uploadFailure(std::move(texture.error())));
        }
        rhi::Texture* ptr = texture->get();
        scene->textures.push_back(std::move(*texture));
        uploaded[index] = ptr;
        return ptr;
    };

    scene->materials.reserve(gltfScene.materials.size());
    for (const GltfMaterial& src : gltfScene.materials) {
        render::Material material;
        // glTF factors are linear; texture color-space conversion happens in the texture view.
        material.albedo = src.baseColorFactor;
        material.roughness = src.roughness;
        material.metallic = src.metallic;
        material.occlusionStrength = src.occlusionStrength;
        // glTF's emissiveFactor is linear as authored, unlike a display-space color constant --
        // do not run it through srgbToLinear.
        material.emissive = src.emissiveFactor;
        if (src.baseColorImage >= 0) {
            auto texture = ensureUploaded(src.baseColorImage, true);
            if (!texture) {
                return std::unexpected(texture.error());
            }
            material.diffuse = *texture;
        }
        if (src.normalImage >= 0) {
            auto texture = ensureUploaded(src.normalImage, false);
            if (!texture) {
                return std::unexpected(texture.error());
            }
            material.normalMap = *texture;
        }
        if (src.metallicRoughnessImage >= 0) {
            // Roughness (G) and metallic (B) are sampled data, not color -- linear, no sRGB decode.
            auto texture = ensureUploaded(src.metallicRoughnessImage, false);
            if (!texture) {
                return std::unexpected(texture.error());
            }
            material.metallicRoughness = *texture;
        }
        if (src.occlusionImage >= 0) {
            // Occlusion is sampled data too.
            auto texture = ensureUploaded(src.occlusionImage, false);
            if (!texture) {
                return std::unexpected(texture.error());
            }
            material.occlusion = *texture;
        }
        if (src.emissiveImage >= 0) {
            // Emissive is an authored color texture: sRGB-decode through the texture view.
            auto texture = ensureUploaded(src.emissiveImage, true);
            if (!texture) {
                return std::unexpected(texture.error());
            }
            material.emissiveMap = *texture;
        }
        scene->materials.push_back(material);
    }

    // Release decoded CPU images as soon as all referenced textures are resident.
    gltfScene.images.clear();
    gltfScene.images.shrink_to_fit();

    scene->meshes.reserve(gltfScene.meshes.size());
    for (size_t i = 0; i < gltfScene.meshes.size(); ++i) {
        auto mesh = render::createMesh(device, render::fromGeo(gltfScene.meshes[i]),
                                       std::string(sceneName) + ".mesh" + std::to_string(i));
        if (!mesh) {
            return std::unexpected(uploadFailure(std::move(mesh.error())));
        }
        scene->meshes.push_back(std::move(*mesh));
    }

    scene->objects.reserve(gltfScene.instances.size());
    for (size_t i = 0; i < gltfScene.instances.size(); ++i) {
        const GltfInstance& instance = gltfScene.instances[i];
        if (instance.meshIndex >= gltfScene.meshes.size() ||
            instance.materialIndex >= scene->materials.size()) {
            return std::unexpected(
                AssetError{AssetErrorCode::Malformed,
                           std::string(sceneName) + " scene: instance index is out of range"});
        }
        const auto decomposed = decomposeTransform(instance.world);
        if (!decomposed) {
            return std::unexpected(
                AssetError{AssetErrorCode::Malformed,
                           std::string(sceneName) +
                               " scene: a node's world transform could not be decomposed into "
                               "position/rotation/scale"});
        }
        scene->objects.push_back({.name = std::string(sceneName) + " object " + std::to_string(i),
                                  .position = decomposed->position,
                                  .eulerDegrees = decomposed->eulerDegrees,
                                  .scale = decomposed->scale,
                                  .meshIndex = instance.meshIndex,
                                  .materialIndex = instance.materialIndex});
    }

    scene->animation.tracks.reserve(gltfScene.tracks.size());
    for (GltfAnimationTrack& track : gltfScene.tracks) {
        scene->animation.tracks.push_back(
            {.objectIndex = track.instanceIndex, .keys = std::move(track.keys)});
    }
    scene->animation.duration = gltfScene.animationDuration;
    scene->animation.loop = true;
    // A clip's pose at t = 0 need not be the file's authored rest pose, so the scene is posed
    // before anything measures it: the bounds below, and with them the camera fit and the shadow
    // ortho fit, then describe the geometry the first frame actually draws. Seeding the previous
    // transforms last keeps that first frame reporting no motion.
    if (!scene->animation.tracks.empty()) {
        scene->animate(0.0);
    }
    scene->resetMotion();

    glm::vec3 aabbMin{std::numeric_limits<float>::max()};
    glm::vec3 aabbMax{std::numeric_limits<float>::lowest()};
    for (const SceneObject& object : scene->objects) {
        const glm::mat4 model = object.modelMatrix();
        const GeoData& mesh = gltfScene.meshes[object.meshIndex];
        for (const VertexPNTU& v : mesh.vertices) {
            const glm::vec3 world = glm::vec3(model * glm::vec4(v.px, v.py, v.pz, 1.0f));
            aabbMin = glm::min(aabbMin, world);
            aabbMax = glm::max(aabbMax, world);
        }
    }
    if (aabbMin.x > aabbMax.x) {
        return std::unexpected(
            AssetError{AssetErrorCode::Malformed,
                       std::string(sceneName) + " scene: active glTF scene contains no geometry"});
    }
    const glm::vec3 center = (aabbMin + aabbMax) * 0.5f;
    // Half the AABB diagonal gives a conservative world-space bounding sphere.
    scene->boundingSphere = glm::vec4(center, glm::length(aabbMax - center));

    if (auto sky = attachNeutralEnvironment(device, *scene, sceneName); !sky) {
        return std::unexpected(sky.error());
    }

    return scene;
}

//======================================================================================================================
std::optional<DecomposedTransform> decomposeTransform(const glm::mat4& world) {
    constexpr float kTolerance = 1e-4f;
    if (std::abs(world[0][3]) > kTolerance || std::abs(world[1][3]) > kTolerance ||
        std::abs(world[2][3]) > kTolerance || std::abs(world[3][3] - 1.0f) > kTolerance) {
        return std::nullopt; // a projective row is not a translate-rotate-scale pose
    }

    glm::vec3 columns[3] = {glm::vec3(world[0]), glm::vec3(world[1]), glm::vec3(world[2])};
    glm::vec3 scale{glm::length(columns[0]), glm::length(columns[1]), glm::length(columns[2])};
    // A mirroring basis has no rotation of its own; glm::decompose's convention -- which this
    // extraction has always followed -- puts the flip in all three scale components at once.
    if (glm::dot(columns[0], glm::cross(columns[1], columns[2])) < 0.0f) {
        scale = -scale;
        for (glm::vec3& column : columns) {
            column = -column;
        }
    }

    glm::mat3 basis{1.0f};
    int degenerate = 0;
    for (int i = 0; i < 3; ++i) {
        const float length = glm::length(columns[i]);
        if (length > kTolerance) {
            basis[i] = columns[i] / length;
        } else {
            ++degenerate;
        }
    }
    if (degenerate == 1) {
        // Two axes still fix the frame; the third is their right-handed completion. A zero scale
        // on one axis is a legitimate authored pose -- glm::decompose would divide by that axis's
        // length and return NaN rather than reporting a failure, which is why this does not use it.
        for (int i = 0; i < 3; ++i) {
            if (glm::length(columns[i]) <= kTolerance) {
                basis[i] = glm::cross(basis[(i + 1) % 3], basis[(i + 2) % 3]);
            }
        }
    } else if (degenerate == 2) {
        return std::nullopt; // one surviving axis fixes no rotation
    }

    // Extraction must match SceneObject's Y-X-Z composition order to round-trip compound rotation.
    float yaw = 0.f, pitch = 0.f, roll = 0.f;
    glm::extractEulerAngleYXZ(glm::mat4(basis), yaw, pitch, roll);
    const DecomposedTransform decomposed{.position = glm::vec3(world[3]),
                                         .eulerDegrees = glm::degrees(glm::vec3(pitch, yaw, roll)),
                                         .scale = scale};

    // Proving the factorisation by recomposing it is what makes this a decision rather than a
    // guess: shear, which no translate-rotate-scale chain can produce, fails here instead of being
    // silently orthogonalised, and a NaN fails the comparison rather than escaping into a caller.
    const glm::mat4 recomposed = composeTransform(decomposed);
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            const float scaled = kTolerance * std::max(1.0f, std::abs(world[col][row]));
            if (!(std::abs(recomposed[col][row] - world[col][row]) <= scaled)) {
                return std::nullopt;
            }
        }
    }
    return decomposed;
}

//======================================================================================================================
glm::mat4 SceneObject::modelMatrix() const {
    return composeTransform({.position = position, .eulerDegrees = eulerDegrees, .scale = scale});
}

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
    for (const RigidTrack& track : animation.tracks) {
        LMX_ASSERT(track.objectIndex < objects.size(), "RigidTrack.objectIndex out of range");
        const auto decomposed = decomposeTransform(sampleRigidTrack(track, seconds));
        LMX_ASSERT(decomposed.has_value(), "a rigid track sampled to an indecomposable pose");
        SceneObject& object = objects[track.objectIndex];
        object.position = decomposed->position;
        object.eulerDegrees = decomposed->eulerDegrees;
        object.scale = decomposed->scale;
    }
}

//======================================================================================================================
render::SceneView Scene::view(std::vector<render::DrawItem>& items, render::ShadowFilter filter,
                              bool wireframe) const {
    items.clear();
    items.reserve(objects.size());
    for (const SceneObject& object : objects) {
        LMX_ASSERT(object.meshIndex < meshes.size(), "SceneObject.meshIndex out of range");
        LMX_ASSERT(object.materialIndex < materials.size(),
                   "SceneObject.materialIndex out of range");
        items.push_back({.mesh = &meshes[object.meshIndex],
                         .model = object.modelMatrix(),
                         .material = materials[object.materialIndex],
                         .previousModel = object.previousModel,
                         .motionClass = object.motionClass});
    }

    render::SceneView sceneView;
    sceneView.items = items;
    for (size_t i = 0; i < std::size(sceneView.lights); ++i) {
        sceneView.lights[i] = lights[i];
    }
    sceneView.boundingSphere = boundingSphere;
    // A cubemap marks a fully constructed sky; the sphere and cubemap are published together.
    if (skyCubemap != nullptr) {
        sceneView.skySphere = &skySphere;
        sceneView.skyCubemap = skyCubemap.get();
    }
    // The IBL set is generated from that same sky and published with it, so a scene that shows a
    // sky also lights from it. Forwarded unconditionally: unique_ptr::get() on an empty pointer is
    // the null the renderer's fallbacks already handle.
    sceneView.irradiance = irradianceMap.get();
    sceneView.prefilteredEnv = prefilteredEnvMap.get();
    sceneView.dfgLut = dfgLut.get();
    sceneView.shadowFilter = filter;
    sceneView.wireframe = wireframe;
    return sceneView;
}

//======================================================================================================================
AssetResult<std::unique_ptr<Scene>> loadSponzaScene(rhi::Device& device) {
    auto scene = loadCatalogGltfScene(device, "Assets/Fetched/Sponza/Sponza.gltf", "Sponza");
    if (!scene) {
        return std::unexpected(scene.error());
    }

    const glm::vec3 center{(*scene)->boundingSphere};
    const float radius = (*scene)->boundingSphere.w;
    // Place the camera inside the long X-axis atrium, looking toward its center.
    (*scene)->initialCamera.position = center + glm::vec3(radius * 0.6f, 0.0f, 0.0f);
    (*scene)->initialCamera.yaw = -glm::half_pi<float>();
    (*scene)->initialCamera.pitch = 0.0f;
    (*scene)->initialCamera.fovY = glm::radians(45.0f);
    (*scene)->initialCamera.nearZ = 0.05f;
    (*scene)->initialCamera.farZ = radius * 20.0f;

    return std::move(*scene);
}

//======================================================================================================================
AssetResult<std::unique_ptr<Scene>> loadHelmetScene(rhi::Device& device) {
    auto scene = loadCatalogGltfScene(device, "Assets/Fetched/DamagedHelmet/DamagedHelmet.glb",
                                      "DamagedHelmet");
    if (!scene) {
        return std::unexpected(scene.error());
    }

    const glm::vec3 center{(*scene)->boundingSphere};
    const float radius = (*scene)->boundingSphere.w;
    // A +Z showcase view looks toward the model with the default -Z forward vector.
    (*scene)->initialCamera.position = center + glm::vec3(0.0f, 0.0f, radius * 2.5f);
    (*scene)->initialCamera.yaw = 0.0f;
    (*scene)->initialCamera.pitch = 0.0f;
    (*scene)->initialCamera.fovY = glm::radians(45.0f);
    (*scene)->initialCamera.nearZ = 0.01f;
    (*scene)->initialCamera.farZ = radius * 20.0f;

    return std::move(*scene);
}

//======================================================================================================================
AssetResult<std::unique_ptr<Scene>> loadMilkTruckScene(rhi::Device& device) {
    auto scene = loadCatalogGltfScene(device, "Assets/Fetched/CesiumMilkTruck/CesiumMilkTruck.glb",
                                      "CesiumMilkTruck");
    if (!scene) {
        return std::unexpected(scene.error());
    }

    const glm::vec3 center{(*scene)->boundingSphere};
    const float radius = (*scene)->boundingSphere.w;
    // A +Z showcase view looks toward the model with the default -Z forward vector.
    (*scene)->initialCamera.position = center + glm::vec3(0.0f, 0.0f, radius * 2.5f);
    (*scene)->initialCamera.yaw = 0.0f;
    (*scene)->initialCamera.pitch = 0.0f;
    (*scene)->initialCamera.fovY = glm::radians(45.0f);
    (*scene)->initialCamera.nearZ = 0.01f;
    (*scene)->initialCamera.farZ = radius * 20.0f;

    return std::move(*scene);
}

} // namespace lmx::engine
