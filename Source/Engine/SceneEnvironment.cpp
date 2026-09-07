//----------------------------------------------------------------------------------------------------------------------
/// @file SceneEnvironment.cpp
/// @brief Implements the shared sky, image-based lighting, and directional-light rig.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/SceneEnvironment.h"

#include "Engine/Color.h"
#include "Engine/GeometryGenerator.h"
#include "Render/Mesh.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>

namespace lmx::engine {

namespace {

constexpr glm::vec3 kLightDirections[3] = {
    {0.577f, -0.577f, 0.577f},
    {-0.577f, -0.577f, 0.577f},
    {0.0f, -0.707f, -0.707f},
};

constexpr float kLightStrengths[3] = {0.7f, 0.2f, 0.2f};
static_assert(std::size(kLightStrengths) == std::size(kLightDirections),
              "kLightStrengths and kLightDirections must have the same length");

} // namespace

//======================================================================================================================
AssetResult<void> attachEnvironment(rhi::Device& device, Scene& scene,
                                    std::unique_ptr<rhi::Texture> skyCubemap,
                                    const ibl::CpuCubemap& environment, bool analyticLights,
                                    std::string_view label, ibl::GenerationOptions options) {
    // The sky pass recentres the sphere and forces it to the far plane; only enclosure matters.
    auto sphere = render::createMesh(device, render::fromGeo(makeSphere(0.5f, 20, 20)),
                                     std::string(label) + ".skySphere");
    if (!sphere) {
        return std::unexpected(
            AssetError{AssetErrorCode::UploadFailed, std::move(sphere.error().message)});
    }
    scene.skySphere = std::move(*sphere);
    scene.skyCubemap = std::move(skyCubemap);

    auto generated = ibl::generate(device, environment, label, options);
    if (!generated) {
        return std::unexpected(
            AssetError{AssetErrorCode::UploadFailed, std::move(generated.error().message)});
    }
    scene.irradianceMap = std::move(generated->irradiance);
    scene.prefilteredEnvMap = std::move(generated->prefilteredEnv);
    scene.dfgLut = std::move(generated->dfgLut);

    for (size_t i = 0; i < std::size(kLightDirections); ++i) {
        scene.lights[i].direction = kLightDirections[i];
        scene.lights[i].strength =
            analyticLights ? glm::vec3(srgbToLinear(kLightStrengths[i])) : glm::vec3(0.0f);
    }
    return {};
}

//======================================================================================================================
AssetResult<void> attachNeutralEnvironment(rhi::Device& device, Scene& scene,
                                           std::string_view label) {
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
        return std::unexpected(
            AssetError{AssetErrorCode::UploadFailed, std::move(cubemap.error().message)});
    }

    // One authored constant reaches both consumers: the sRGB texture view above decodes those
    // bytes on the GPU, and the same decode runs here so the generated IBL describes the sky the
    // renderer actually samples. Deriving it rather than reading the cube back keeps the two from
    // drifting apart.
    const glm::vec3 skyRadiance = srgbToLinear(glm::vec3(static_cast<float>(kNeutralSky[0]),
                                                         static_cast<float>(kNeutralSky[1]),
                                                         static_cast<float>(kNeutralSky[2])) /
                                               255.0f);
    return attachEnvironment(device, scene, std::move(*cubemap),
                             ibl::makeConstantCubemap(skyRadiance, 1), true, label);
}

} // namespace lmx::engine
