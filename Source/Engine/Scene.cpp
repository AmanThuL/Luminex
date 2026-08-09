#include "Engine/Scene.h"

#include "Core/Assert.h"
#include "Engine/Color.h"
#include "Engine/GeometryGenerator.h"
#include "Engine/GltfLoader.h"

#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
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
AssetError missingAsset(std::string_view sceneName, std::string_view relative) {
    return AssetError{AssetErrorCode::NotFound,
                      std::string(sceneName) + " scene: " + std::string(relative) +
                          " not found from this working directory -- run `xmake setup`"};
}

//======================================================================================================================
AssetError uploadFailure(rhi::Error error) {
    return AssetError{AssetErrorCode::UploadFailed, std::move(error.message)};
}

constexpr glm::vec3 kLightDirections[3] = {
    {0.577f, -0.577f, 0.577f},
    {-0.577f, -0.577f, 0.577f},
    {0.0f, -0.707f, -0.707f},
};

constexpr float kLightStrengths[3] = {0.7f, 0.2f, 0.2f};
static_assert(std::size(kLightStrengths) == std::size(kLightDirections),
              "kLightStrengths and kLightDirections must have the same length");

//======================================================================================================================
// All catalog scenes use the same sky and directional-light rig.
AssetResult<void> attachSkyAndLights(rhi::Device& device, Scene& scene, std::string_view label) {
    // The sky pass recentres the sphere and forces it to the far plane; only enclosure matters.
    auto sphere = render::createMesh(device, render::fromGeo(makeSphere(0.5f, 20, 20)),
                                     std::string(label) + ".skySphere");
    if (!sphere) {
        return std::unexpected(uploadFailure(std::move(sphere.error())));
    }
    scene.skySphere = std::move(*sphere);

    constexpr std::array<uint8_t, 4> kNeutralSky = {149, 170, 196, 255};
    const rhi::TextureMip face{.data = kNeutralSky.data(), .bytesPerRow = 4};
    const std::array<rhi::TextureMip, 6> faces = {face, face, face, face, face, face};
    // The sRGB view decodes this authored display colour before lighting consumes it.
    auto cubemap = device.createTexture({.width = 1,
                                         .height = 1,
                                         .format = rhi::Format::RGBA8Unorm_sRGB,
                                         .kind = rhi::TextureKind::Cube,
                                         .mipLevels = 1,
                                         .sampled = true,
                                         .label = std::string(label) + ".sky"},
                                        faces);
    if (!cubemap) {
        return std::unexpected(uploadFailure(std::move(cubemap.error())));
    }
    scene.skyCubemap = std::move(*cubemap);

    for (size_t i = 0; i < std::size(kLightDirections); ++i) {
        scene.lights[i].direction = kLightDirections[i];
        scene.lights[i].strength = glm::vec3(srgbToLinear(kLightStrengths[i]));
    }

    // Authored ambient color crosses the sRGB-to-linear boundary at scene construction.
    scene.ambient = srgbToLinear(glm::vec3(0.25f, 0.25f, 0.35f));
    return {};
}

//======================================================================================================================
uint32_t mipLevelsFor(uint32_t width, uint32_t height) {
    uint32_t levels = 1;
    for (uint32_t extent = std::max(width, height); extent > 1; extent >>= 1) {
        ++levels;
    }
    return levels;
}

//======================================================================================================================
// Builds shared glTF scene resources and bounds; each catalog scene supplies its camera pose.
AssetResult<std::unique_ptr<Scene>> loadGltfBackedScene(rhi::Device& device,
                                                        std::string_view relativeAssetPath,
                                                        std::string_view sceneName) {
    const auto path = findRepoAsset(relativeAssetPath);
    if (!path) {
        return std::unexpected(missingAsset(sceneName, relativeAssetPath));
    }
    auto loaded = loadGltf(path->string());
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
        const GltfImage& image = gltfScene.images[index];
        if (image.width == 0 || image.height == 0 || image.rgba8.empty()) {
            return std::unexpected(
                AssetError{AssetErrorCode::Malformed,
                           std::string(sceneName) + " scene: referenced image was not decoded"});
        }
        const uint32_t mipLevels = mipLevelsFor(image.width, image.height);
        std::vector<rhi::TextureMip> mips(mipLevels);
        mips[0] = {.data = image.rgba8.data(), .bytesPerRow = uint64_t{image.width} * 4};
        // glTF image sources provide level zero; the GPU generates the remaining mip chain.
        auto texture = device.createTexture(
            {.width = image.width,
             .height = image.height,
             .format = srgb ? rhi::Format::RGBA8Unorm_sRGB : rhi::Format::RGBA8Unorm,
             .mipLevels = mipLevels,
             .sampled = true,
             .label = std::string(sceneName) + ".image" + std::to_string(index) +
                      (srgb ? ".srgb" : ".linear")},
            mips);
        if (!texture) {
            return std::unexpected(uploadFailure(std::move(texture.error())));
        }
        if (mipLevels > 1) {
            device.generateMipmaps(**texture);
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
        material.fresnelR0 = fresnelFromMetallic(src.baseColorFactor, src.metallic);
        material.roughness = src.roughness;
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

    glm::vec3 aabbMin{std::numeric_limits<float>::max()};
    glm::vec3 aabbMax{std::numeric_limits<float>::lowest()};
    scene->objects.reserve(gltfScene.instances.size());
    for (size_t i = 0; i < gltfScene.instances.size(); ++i) {
        const GltfInstance& instance = gltfScene.instances[i];
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

        if (instance.meshIndex >= gltfScene.meshes.size() ||
            instance.materialIndex >= scene->materials.size()) {
            return std::unexpected(
                AssetError{AssetErrorCode::Malformed,
                           std::string(sceneName) + " scene: instance index is out of range"});
        }
        const GeoData& mesh = gltfScene.meshes[instance.meshIndex];
        for (const VertexPNTU& v : mesh.vertices) {
            const glm::vec3 world = glm::vec3(instance.world * glm::vec4(v.px, v.py, v.pz, 1.0f));
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

    if (auto sky = attachSkyAndLights(device, *scene, sceneName); !sky) {
        return std::unexpected(sky.error());
    }

    return scene;
}

} // namespace

//======================================================================================================================
std::optional<DecomposedTransform> decomposeTransform(const glm::mat4& world) {
    glm::vec3 scale{1.f}, translation{0.f}, skew{0.f};
    glm::vec4 perspective{0.f};
    glm::quat orientation{1.f, 0.f, 0.f, 0.f};
    if (!glm::decompose(world, scale, orientation, translation, skew, perspective)) {
        return std::nullopt;
    }
    // Extraction must match SceneObject's Y-X-Z composition order to round-trip compound rotation.
    float yaw = 0.f, pitch = 0.f, roll = 0.f;
    glm::extractEulerAngleYXZ(glm::mat4_cast(orientation), yaw, pitch, roll);
    return DecomposedTransform{.position = translation,
                               .eulerDegrees = glm::degrees(glm::vec3(pitch, yaw, roll)),
                               .scale = scale};
}

//======================================================================================================================
glm::mat4 SceneObject::modelMatrix() const {
    glm::mat4 model = glm::translate(glm::mat4{1.0f}, position);
    model = glm::rotate(model, glm::radians(eulerDegrees.y), glm::vec3{0.0f, 1.0f, 0.0f});
    model = glm::rotate(model, glm::radians(eulerDegrees.x), glm::vec3{1.0f, 0.0f, 0.0f});
    model = glm::rotate(model, glm::radians(eulerDegrees.z), glm::vec3{0.0f, 0.0f, 1.0f});
    return glm::scale(model, scale);
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
                         .material = materials[object.materialIndex]});
    }

    render::SceneView sceneView;
    sceneView.items = items;
    for (size_t i = 0; i < std::size(sceneView.lights); ++i) {
        sceneView.lights[i] = lights[i];
    }
    sceneView.ambient = ambient;
    sceneView.boundingSphere = boundingSphere;
    // A cubemap marks a fully constructed sky; the sphere and cubemap are published together.
    if (skyCubemap != nullptr) {
        sceneView.skySphere = &skySphere;
        sceneView.skyCubemap = skyCubemap.get();
    }
    sceneView.shadowFilter = filter;
    sceneView.wireframe = wireframe;
    return sceneView;
}

//======================================================================================================================
AssetResult<std::unique_ptr<Scene>> loadSponzaScene(rhi::Device& device) {
    auto scene = loadGltfBackedScene(device, "Assets/Fetched/Sponza/Sponza.gltf", "Sponza");
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
    auto scene = loadGltfBackedScene(device, "Assets/Fetched/DamagedHelmet/DamagedHelmet.glb",
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

} // namespace lmx::engine
