//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentExport.cpp
/// @brief Derives persistent edits from a live scene without changing loaded or session state.
//----------------------------------------------------------------------------------------------------------------------

#include "Scenes/SceneDocumentExport.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Aabb.h"
#include "Engine/Asset/Document/Orientation.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace lmx::scenes {
namespace {

//======================================================================================================================
bool same(float a, float b) {
    return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b);
}

//======================================================================================================================
bool same(glm::vec3 a, glm::vec3 b) {
    return same(a.x, b.x) && same(a.y, b.y) && same(a.z, b.z);
}

//======================================================================================================================
bool same(const asset::ObjectPose& a, const asset::ObjectPose& b) {
    return same(a.translation, b.translation) && same(a.eulerDegrees, b.eulerDegrees) &&
           same(a.scale, b.scale);
}

//======================================================================================================================
asset::ObjectPose objectPose(const engine::SceneObject& object) {
    return {object.position, object.eulerDegrees, object.scale};
}

//======================================================================================================================
void note(ExportReport* report, uint32_t node, std::string what) {
    if (report)
        report->approximations.push_back({node, std::move(what)});
}

//======================================================================================================================
asset::AssetResult<void> replaceRotation(asset::DocNode& node, glm::vec3 direction, uint32_t index,
                                         std::string_view what, ExportReport* report) {
    if (same(asset::directionForRotation(node.rotation), direction))
        return {};
    if (const auto exact = asset::exactRotationForDirection(direction)) {
        node.rotation = *exact;
        return {};
    }
    if (!isFinite(direction) || std::abs(glm::dot(direction, direction) - 1.0f) > 1e-5f)
        return std::unexpected(asset::AssetError{
            asset::AssetErrorCode::Unsupported,
            "/nodes/" + std::to_string(index) +
                "/rotation: light direction must be a finite unit vector to convert exactly"});
    node.rotation = asset::rotationForDirection(direction);
    note(report, index, std::string(what));
    return {};
}

//======================================================================================================================
asset::AssetResult<void> exportImported(asset::SceneDocument& doc,
                                        const engine::SceneBinding& binding,
                                        const engine::Scene& scene,
                                        const SessionDocumentState& state) {
    LMX_ASSERT(state.importedEnabled.size() == binding.importedNodes.size() &&
                   state.importedPoseBaseline.size() == binding.importedNodes.size(),
               "export requires the matching imported state captured before editing");
    for (size_t i = 0; i < binding.importedNodes.size(); ++i) {
        const auto& imported = binding.importedNodes[i];
        auto& node = doc.nodes.at(imported.assetRoot);
        LMX_ASSERT(node.asset, "imported binding must reference a document asset root");
        auto found =
            std::ranges::find(node.overrides, imported.sourceNode, &asset::DocOverride::node);
        const bool existing = found != node.overrides.end();
        asset::DocOverride value =
            existing ? *found
                     : asset::DocOverride{.node = imported.sourceNode, .name = imported.name};
        bool changed = false;
        if (value.enabled.value_or(true) != state.importedEnabled[i]) {
            value.enabled = state.importedEnabled[i];
            changed = true;
        }
        if (!imported.animated && !imported.objects.empty()) {
            const auto pose = objectPose(scene.objects.at(imported.objects.front()));
            const std::string pointer = "/nodes/" + std::to_string(imported.assetRoot) +
                                        "/extensions/LMX_scene/overrides (source node " +
                                        std::to_string(imported.sourceNode) + ")/pose";
            if (!isFinite(pose.translation) || !isFinite(pose.eulerDegrees) ||
                !isFinite(pose.scale))
                return std::unexpected(asset::AssetError{asset::AssetErrorCode::Malformed,
                                                         pointer + ": pose must be finite"});
            for (size_t object : imported.objects)
                if (!same(pose, objectPose(scene.objects.at(object))))
                    return std::unexpected(asset::AssetError{
                        asset::AssetErrorCode::Unsupported,
                        pointer + ": primitives of one source node have different world poses"});
            const auto& baseline = value.pose ? value.pose : state.importedPoseBaseline[i];
            LMX_ASSERT(baseline, "static imported objects require a captured pose baseline");
            if (!same(pose, *baseline)) {
                value.pose = pose;
                changed = true;
            }
        }
        if (changed) {
            if (existing)
                *found = std::move(value);
            else
                node.overrides.push_back(std::move(value));
        }
    }
    return {};
}

//======================================================================================================================
asset::AssetResult<bool> exportDirectional(asset::DocNode& node, asset::DocLight& saved,
                                           const engine::DirectionalLight& light, uint32_t index,
                                           ExportReport* report) {
    if (auto rotation =
            replaceRotation(node, light.direction, index, "directional light direction", report);
        !rotation)
        return std::unexpected(rotation.error());
    if (same(asset::decodeStrength({saved.colour, saved.intensity}), light.strength))
        return false;
    if (!isFinite(light.strength) || glm::any(glm::lessThan(light.strength, glm::vec3(0))))
        return std::unexpected(asset::AssetError{
            asset::AssetErrorCode::Malformed,
            "/nodes/" + std::to_string(index) +
                "/extensions/KHR_lights_punctual: strength must be finite and nonnegative"});
    const auto encoded = asset::encodeStrength(light.strength);
    saved.colour = encoded.colour;
    saved.intensity = encoded.intensity;
    return true;
}

//======================================================================================================================
asset::AssetResult<bool> exportLocal(asset::DocNode& node, asset::DocLight& saved,
                                     const engine::LocalLight& light, uint32_t index,
                                     ExportReport* report) {
    node.translation = light.position;
    if (light.type == engine::LocalLightType::Spot)
        if (auto rotation =
                replaceRotation(node, light.direction, index, "spot light direction", report);
            !rotation)
            return std::unexpected(rotation.error());
    bool changed = false;
    const auto type = light.type == engine::LocalLightType::Spot ? asset::DocLightType::Spot
                                                                 : asset::DocLightType::Point;
    if (saved.type != type) {
        saved.type = type;
        changed = true;
    }
    for (int component = 0; component < 3; ++component)
        if (!same(static_cast<float>(saved.colour[component]), light.colour[component])) {
            saved.colour[component] = light.colour[component];
            changed = true;
        }
    if (!same(static_cast<float>(saved.intensity), light.intensity)) {
        saved.intensity = light.intensity;
        changed = true;
    }
    if (!saved.range || !same(*saved.range, light.range)) {
        saved.range = light.range;
        changed = true;
    }
    if (type == asset::DocLightType::Spot) {
        if (!same(saved.innerCone, light.innerCone)) {
            saved.innerCone = light.innerCone;
            changed = true;
        }
        if (!same(saved.outerCone, light.outerCone)) {
            saved.outerCone = light.outerCone;
            changed = true;
        }
    }
    return changed;
}

//======================================================================================================================
asset::AssetResult<void> exportLights(asset::SceneDocument& doc,
                                      const engine::SceneBinding& binding,
                                      const engine::Scene& scene, ExportReport* report) {
    std::vector<size_t> references(doc.lights.size());
    for (const auto& node : doc.nodes)
        if (node.light)
            ++references.at(*node.light);
    for (uint32_t n = 0; n < binding.nodes.size(); ++n) {
        const auto& bound = binding.nodes[n];
        if (!bound.directional && !bound.light)
            continue;
        auto& node = doc.nodes[n];
        LMX_ASSERT(node.light, "bound light requires a document definition");
        auto light = doc.lights.at(*node.light);
        asset::AssetResult<bool> changed;
        if (bound.directional) {
            changed = exportDirectional(node, light, scene.lights[*bound.directional], n, report);
        } else {
            const auto* live = scene.light(*bound.light);
            LMX_ASSERT(live, "bound local light must retain its live identity");
            changed = exportLocal(node, light, *live, n, report);
        }
        if (!changed)
            return std::unexpected(changed.error());
        if (!*changed)
            continue;
        if (references[*node.light] > 1) {
            node.light = static_cast<uint32_t>(doc.lights.size());
            doc.lights.push_back(std::move(light));
        } else {
            doc.lights[*node.light] = std::move(light);
        }
    }
    return {};
}

//======================================================================================================================
asset::AssetResult<void> exportCamera(asset::SceneDocument& doc, const engine::SceneCamera& camera,
                                      ExportReport* report) {
    auto& node = doc.nodes.at(doc.camera);
    LMX_ASSERT(node.camera, "saved scene camera must reference a lens");
    const auto angles = asset::cameraAnglesForRotation(node.rotation, 0);
    // The document decodes yaw in [-pi, pi], so any full-turn offset is dropped before matching.
    if (!std::isfinite(camera.yaw) || !std::isfinite(camera.pitch) ||
        std::abs(camera.pitch) > glm::half_pi<float>())
        return std::unexpected(asset::AssetError{
            asset::AssetErrorCode::Unsupported,
            "/nodes/" + std::to_string(doc.camera) +
                "/rotation: saved camera yaw and pitch must be finite with |pitch| <= pi/2 to "
                "convert exactly"});
    const float yaw = asset::unwrapYaw(0.0f, camera.yaw);
    if (!same(angles.x, yaw) || !same(angles.y, camera.pitch)) {
        if (const auto exact = asset::exactRotationForCamera(yaw, camera.pitch, 0)) {
            node.rotation = *exact;
        } else {
            node.rotation = asset::rotationForCamera(yaw, camera.pitch);
            note(report, doc.camera, "scene camera");
        }
    }
    node.translation = camera.position;
    auto lens = doc.cameras.at(*node.camera);
    const float farZ = lens.farZ.value_or(std::numeric_limits<float>::infinity());
    if (same(lens.fovY, camera.fovY) && same(lens.nearZ, camera.nearZ) && same(farZ, camera.farZ))
        return {};
    lens.fovY = camera.fovY;
    lens.nearZ = camera.nearZ;
    lens.farZ = camera.farZ == std::numeric_limits<float>::infinity() ? std::optional<float>{}
                                                                      : camera.farZ;
    const auto references = std::ranges::count_if(
        doc.nodes, [&](const auto& candidate) { return candidate.camera == node.camera; });
    if (references > 1) {
        node.camera = static_cast<uint32_t>(doc.cameras.size());
        doc.cameras.push_back(std::move(lens));
    } else {
        doc.cameras[*node.camera] = std::move(lens);
    }
    return {};
}

} // namespace

//======================================================================================================================
asset::AssetResult<asset::SceneDocument> exportSceneDocument(const engine::LoadedScene& loaded,
                                                             const engine::Scene& scene,
                                                             const SessionDocumentState& state,
                                                             ExportReport* report) {
    LMX_ASSERT(loaded.scene.get() == &scene, "export scene must belong to the loaded snapshot");
    auto doc = loaded.document;
    LMX_ASSERT(state.nodeEnabled.size() == doc.nodes.size() &&
                   loaded.binding.nodes.size() == doc.nodes.size(),
               "export requires matching document state and bindings");
    doc.look = scene.look;
    for (size_t n = 0; n < doc.nodes.size(); ++n)
        doc.nodes[n].enabled = state.nodeEnabled[n];
    if (auto imported = exportImported(doc, loaded.binding, scene, state); !imported)
        return std::unexpected(imported.error());
    if (auto lights = exportLights(doc, loaded.binding, scene, report); !lights)
        return std::unexpected(lights.error());
    if (state.sceneCamera)
        if (auto camera = exportCamera(doc, *state.sceneCamera, report); !camera)
            return std::unexpected(camera.error());
    // Nonfinite look or lens values would abort canonical formatting; report them like the rest.
    if (auto valid = asset::validateSceneDocumentModel(doc); !valid)
        return std::unexpected(valid.error());
    return doc;
}

//======================================================================================================================
bool documentDirty(const asset::SceneDocument& loaded, const asset::SceneDocument& exported) {
    return asset::sceneDocumentJson(loaded, "scene.bin") !=
               asset::sceneDocumentJson(exported, "scene.bin") ||
           asset::sceneDocumentBuffer(loaded) != asset::sceneDocumentBuffer(exported);
}

} // namespace lmx::scenes
