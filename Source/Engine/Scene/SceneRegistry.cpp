//----------------------------------------------------------------------------------------------------------------------
/// @file SceneRegistry.cpp
/// @brief Implements scene construction, resource and light registration, and lookup.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Scene/Scene.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Aabb.h"
#include "Engine/Lights/LocalLightMath.h"
#include "Engine/Scene/SceneStorage.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace lmx::engine {

//======================================================================================================================
Scene::Scene() : m_storage(std::make_unique<Storage>()) {}

//======================================================================================================================
Scene::~Scene() = default;

//======================================================================================================================
Scene::Scene(Scene&&) noexcept = default;

//======================================================================================================================
Scene& Scene::operator=(Scene&&) noexcept = default;

//======================================================================================================================
MeshId Scene::addMesh(engine::MeshData data, std::string_view label) {
    LMX_ASSERT(m_storage->device == nullptr, "geometry is immutable after finalize");
    LMX_ASSERT(!label.empty(), "mesh requires a label");
    LMX_ASSERT(data.vertices.size() <= std::numeric_limits<uint32_t>::max() &&
                   data.indices.size() <= std::numeric_limits<uint32_t>::max(),
               "mesh exceeds row range");
    for (uint32_t index : data.indices) {
        LMX_ASSERT(index < data.vertices.size(), "mesh index exceeds vertex count");
    }
    LMX_ASSERT(m_storage->meshRows.size() < std::numeric_limits<uint32_t>::max(),
               "mesh rows exhausted");
    const auto count = static_cast<uint32_t>(m_storage->meshRows.size());
    engine::MeshRow row{};
    if (count != 0) {
        const auto& previous = m_storage->meshRows.back();
        LMX_ASSERT(uint64_t{previous.firstVertex} + previous.vertexCount + data.vertices.size() <=
                           std::numeric_limits<uint32_t>::max() &&
                       uint64_t{previous.firstIndex} + previous.indexCount + data.indices.size() <=
                           std::numeric_limits<uint32_t>::max(),
                   "geometry pool exceeds row range");
        row.firstVertex = previous.firstVertex + previous.vertexCount;
        row.firstIndex = previous.firstIndex + previous.indexCount;
    }
    row.vertexCount = static_cast<uint32_t>(data.vertices.size());
    row.indexCount = static_cast<uint32_t>(data.indices.size());
    Aabb bounds = emptyAabb();
    bool finite = true;
    for (const auto& vertex : data.vertices) {
        const glm::vec3 point(vertex.px, vertex.py, vertex.pz);
        finite = finite && isFinite(point);
        expand(bounds, point);
    }
    bool hasSurface = false;
    for (size_t i = 0; i + 2 < data.indices.size(); i += 3) {
        const auto position = [&](uint32_t index) {
            const auto& vertex = data.vertices[index];
            return glm::dvec3(vertex.px, vertex.py, vertex.pz);
        };
        const glm::dvec3 area =
            glm::cross(position(data.indices[i + 1]) - position(data.indices[i]),
                       position(data.indices[i + 2]) - position(data.indices[i]));
        hasSurface = hasSurface || glm::dot(area, area) > 0.0;
    }
    if (finite && hasSurface && isValidAabb(bounds)) {
        row.boundsMin = bounds.minimum;
        row.boundsMax = bounds.maximum;
    }
    m_storage->meshRows.push_back(row);
    m_storage->meshData.push_back(std::move(data));
    return {count, 1, m_storage->store};
}

//======================================================================================================================
TextureId Scene::addTexture(std::unique_ptr<rojoRHI::Texture> texture) {
    LMX_ASSERT(texture != nullptr, "scene texture must not be null");
    const uint32_t slot = m_storage->textureIds.allocate();
    m_storage->textures.resize(m_storage->textureIds.size());
    m_storage->textures[slot] = std::move(texture);
    return {slot, m_storage->textureIds.generation(slot), m_storage->store};
}

//======================================================================================================================
MaterialId Scene::addMaterial(MaterialRecord material) {
    for (const auto id : {material.diffuse, material.normalMap, material.metallicRoughness,
                          material.occlusion, material.emissiveMap}) {
        LMX_ASSERT(!id || tryTexture(*id), "material texture identity is invalid");
    }
    LMX_ASSERT(m_storage->materials.size() < std::numeric_limits<uint32_t>::max(),
               "material rows exhausted");
    const auto slot = static_cast<uint32_t>(m_storage->materials.size());
    m_storage->materials.push_back(std::move(material));
    return {slot, 1, m_storage->store};
}

//======================================================================================================================
InstanceId Scene::addObject(SceneObject object) {
    LMX_ASSERT(tryMesh(object.mesh), "instance mesh identity is invalid");
    LMX_ASSERT(tryMaterial(object.material), "instance material identity is invalid");
    const uint32_t slot = m_storage->instances.allocate();
    object.id = {slot, m_storage->instances.generation(slot), m_storage->store};
    object.previousModel = object.modelMatrix();
    objects.push_back(std::move(object));
    LMX_ASSERT(m_storage->coverageEpoch < std::numeric_limits<uint64_t>::max(),
               "scene coverage epoch exhausted");
    ++m_storage->coverageEpoch;
    return objects.back().id;
}

//======================================================================================================================
void Scene::removeObject(InstanceId id) {
    LMX_ASSERT(tryObject(id), "instance identity is invalid");
    const auto found = std::ranges::find(objects, id, &SceneObject::id);
    const auto index = static_cast<uint32_t>(found - objects.begin());
    objects.erase(found);
    LMX_ASSERT(m_storage->coverageEpoch < std::numeric_limits<uint64_t>::max(),
               "scene coverage epoch exhausted");
    ++m_storage->coverageEpoch;
    m_storage->instances.release(id.slot);
    std::erase_if(animation.tracks,
                  [index](const auto& track) { return track.objectIndex == index; });
    std::erase_if(animation.emissiveTracks,
                  [index](const auto& track) { return track.objectIndex == index; });
    for (auto& track : animation.tracks) {
        track.objectIndex -= track.objectIndex > index ? 1 : 0;
    }
    for (auto& track : animation.emissiveTracks) {
        track.objectIndex -= track.objectIndex > index ? 1 : 0;
    }
}

//======================================================================================================================
void Scene::removeTexture(TextureId id) {
    LMX_ASSERT(tryTexture(id), "texture identity is invalid");
    for (const auto& material : m_storage->materials) {
        for (const auto reference :
             {material.diffuse, material.normalMap, material.metallicRoughness, material.occlusion,
              material.emissiveMap}) {
            LMX_ASSERT(!reference || *reference != id, "texture is still referenced by a material");
        }
    }
    LMX_ASSERT(m_storage->lastFrame <= std::numeric_limits<uint64_t>::max() - kSlots,
               "frame counter exhausted");
    m_storage->retiringTextures.emplace_back(m_storage->lastFrame + kSlots,
                                             std::move(m_storage->textures[id.slot]));
    m_storage->textureIds.release(id.slot);
}

//======================================================================================================================
rojoRHI::Result<LightId> Scene::addLight(const engine::LocalLight& light) {
    auto row = engine::makeLightRow(light);
    if (!row) {
        return std::unexpected(row.error());
    }
    auto& storage = *m_storage;
    if (storage.liveLightIds.size() >= engine::kMaxLocalLights) {
        return std::unexpected(
            rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc, "local light capacity exceeded"});
    }
    const uint32_t slot = storage.localLightIds.allocate();
    if (slot >= storage.localLightData.size()) {
        storage.localLightData.resize(slot + 1);
    }
    storage.localLightData[slot] = light;
    storage.enabledLightCount += light.enabled ? 1u : 0u;
    const LightId id{slot, storage.localLightIds.generation(slot), storage.store};
    storage.liveLightIds.insert(
        std::ranges::upper_bound(storage.liveLightIds, slot, {}, &LightId::slot), id);
    // The animation index list freezes at finalize (storage.device becomes non-null there): a
    // light added afterward -- a runtime pile addition, say -- gets no index and is static by
    // contract, so this list never grows once the scene is playable, regardless of how many
    // lights are later added and removed.
    if (storage.device == nullptr) {
        storage.lightCreationOrder.push_back(id);
    }
    return id;
}

//======================================================================================================================
bool Scene::removeLight(LightId id) {
    auto& storage = *m_storage;
    if (!resolves(id, storage.store, storage.localLightIds)) {
        return false;
    }
    storage.localLightIds.release(id.slot);
    storage.enabledLightCount -= storage.localLightData[id.slot].enabled ? 1u : 0u;
    std::erase(storage.liveLightIds, id);
    return true;
}

//======================================================================================================================
rojoRHI::Result<void> Scene::updateLight(LightId id, const engine::LocalLight& light) {
    auto& storage = *m_storage;
    if (!resolves(id, storage.store, storage.localLightIds)) {
        return std::unexpected(
            rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc, "light identity is invalid"});
    }
    auto row = engine::makeLightRow(light);
    if (!row) {
        return std::unexpected(row.error());
    }
    storage.enabledLightCount -= storage.localLightData[id.slot].enabled ? 1u : 0u;
    storage.enabledLightCount += light.enabled ? 1u : 0u;
    storage.localLightData[id.slot] = light;
    return {};
}

//======================================================================================================================
const engine::LocalLight* Scene::light(LightId id) const {
    const auto& storage = *m_storage;
    return resolves(id, storage.store, storage.localLightIds) ? &storage.localLightData[id.slot]
                                                              : nullptr;
}

//======================================================================================================================
uint32_t Scene::enabledLightCount() const {
    return m_storage->enabledLightCount;
}

//======================================================================================================================
std::span<const LightId> Scene::localLights() const {
    return m_storage->liveLightIds;
}

//======================================================================================================================
std::optional<LightId> Scene::animationLightId(uint32_t index) const {
    const auto& order = m_storage->lightCreationOrder;
    if (index >= order.size()) {
        return std::nullopt;
    }
    return order[index];
}

//======================================================================================================================
SceneObject* Scene::tryObject(InstanceId id) {
    return const_cast<SceneObject*>(std::as_const(*this).tryObject(id));
}

//======================================================================================================================
const SceneObject* Scene::tryObject(InstanceId id) const {
    if (!resolves(id, m_storage->store, m_storage->instances)) {
        return nullptr;
    }
    const auto found = std::ranges::find(objects, id, &SceneObject::id);
    return found == objects.end() ? nullptr : &*found;
}

//======================================================================================================================
const engine::MeshRow* Scene::tryMesh(MeshId id) const {
    return id.store == m_storage->store && id.generation == 1 &&
                   id.slot < m_storage->meshRows.size()
               ? &m_storage->meshRows[id.slot]
               : nullptr;
}

//======================================================================================================================
std::optional<Aabb> Scene::meshBounds(MeshId id) const {
    const auto* row = tryMesh(id);
    if (!row || !isValidAabb({row->boundsMin, row->boundsMax})) {
        return std::nullopt;
    }
    return Aabb{row->boundsMin, row->boundsMax};
}

//======================================================================================================================
MaterialRecord* Scene::tryMaterial(MaterialId id) {
    return const_cast<MaterialRecord*>(std::as_const(*this).tryMaterial(id));
}

//======================================================================================================================
const MaterialRecord* Scene::tryMaterial(MaterialId id) const {
    return id.store == m_storage->store && id.generation == 1 &&
                   id.slot < m_storage->materials.size()
               ? &m_storage->materials[id.slot]
               : nullptr;
}

//======================================================================================================================
rojoRHI::Texture* Scene::tryTexture(TextureId id) const {
    return resolves(id, m_storage->store, m_storage->textureIds)
               ? m_storage->textures[id.slot].get()
               : nullptr;
}

//======================================================================================================================
MaterialRecord& Scene::material(MaterialId id) {
    auto* result = tryMaterial(id);
    LMX_ASSERT(result, "material identity is invalid");
    return *result;
}

//======================================================================================================================
const MaterialRecord& Scene::material(MaterialId id) const {
    const auto* result = tryMaterial(id);
    LMX_ASSERT(result, "material identity is invalid");
    return *result;
}

} // namespace lmx::engine
