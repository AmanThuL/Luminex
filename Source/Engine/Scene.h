#pragma once

#include "Engine/Asset.h"
#include "RHI/RHI.h"
#include "Render/Mesh.h"
#include "Render/Renderer.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lmx::engine {

struct SceneObject {
    std::string name;
    glm::vec3 position{0.f};
    glm::vec3 eulerDegrees{0.f};
    glm::vec3 scale{1.f};
    uint32_t meshIndex = 0;
    uint32_t materialIndex = 0;

    glm::mat4 modelMatrix() const;
};

// What decomposeTransform below extracts from a general 4x4.
struct DecomposedTransform {
    glm::vec3 position{0.f};
    glm::vec3 eulerDegrees{0.f};
    glm::vec3 scale{1.f};
};

std::optional<DecomposedTransform> decomposeTransform(const glm::mat4& world);

struct SceneCamera {
    glm::vec3 position;
    float yaw, pitch, fovY, nearZ, farZ;
};

class Scene {
public:
    std::string name;
    std::vector<render::Mesh> meshes;
    std::vector<std::unique_ptr<rhi::Texture>> textures;
    std::vector<render::Material> materials; // texture pointers reach into `textures`
    std::vector<SceneObject> objects;
    render::DirectionalLight lights[3];
    // Linear, like every other colour Engine hands to Render (Color.h's rule) -- attachSkyAndLights
    // (Scene.cpp) always overwrites this for a real scene, so the default here only keeps a bare,
    // never-built Scene well-defined rather than expressing an authored choice of its own: zero,
    // not a copy of the authored (0.25, 0.25, 0.35) triple Render.h's SceneView::ambient carries.
    glm::vec3 ambient{0.0f};
    glm::vec4 boundingSphere{0.f};
    render::Mesh skySphere;
    std::unique_ptr<rhi::Texture> skyCubemap;
    // Image-based lighting generated from the same authored sky radiance skyCubemap carries
    // (Engine/Ibl.h): a cosine-convolved irradiance cube, a GGX-prefiltered radiance chain, and the
    // split-sum DFG table. Published together with the sky by the scene-build path, so a scene that
    // has a skyCubemap has all three.
    std::unique_ptr<rhi::Texture> irradianceMap;
    std::unique_ptr<rhi::Texture> prefilteredEnvMap;
    std::unique_ptr<rhi::Texture> dfgLut;
    SceneCamera initialCamera{};

    // Fills `items` (cleared first, one DrawItem per object, in object order) and returns the
    // SceneView Render consumes this frame. `items` is caller-owned rather than a Scene member so
    // it can live on the App's per-frame stack -- render::Renderer::render() only needs the span
    // to outlive the one render() call that reads it.
    render::SceneView view(std::vector<render::DrawItem>& items, render::ShadowFilter filter,
                           bool wireframe) const;
};

// Crytek Sponza from the McGuire Computer Graphics Archive. `xmake setup` converts the pinned OBJ
// archive to core glTF; camera and bounding sphere are computed from the loaded AABB.
AssetResult<std::unique_ptr<Scene>> loadSponzaScene(rhi::Device& device);

// Khronos' DamagedHelmet sample (Assets/Fetched/DamagedHelmet, fetched by `xmake setup`). No
// floor -- a model showcase, floating near the origin.
AssetResult<std::unique_ptr<Scene>> loadHelmetScene(rhi::Device& device);

// Deterministic, fully code-generated diagnostic scene: a material sweep sphere grid, known-color
// patches, a horizontal gradient ramp, depth probes at known view distances, and a normal-map
// probe quad. No fetched assets -- always available, byte-identical across runs.
AssetResult<std::unique_ptr<Scene>> loadMaterialLabScene(rhi::Device& device);

} // namespace lmx::engine
