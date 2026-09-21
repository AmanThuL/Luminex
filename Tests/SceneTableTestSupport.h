#pragma once

#include "Engine/Scene/Scene.h"
#include "Render/SceneViewBuilder.h"
#include <catch2/catch_test_macros.hpp>
#include <deque>
#include <unordered_map>

namespace lmx::test {
using engine::AlphaMode;

// Authored test inputs retain the original oracle values; Scene owns the actual GPU tables.
struct FixtureSceneState;
struct FixtureMesh {
    engine::MeshData data;
    mutable std::vector<std::shared_ptr<FixtureSceneState>> retained;
};
inline rojoRHI::Result<FixtureMesh> fixtureMesh(rojoRHI::Device&, engine::MeshData data,
                                                std::string_view) {
    return FixtureMesh{.data = std::move(data), .retained = {}};
}

struct FixtureMaterial {
    /// Null means the renderer's white 1x1 fallback -- so `albedo` alone applies, rather than the
    /// draw silently reading an unbound texture slot.
    rojoRHI::Texture* diffuse = nullptr;
    /// Null means no normal mapping: the shader's flags bit 0 stays clear and its TBN path never
    /// runs. The slot is still bound (to a flat-normal 1x1) so nothing dereferences an empty one.
    rojoRHI::Texture* normalMap = nullptr;
    /// glTF 2.0 channel convention: roughness = G, metallic = B (R and A unused). Null means the
    /// shared white fallback, so `metallic`/`roughness` alone apply.
    rojoRHI::Texture* metallicRoughness = nullptr;
    /// glTF 2.0 channel convention: occlusion = R. Null means the shared white fallback (no
    /// occlusion). Attenuates the image-based terms only, never the analytic lights.
    rojoRHI::Texture* occlusion = nullptr;
    /// Null means the shared white fallback, so `emissive` alone applies.
    rojoRHI::Texture* emissiveMap = nullptr;
    /// The glTF base colour: linear, and the input the shader derives both the diffuse albedo and a
    /// metal's F0 from -- which is why there is no separate reflectance field to keep in step with
    /// it.
    glm::vec4 albedo{1.0f};
    /// Perceptual roughness, squared to the GGX alpha in the shader. The shader floors it at
    /// Lighting.slang's kMinRoughness, so a 0 here is a near-mirror rather than a singular lobe.
    float roughness = 0.5f;
    /// dielectric/metal mix. Deliberately diverges from glTF's material default of 1.0
    /// (GltfMaterial keeps that spec default, and Scene.cpp sets this explicitly for every glTF
    /// material): every non-glTF material here -- MaterialLab's sphere grid, known-color patches,
    /// ramp/probe materials, test materials -- relies on Material{} and never sets metallic, so a
    /// metallic default would render all of them as conductors.
    float metallic = 0.0f;
    /// glTF occlusion strength: 0 ignores the map and 1 applies it fully.
    float occlusionStrength = 1.0f;
    glm::vec3 emissive{0.0f};    ///< linear radiance the surface emits, added after all lighting
    glm::mat4 uvTransform{1.0f}; ///< Material UV transform applied before texture sampling.
    AlphaMode alphaMode = AlphaMode::Opaque; ///< Opaque or alpha-tested coverage.
    float alphaCutoff = 0.5f; ///< Nonnegative MASK threshold for texture alpha times albedo alpha.
    bool doubleSided = false; ///< MASK surfaces render both faces and reverse back-face normals.
};

struct FixtureDrawItem {
    const FixtureMesh* mesh = nullptr; ///< Borrowed mesh drawn by this item.
    glm::mat4 model{1.0f};             ///< Object-to-world transform.
    FixtureMaterial material;          ///< Material copied for this frame.
    /// The object-to-world transform this item was drawn with in the previous declared frame.
    /// Equal to `model` when the item has not moved, so a still object reprojects onto itself.
    glm::mat4 previousModel{1.0f};
    /// How this item's motion is produced; `Invalid` writes the motion sentinel instead of
    /// reprojecting through `previousModel`.
    engine::MotionClass motionClass = engine::MotionClass::Rigid;
};

struct FixtureSceneState {
    engine::Scene scene;
    std::vector<engine::DrawItem> draws;
    std::vector<const FixtureMesh*> meshes;
    const FixtureMesh* sky = nullptr;
    std::optional<engine::TextureId> normalPresence;
    /// Identities of `FixtureSceneView::localLights`, in the order they were authored, so a later
    /// frame re-authors the same rows rather than growing the table.
    std::vector<engine::LightId> lights;
};

// Each view retains its production scene until the test drains the GPU. Changes use the real
// paced table path. Original texture probes keep ownership in the test and replace only the
// resolved draw bindings; a scene-owned normal marker preserves the tested normal-map flag.
struct FixtureSceneView : render::SceneView {
    std::span<const FixtureDrawItem> items;
    /// Local point and spot lights authored into the fixture scene before finalize. Their count is
    /// part of the rebuild key, so a view handed a different number of lights builds a new scene
    /// rather than leaving stale rows behind.
    std::span<const engine::LocalLight> localLights;
    const FixtureMesh* skySphere = nullptr;
    mutable std::shared_ptr<FixtureSceneState> state;
    mutable std::deque<std::pair<uint64_t, std::shared_ptr<FixtureSceneState>>> retired;
    bool production = false;

    FixtureSceneView() = default;
    FixtureSceneView(const render::SceneView& view) : render::SceneView(view), production(true) {}

    render::SceneView prepare(rojoRHI::Device& device) const {
        if (production)
            return static_cast<const render::SceneView&>(*this);
        const uint64_t frame = device.frameNumber();
        while (!retired.empty() && retired.front().first <= frame)
            retired.pop_front();
        bool rebuild = !state || state->meshes.size() != items.size() || state->sky != skySphere ||
                       state->lights.size() != localLights.size();
        if (!rebuild) {
            for (size_t i = 0; i < items.size(); ++i)
                rebuild |= state->meshes[i] != items[i].mesh;
        }
        if (rebuild) {
            if (state)
                retired.emplace_back(frame + 3, std::move(state));
            state = std::make_shared<FixtureSceneState>();
            state->sky = skySphere;
            if (!items.empty())
                items.front().mesh->retained.push_back(state);
            else if (skySphere)
                skySphere->retained.push_back(state);
            std::unordered_map<const FixtureMesh*, engine::MeshId> meshIds;
            auto meshId = [&](const FixtureMesh* mesh) {
                REQUIRE(mesh != nullptr);
                if (!meshIds.contains(mesh))
                    meshIds.emplace(mesh,
                                    state->scene.addMesh(mesh->data, "lmx.test.scene.geometry"));
                return meshIds.at(mesh);
            };
            const uint32_t normal = 0xffff8080u;
            rojoRHI::TextureMip mip{.data = &normal, .bytesPerRow = 4};
            auto marker = device.createTexture({.width = 1,
                                                .height = 1,
                                                .format = rojoRHI::Format::RGBA8Unorm,
                                                .sampled = true,
                                                .label = "lmx.test.scene.normalPresence"},
                                               std::span{&mip, 1});
            REQUIRE(marker);
            state->normalPresence = state->scene.addTexture(std::move(*marker));
            for (const auto& item : items) {
                const auto material = state->scene.addMaterial({});
                state->scene.addObject({.mesh = meshId(item.mesh), .material = material});
                state->meshes.push_back(item.mesh);
            }
            for (const auto& light : localLights) {
                auto id = state->scene.addLight(light);
                INFO((id ? "" : id.error().message));
                REQUIRE(id);
                state->lights.push_back(*id);
            }
            if (skySphere)
                state->scene.skySphere = meshId(skySphere);
            auto result = state->scene.finalize(device);
            INFO((result ? "" : result.error().message));
            REQUIRE(result);
        }
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& input = items[i];
            auto pose = asset::decomposeTransform(input.model);
            REQUIRE(pose);
            auto& object = state->scene.objects[i];
            object.position = pose->position;
            object.eulerDegrees = pose->eulerDegrees;
            object.scale = pose->scale;
            object.previousModel = input.previousModel;
            object.motionClass = input.motionClass;
            auto& material = state->scene.material(object.material);
            material.albedo = input.material.albedo;
            material.roughness = input.material.roughness;
            material.metallic = input.material.metallic;
            material.occlusionStrength = input.material.occlusionStrength;
            material.emissive = input.material.emissive;
            material.uvTransform = input.material.uvTransform;
            material.alphaMode = input.material.alphaMode;
            material.alphaCutoff = input.material.alphaCutoff;
            material.doubleSided = input.material.doubleSided;
            material.normalMap = input.material.normalMap ? state->normalPresence : std::nullopt;
        }
        // Re-authored every frame, like the draw items above, so a fixture may move or recolour a
        // light between frames without rebuilding its scene.
        for (size_t i = 0; i < localLights.size(); ++i) {
            auto updated = state->scene.updateLight(state->lights[i], localLights[i]);
            INFO((updated ? "" : updated.error().message));
            REQUIRE(updated);
        }
        auto prepared = state->scene.prepareFrame(frame);
        INFO((prepared ? "" : prepared.error().message));
        REQUIRE(prepared);
        const auto sceneView =
            render::buildSceneView(state->scene, state->draws, shadowFilter, wireframe);
        render::SceneView view = static_cast<const render::SceneView&>(*this);
        view.tables = sceneView.tables;
        view.items = state->draws;
        view.skySphere = skySphere ? std::optional{*state->scene.tryMesh(*state->scene.skySphere)}
                                   : std::nullopt;
        for (size_t i = 0; i < items.size(); ++i) {
            auto& draw = state->draws[i];
            const auto& material = items[i].material;
            draw.diffuse = material.diffuse;
            draw.normalMap = material.normalMap;
            draw.metallicRoughness = material.metallicRoughness;
            draw.occlusion = material.occlusion;
            draw.emissiveMap = material.emissiveMap;
        }
        return view;
    }
};
inline render::SceneView prepareSceneView(const FixtureSceneView& view, rojoRHI::Device& device) {
    return view.prepare(device);
}
inline render::SceneView prepareSceneView(const render::SceneView& view, rojoRHI::Device&) {
    return view;
}
template <typename DeviceOwner>
render::SceneView prepareSceneView(const FixtureSceneView& view, DeviceOwner& device) {
    return view.prepare(**device);
}
template <typename DeviceOwner>
render::SceneView prepareSceneView(const render::SceneView& view, DeviceOwner&) {
    return view;
}
} // namespace lmx::test
