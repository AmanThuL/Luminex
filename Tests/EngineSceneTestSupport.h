#pragma once

#include "Scene/IblUpload.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Asset/SceneAnimation.h"
#include "Asset/TextureBake.h"
#include "Asset/Transform.h"
#include "BrdfOracle.h"
#include "Core/Color.h"
#include "DisplayTransformOracle.h"
#include "EngineTestSupport.h"
#include "GpuTestSupport.h"
#include <rojoRHI/RHI.h>
#include "Render/Camera.h"
#include "Render/Mesh.h"
#include "Render/Renderer.h"
#include "Scene/Scene.h"
#include "Scene/SceneLibrary.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

using namespace lmx::asset;
using lmx::srgbToLinear;
using namespace lmx::scene;
using lmx::test::findRepoAsset;
using lmx::test::near3;
namespace rhi = rojoRHI;
namespace render = lmx::render;

namespace {

//======================================================================================================================
template <typename T>
inline std::string describeSceneError(const AssetResult<T>& result) {
    return result.has_value() ? std::string{} : result.error().message;
}

//======================================================================================================================
template <typename T>
inline std::string describeSceneError(const rojoRHI::Result<T>& result) {
    return result.has_value() ? std::string{} : result.error().message;
}

//======================================================================================================================
// Compare all matrix elements so transform tests pin the complete round-trip.
inline bool matricesNear(const glm::mat4& a, const glm::mat4& b, float margin) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            if (std::abs(a[col][row] - b[col][row]) > margin) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

namespace {

// Renames a fetched directory aside for the scope's lifetime to force an asset fallback, then
// restores it on destruction.
class TemporarilyHiddenDirectory {
public:
    //==================================================================================================================
    explicit TemporarilyHiddenDirectory(std::filesystem::path directory)
        : m_original(std::move(directory)), m_hidden(m_original.string() + ".hidden-for-test") {
        if (std::filesystem::exists(m_original)) {
            std::filesystem::rename(m_original, m_hidden);
            m_renamed = true;
        }
    }

    //==================================================================================================================
    ~TemporarilyHiddenDirectory() {
        if (m_renamed) {
            std::filesystem::rename(m_hidden, m_original);
        }
    }

    //==================================================================================================================
    TemporarilyHiddenDirectory(const TemporarilyHiddenDirectory&) = delete;
    //==================================================================================================================
    TemporarilyHiddenDirectory& operator=(const TemporarilyHiddenDirectory&) = delete;

private:
    std::filesystem::path m_original, m_hidden;
    bool m_renamed = false;
};

//======================================================================================================================
// Renders one texture's exact mip level 1 into a 64x64 target via Shaders/SamplerSmoke.slang's
// SampleLevel-forcing entry point, independent of geometry or camera -- the same oracle the
// deleted generateMipmaps GPU test used, repurposed here to compare two textures' mip content
// directly.
inline std::vector<uint8_t> readMipLevel1(rojoRHI::Device& device, rojoRHI::Texture& texture) {
    auto destination = makeProbeTarget(device, "lmx.test.fallbackMipDestination");
    REQUIRE(destination.has_value());
    auto library = device.loadShaderLibrary("Shaders/SamplerSmoke");
    REQUIRE(library.has_value());
    auto pipeline = device.createGraphicsPipeline({.library = library->get(),
                                                   .vertexEntry = "vertexMain",
                                                   .fragmentEntry = "fragmentMipLevel1",
                                                   .colorFormat = rojoRHI::Format::BGRA8Unorm,
                                                   .label = "lmx.test.fallbackMipPipeline"});
    REQUIRE(pipeline.has_value());
    auto sampler = device.createSampler(
        {.addressMode = rojoRHI::AddressMode::Clamp, .label = "lmx.test.fallbackMipSampler"});
    REQUIRE(sampler.has_value());
    return renderSampledImage(device, **pipeline, /*textureSlot=*/0, texture, **sampler,
                              **destination);
}

} // namespace

namespace {

//======================================================================================================================
inline const SceneObject* findObject(const Scene& scene, std::string_view name) {
    for (const SceneObject& object : scene.objects) {
        if (object.name == name) {
            return &object;
        }
    }
    return nullptr;
}

} // namespace

namespace {

struct ScreenBox {
    float minX, maxX, minY, maxY;
};

//======================================================================================================================
// Projects a world-space AABB's 8 corners through the camera and returns the enclosing
// screen-space box, in the same pixel convention as projectScenePixel above (row 0 = top) but
// without truncating to an integer pixel -- a partly off-screen box must stay readable as such
// rather than wrapping through uint32_t.
inline ScreenBox projectAabbToScreen(const render::Camera& camera, uint32_t size,
                                     const glm::vec3& center, const glm::vec3& halfExtent) {
    ScreenBox box{std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                  std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest()};
    const glm::mat4 viewProj = camera.projectionMatrix(1.0f) * camera.viewMatrix();
    for (float sx : {-1.0f, 1.0f}) {
        for (float sy : {-1.0f, 1.0f}) {
            for (float sz : {-1.0f, 1.0f}) {
                const glm::vec3 corner = center + glm::vec3(sx, sy, sz) * halfExtent;
                const glm::vec4 clip = viewProj * glm::vec4(corner, 1.0f);
                const float ndcX = clip.x / clip.w;
                const float ndcY = clip.y / clip.w;
                const float px = (ndcX * 0.5f + 0.5f) * static_cast<float>(size);
                const float py = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(size);
                box.minX = std::min(box.minX, px);
                box.maxX = std::max(box.maxX, px);
                box.minY = std::min(box.minY, py);
                box.maxY = std::max(box.maxY, py);
            }
        }
    }
    return box;
}

//======================================================================================================================
inline bool disjoint(const ScreenBox& a, const ScreenBox& b) {
    return a.maxX < b.minX || b.maxX < a.minX || a.maxY < b.minY || b.maxY < a.minY;
}

//======================================================================================================================
inline bool insideFrame(const ScreenBox& box, float size) {
    return box.minX >= 0.0f && box.maxX <= size && box.minY >= 0.0f && box.maxY <= size;
}

} // namespace

namespace {

//======================================================================================================================
inline render::MeshData makeUvQuad(float halfExtent) {
    render::MeshData mesh;
    struct Corner {
        float x, y, u, v;
    };
    constexpr Corner kCorners[4] = {
        {-1.0f, -1.0f, 0.0f, 1.0f},
        {1.0f, -1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 0.0f},
        {-1.0f, 1.0f, 0.0f, 0.0f},
    };
    for (const Corner& c : kCorners) {
        mesh.vertices.push_back({c.x * halfExtent, c.y * halfExtent, 0.0f, // position
                                 0.0f, 0.0f, 1.0f,                         // normal: +Z
                                 1.0f, 0.0f, 0.0f, 1.0f,                   // tangent
                                 c.u, c.v});
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

struct ProjectedPixel {
    uint32_t x = 0, y = 0;
};

//======================================================================================================================
// Same clip -> pixel convention Tests/GpuSmokeTests.cpp's own projectToPixel uses: row 0 is the
// top of a readback, so a clip-space +y maps to a small row index.
inline ProjectedPixel projectScenePixel(const render::Camera& camera, uint32_t size,
                                        const glm::vec3& world) {
    const glm::vec4 clip =
        camera.projectionMatrix(1.0f) * camera.viewMatrix() * glm::vec4(world, 1.0f);
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    const float px = (ndcX * 0.5f + 0.5f) * static_cast<float>(size);
    const float py = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(size);
    return {static_cast<uint32_t>(px), static_cast<uint32_t>(py)};
}

} // namespace

namespace {

//======================================================================================================================
// CPU-owned identities support playback before any GPU allocation.
inline Scene makeMotionTestScene() {
    Scene scene;
    const MeshId mesh = scene.addMesh(render::makeCube(), "lmx.test.motionCube");
    const MaterialId material = scene.addMaterial({});
    scene.addObject({.name = "object",
                     .position = glm::vec3(1.0f, 0.0f, 0.0f),
                     .mesh = mesh,
                     .material = material});
    scene.resetMotion();
    return scene;
}

} // namespace

namespace {

//======================================================================================================================
// Projects a world-space AABB's 8 corners into a `width` by `height` frame, using that frame's own
// aspect ratio rather than the square one projectAabbToScreen assumes -- TemporalLab is authored
// for the editor's wide viewport.
inline ScreenBox projectAabbToFrame(const render::Camera& camera, uint32_t width, uint32_t height,
                                    const glm::vec3& center, const glm::vec3& halfExtent) {
    ScreenBox box{std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                  std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest()};
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const glm::mat4 viewProj = camera.projectionMatrix(aspect) * camera.viewMatrix();
    for (float sx : {-1.0f, 1.0f}) {
        for (float sy : {-1.0f, 1.0f}) {
            for (float sz : {-1.0f, 1.0f}) {
                const glm::vec3 corner = center + glm::vec3(sx, sy, sz) * halfExtent;
                const glm::vec4 clip = viewProj * glm::vec4(corner, 1.0f);
                const float px = (clip.x / clip.w * 0.5f + 0.5f) * static_cast<float>(width);
                const float py =
                    (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * static_cast<float>(height);
                box.minX = std::min(box.minX, px);
                box.maxX = std::max(box.maxX, px);
                box.minY = std::min(box.minY, py);
                box.maxY = std::max(box.maxY, py);
            }
        }
    }
    return box;
}

//======================================================================================================================
inline render::Camera cameraFrom(const SceneCamera& authored) {
    render::Camera camera;
    camera.position = authored.position;
    camera.yaw = authored.yaw;
    camera.pitch = authored.pitch;
    camera.fovY = authored.fovY;
    camera.nearZ = authored.nearZ;
    camera.farZ = authored.farZ;
    return camera;
}

} // namespace
