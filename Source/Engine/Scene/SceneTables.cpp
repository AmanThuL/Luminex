//----------------------------------------------------------------------------------------------------------------------
/// @file SceneTables.cpp
/// @brief Implements scene identities, pooled geometry and paced table updates.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Scene/Scene.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Engine/Types/LocalLightMath.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <utility>

namespace lmx::engine {
namespace {
constexpr uint32_t kSlots = 3;
constexpr uint8_t kAllSlots = 7;
constexpr uint32_t kMinimumCapacity = 4;

struct IdentitySlot {
    uint16_t generation = 1;
    bool live = true;
};

//======================================================================================================================
uint16_t nextStore() {
    static std::atomic<uint32_t> next{1};
    uint32_t value = next.load(std::memory_order_relaxed);
    do {
        LMX_ASSERT(value <= std::numeric_limits<uint16_t>::max(),
                   "scene store identities exhausted");
    } while (!next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed));
    return static_cast<uint16_t>(value);
}

//======================================================================================================================
uint32_t allocateIdentity(std::vector<IdentitySlot>& slots, uint32_t& searchStart) {
    for (uint32_t i = searchStart; i < slots.size(); ++i) {
        if (!slots[i].live && slots[i].generation != std::numeric_limits<uint16_t>::max()) {
            slots[i].live = true;
            searchStart = i + 1;
            return i;
        }
    }
    LMX_ASSERT(slots.size() < std::numeric_limits<uint32_t>::max(),
               "scene row identities exhausted");
    slots.push_back({});
    searchStart = static_cast<uint32_t>(slots.size());
    return searchStart - 1;
}

//======================================================================================================================
template <typename Id>
bool resolves(Id id, uint16_t store, const std::vector<IdentitySlot>& slots) {
    return id.store == store && id.slot < slots.size() && slots[id.slot].live &&
           slots[id.slot].generation == id.generation;
}

struct MaterialCoverage {
    std::optional<TextureId> diffuse;
    glm::mat4 uvTransform{1.0f};
    float alpha = 1.0f;
    float cutoff = 0.5f;
    engine::AlphaMode mode = engine::AlphaMode::Opaque;
    bool doubleSided = false;

    //==================================================================================================================
    bool operator==(const MaterialCoverage&) const = default;
};

struct RetiringBuffer {
    std::unique_ptr<rojoRHI::Buffer> buffer;
    uint64_t releaseAtFrame = 0;
};

template <typename Row>
struct PacedTable {
    std::array<std::unique_ptr<rojoRHI::Buffer>, kSlots> buffers;
    std::vector<Row> shadow;
    std::vector<uint8_t> dirty;
    uint32_t capacity = 0;
};

//======================================================================================================================
template <typename Row>
rojoRHI::Result<void> reserveTable(rojoRHI::Device& device, PacedTable<Row>& table, uint32_t count,
                                   std::string_view label, uint64_t lastFrame,
                                   std::vector<RetiringBuffer>& retiring, uint64_t& growthEvents) {
    if (count <= table.capacity && table.capacity != 0) {
        return {};
    }
    uint32_t capacity = std::max(table.capacity, kMinimumCapacity);
    while (capacity < count) {
        LMX_ASSERT(capacity <= std::numeric_limits<uint32_t>::max() / 2,
                   "scene capacity exhausted");
        capacity *= 2;
    }
    std::array<std::unique_ptr<rojoRHI::Buffer>, kSlots> buffers;
    for (uint32_t slot = 0; slot < kSlots; ++slot) {
        auto buffer = device.createBuffer({.size = uint64_t{capacity} * sizeof(Row),
                                           .storageRead = true,
                                           .cpuReadback = true,
                                           .cpuWrite = true,
                                           .label = std::format("{}.{}", label, slot)},
                                          nullptr);
        if (!buffer) {
            return std::unexpected(buffer.error());
        }
        buffers[slot] = std::move(*buffer);
    }
    if (table.capacity != 0) {
        LMX_ASSERT(lastFrame <= std::numeric_limits<uint64_t>::max() - kSlots,
                   "frame counter exhausted");
        for (auto& buffer : table.buffers) {
            retiring.push_back({std::move(buffer), lastFrame + kSlots});
        }
        ++growthEvents;
        LMX_LOG_INFO("{} grew from {} to {} rows; old buffers retire at frame {}", label,
                     table.capacity, capacity, lastFrame + kSlots);
    }
    table.buffers = std::move(buffers);
    table.capacity = capacity;
    table.shadow.resize(count);
    table.dirty.assign(count, kAllSlots);
    return {};
}

//======================================================================================================================
template <typename Row>
void updateRow(PacedTable<Row>& table, uint32_t index, const Row& row) {
    if (index >= table.shadow.size()) {
        table.shadow.resize(index + 1);
        table.dirty.resize(index + 1, kAllSlots);
    }
    if (std::memcmp(&table.shadow[index], &row, sizeof(Row)) != 0) {
        table.shadow[index] = row;
        table.dirty[index] = kAllSlots;
    }
}

//======================================================================================================================
template <typename Row>
void writeRows(PacedTable<Row>& table, uint32_t slot, SceneTableStats& stats) {
    const auto bit = static_cast<uint8_t>(1U << slot);
    for (uint32_t i = 0; i < table.shadow.size(); ++i) {
        if ((table.dirty[i] & bit) != 0) {
            table.buffers[slot]->write(uint64_t{i} * sizeof(Row), &table.shadow[i], sizeof(Row));
            table.dirty[i] &= static_cast<uint8_t>(~bit);
            ++stats.rowsWritten;
            stats.bytesWritten += sizeof(Row);
        }
    }
}

} // namespace

struct Scene::Storage {
    uint16_t store = nextStore();
    std::vector<IdentitySlot> instances;
    std::vector<IdentitySlot> textureIds;
    std::vector<IdentitySlot> localLightIds;
    uint32_t instanceSearchStart = 0;
    uint32_t textureSearchStart = 0;
    uint32_t localLightSearchStart = 0;
    std::vector<engine::MeshData> meshData;
    std::vector<engine::MeshRow> meshRows;
    std::vector<MaterialRecord> materials;
    std::vector<engine::LocalLight> localLightData;
    std::vector<LightId> liveLightIds;
    uint32_t enabledLightCount = 0;
    /// Every `addLight` result from before `finalize`, in call order; `LightOrbitTrack::light`
    /// indexes here. Frozen at `finalize` (see `addLight`): append-only up to that point, so a
    /// later removal never shifts an earlier index's mapping, and never grown afterward, so a
    /// light added post-finalize (unindexable, static by contract) cannot make it grow without
    /// bound across repeated runtime add/remove cycles.
    std::vector<LightId> lightCreationOrder;
    std::vector<std::unique_ptr<rojoRHI::Texture>> textures;
    std::unique_ptr<rojoRHI::Buffer> vertices;
    std::unique_ptr<rojoRHI::Buffer> indices;
    PacedTable<engine::InstanceRow> instanceTable;
    PacedTable<engine::MaterialRow> materialTable;
    PacedTable<engine::MeshRow> meshTable;
    PacedTable<engine::LightRow> lightTable;
    std::vector<RetiringBuffer> retiring;
    std::vector<std::pair<uint64_t, std::unique_ptr<rojoRHI::Texture>>> retiringTextures;
    rojoRHI::Device* device = nullptr;
    uint64_t lastFrame = 0;
    uint64_t coverageEpoch = 0;
    std::vector<MaterialCoverage> materialCoverage;
    bool prepared = false;
    SceneTableStats stats;
};

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
    Aabb bounds{glm::vec3(std::numeric_limits<float>::max()),
                glm::vec3(std::numeric_limits<float>::lowest())};
    bool finite = true;
    for (const auto& vertex : data.vertices) {
        const glm::vec3 point(vertex.px, vertex.py, vertex.pz);
        finite = finite && isFinite(point);
        bounds.minimum = glm::min(bounds.minimum, point);
        bounds.maximum = glm::max(bounds.maximum, point);
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
    const uint32_t slot = allocateIdentity(m_storage->textureIds, m_storage->textureSearchStart);
    m_storage->textures.resize(m_storage->textureIds.size());
    m_storage->textures[slot] = std::move(texture);
    return {slot, m_storage->textureIds[slot].generation, m_storage->store};
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
    const uint32_t slot = allocateIdentity(m_storage->instances, m_storage->instanceSearchStart);
    object.id = {slot, m_storage->instances[slot].generation, m_storage->store};
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
    auto& slot = m_storage->instances[id.slot];
    m_storage->instanceSearchStart = std::min(m_storage->instanceSearchStart, id.slot);
    slot.live = false;
    if (slot.generation != std::numeric_limits<uint16_t>::max()) {
        ++slot.generation;
    }
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
    auto& slot = m_storage->textureIds[id.slot];
    m_storage->textureSearchStart = std::min(m_storage->textureSearchStart, id.slot);
    slot.live = false;
    if (slot.generation != std::numeric_limits<uint16_t>::max()) {
        ++slot.generation;
    }
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
    const uint32_t slot = allocateIdentity(storage.localLightIds, storage.localLightSearchStart);
    if (slot >= storage.localLightData.size()) {
        storage.localLightData.resize(slot + 1);
    }
    storage.localLightData[slot] = light;
    storage.enabledLightCount += light.enabled ? 1u : 0u;
    const LightId id{slot, storage.localLightIds[slot].generation, storage.store};
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
    auto& slot = storage.localLightIds[id.slot];
    storage.localLightSearchStart = std::min(storage.localLightSearchStart, id.slot);
    slot.live = false;
    if (slot.generation != std::numeric_limits<uint16_t>::max()) {
        ++slot.generation;
    }
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

//======================================================================================================================
rojoRHI::Result<void> Scene::finalize(rojoRHI::Device& device) {
    auto& storage = *m_storage;
    LMX_ASSERT(storage.device == nullptr, "scene is already finalized");
    std::vector<engine::Vertex> vertices;
    std::vector<uint32_t> indices;
    for (size_t i = 0; i < storage.meshData.size(); ++i) {
        const auto& mesh = storage.meshData[i];
        vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
        for (uint32_t index : mesh.indices) {
            indices.push_back(index + storage.meshRows[i].firstVertex);
        }
    }
    storage.stats.vertexBytes = vertices.size() * sizeof(engine::Vertex);
    storage.stats.indexBytes = indices.size() * sizeof(uint32_t);
    if (vertices.empty()) {
        vertices.resize(1);
    }
    if (indices.empty()) {
        indices.resize(1);
    }
    auto vertexBuffer = device.createBuffer({.size = vertices.size() * sizeof(engine::Vertex),
                                             .storageRead = true,
                                             .cpuReadback = true,
                                             .label = "lmx.scene.vertices"},
                                            vertices.data());
    if (!vertexBuffer) {
        return std::unexpected(vertexBuffer.error());
    }
    auto indexBuffer = device.createBuffer({.size = indices.size() * sizeof(uint32_t),
                                            .storageRead = true,
                                            .cpuReadback = true,
                                            .label = "lmx.scene.indices"},
                                           indices.data());
    if (!indexBuffer) {
        return std::unexpected(indexBuffer.error());
    }
    for (auto result :
         {reserveTable(device, storage.instanceTable,
                       static_cast<uint32_t>(storage.instances.size()), "lmx.scene.instances", 0,
                       storage.retiring, storage.stats.growthEvents),
          reserveTable(device, storage.materialTable,
                       static_cast<uint32_t>(storage.materials.size()), "lmx.scene.materials", 0,
                       storage.retiring, storage.stats.growthEvents),
          reserveTable(device, storage.meshTable, static_cast<uint32_t>(storage.meshRows.size()),
                       "lmx.scene.meshes", 0, storage.retiring, storage.stats.growthEvents)}) {
        if (!result) {
            return result;
        }
    }
    for (uint32_t i = 0; i < storage.meshRows.size(); ++i) {
        updateRow(storage.meshTable, i, storage.meshRows[i]);
    }
    SceneTableStats ignored;
    for (uint32_t slot = 0; slot < kSlots; ++slot) {
        writeRows(storage.meshTable, slot, ignored);
    }
    storage.vertices = std::move(*vertexBuffer);
    storage.indices = std::move(*indexBuffer);
    storage.meshData.clear();
    storage.meshData.shrink_to_fit();
    storage.device = &device;
    return {};
}

//======================================================================================================================
void Scene::validateObjects() const {
    std::vector<bool> seen(m_storage->instances.size(), false);
    for (const auto& object : objects) {
        LMX_ASSERT(resolves(object.id, m_storage->store, m_storage->instances),
                   "object list contains an invalid instance identity; use addObject");
        LMX_ASSERT(!seen[object.id.slot], "object list contains a duplicate instance identity");
        seen[object.id.slot] = true;
    }
    for (uint32_t slot = 0; slot < m_storage->instances.size(); ++slot) {
        LMX_ASSERT(seen[slot] == m_storage->instances[slot].live,
                   "object list lost a live instance identity; use removeObject");
    }
}

//======================================================================================================================
rojoRHI::Result<void> Scene::prepareFrame(uint64_t frameNumber) {
    auto& storage = *m_storage;
    validateObjects();
    LMX_ASSERT(storage.device, "scene must be finalized before preparing a frame");
    LMX_ASSERT(frameNumber == storage.device->frameNumber() && frameNumber > storage.lastFrame,
               "prepareFrame requires a newly paced device frame");
    const auto retired = std::erase_if(storage.retiring, [frameNumber](const auto& buffer) {
        return frameNumber >= buffer.releaseAtFrame;
    });
    if (retired != 0) {
        LMX_LOG_INFO("Scene tables retired {} buffers at frame {}", retired, frameNumber);
    }
    auto instances = reserveTable(
        *storage.device, storage.instanceTable, static_cast<uint32_t>(storage.instances.size()),
        "lmx.scene.instances", storage.lastFrame, storage.retiring, storage.stats.growthEvents);
    if (!instances) {
        return instances;
    }
    auto materials = reserveTable(
        *storage.device, storage.materialTable, static_cast<uint32_t>(storage.materials.size()),
        "lmx.scene.materials", storage.lastFrame, storage.retiring, storage.stats.growthEvents);
    if (!materials) {
        return materials;
    }
    if (!storage.localLightIds.empty()) {
        auto lights =
            reserveTable(*storage.device, storage.lightTable,
                         static_cast<uint32_t>(storage.localLightIds.size()), "lmx.scene.lights",
                         storage.lastFrame, storage.retiring, storage.stats.growthEvents);
        if (!lights) {
            return lights;
        }
    }
    const auto alreadyRetired = std::erase_if(storage.retiring, [frameNumber](const auto& buffer) {
        return frameNumber >= buffer.releaseAtFrame;
    });
    if (alreadyRetired != 0) {
        LMX_LOG_INFO("Scene tables retired {} inactive-scene buffers at frame {}", alreadyRetired,
                     frameNumber);
    }
    std::erase_if(storage.retiringTextures,
                  [frameNumber](const auto& texture) { return frameNumber >= texture.first; });
    bool coverageChanged = false;
    for (const auto& object : objects) {
        LMX_ASSERT(tryMesh(object.mesh) && tryMaterial(object.material),
                   "object contains an invalid resource identity");
        engine::InstanceRow row{};
        row.model = object.modelMatrix();
        row.previousModel = object.previousModel;
        row.normalMatrix = glm::mat4(glm::transpose(glm::inverse(glm::mat3(row.model))));
        row.meshRow = object.mesh.slot;
        row.materialRow = object.material.slot;
        row.flags =
            object.motionClass == engine::MotionClass::Invalid ? engine::kInstanceMotionInvalid : 0;
        row.emissiveScale = object.emissiveStrength;
        const auto localBounds = meshBounds(object.mesh);
        const auto worldBounds =
            localBounds ? transformAabb(row.model, *localBounds) : std::nullopt;
        if (worldBounds) {
            row.worldBoundsMin = worldBounds->minimum;
            row.worldBoundsMax = worldBounds->maximum;
        } else {
            row.flags |= engine::kInstanceBoundsUnreliable;
        }
        if (object.id.slot >= storage.instanceTable.shadow.size()) {
            coverageChanged = true;
        } else {
            const auto& previous = storage.instanceTable.shadow[object.id.slot];
            coverageChanged |= previous.model != row.model || previous.meshRow != row.meshRow ||
                               previous.materialRow != row.materialRow;
        }
        updateRow(storage.instanceTable, object.id.slot, row);
    }
    for (uint32_t i = 0; i < storage.instances.size(); ++i) {
        if (!storage.instances[i].live) {
            updateRow(storage.instanceTable, i, engine::InstanceRow{});
        }
    }
    for (uint32_t i = 0; i < storage.materials.size(); ++i) {
        const auto& material = storage.materials[i];
        for (const auto id : {material.diffuse, material.normalMap, material.metallicRoughness,
                              material.occlusion, material.emissiveMap}) {
            LMX_ASSERT(!id || tryTexture(*id), "material contains an invalid texture identity");
        }
        LMX_ASSERT(material.alphaMode != engine::AlphaMode::Mask ||
                       (std::isfinite(material.alphaCutoff) && material.alphaCutoff >= 0.0f),
                   "masked material alpha cutoff must be finite and nonnegative");
        const MaterialCoverage coverage{.diffuse = material.diffuse,
                                        .uvTransform = material.uvTransform,
                                        .alpha = material.albedo.a,
                                        .cutoff = material.alphaCutoff,
                                        .mode = material.alphaMode,
                                        .doubleSided = material.doubleSided};
        if (i >= storage.materialCoverage.size()) {
            storage.materialCoverage.push_back(coverage);
            coverageChanged = true;
        } else if (storage.materialCoverage[i] != coverage) {
            storage.materialCoverage[i] = coverage;
            coverageChanged = true;
        }
        engine::MaterialRow row{};
        row.uvTransform = material.uvTransform;
        row.albedo = material.albedo;
        row.emissive = material.emissive;
        row.roughness = material.roughness;
        row.metallic = material.metallic;
        row.occlusionStrength = material.occlusionStrength;
        row.alphaCutoff = material.alphaCutoff;
        row.flags = (material.normalMap ? engine::kMaterialHasNormalMap : 0) |
                    (material.alphaMode == engine::AlphaMode::Mask ? engine::kMaterialMasked : 0) |
                    (material.doubleSided ? engine::kMaterialDoubleSided : 0);
        updateRow(storage.materialTable, i, row);
    }
    for (uint32_t i = 0; i < storage.localLightIds.size(); ++i) {
        if (storage.localLightIds[i].live) {
            auto row = engine::makeLightRow(storage.localLightData[i]);
            LMX_ASSERT(row, "stored local light failed re-validation");
            updateRow(storage.lightTable, i, *row);
        } else {
            updateRow(storage.lightTable, i, engine::LightRow{});
        }
    }
    if (coverageChanged) {
        LMX_ASSERT(storage.coverageEpoch < std::numeric_limits<uint64_t>::max(),
                   "scene coverage epoch exhausted");
        ++storage.coverageEpoch;
    }
    storage.stats.rowsWritten = 0;
    storage.stats.bytesWritten = 0;
    storage.stats.slot = static_cast<uint32_t>(frameNumber % kSlots);
    writeRows(storage.instanceTable, storage.stats.slot, storage.stats);
    writeRows(storage.materialTable, storage.stats.slot, storage.stats);
    if (storage.lightTable.capacity != 0) {
        writeRows(storage.lightTable, storage.stats.slot, storage.stats);
    }
    storage.lastFrame = frameNumber;
    storage.prepared = true;
    return {};
}

//======================================================================================================================
uint64_t Scene::coverageEpoch() const {
    return m_storage->coverageEpoch;
}

//======================================================================================================================
SceneTableStats Scene::tableStats() const {
    const auto& storage = *m_storage;
    SceneTableStats stats = storage.stats;
    stats.instanceCount = static_cast<uint32_t>(objects.size());
    stats.materialCount = static_cast<uint32_t>(storage.materials.size());
    stats.meshCount = static_cast<uint32_t>(storage.meshRows.size());
    stats.instanceCapacity = storage.instanceTable.capacity;
    stats.materialCapacity = storage.materialTable.capacity;
    stats.meshCapacity = storage.meshTable.capacity;
    stats.lightCount = static_cast<uint32_t>(storage.liveLightIds.size());
    stats.lightCapacity = storage.lightTable.capacity;
    stats.pendingReleaseBuffers = static_cast<uint32_t>(storage.retiring.size());
    return stats;
}

//======================================================================================================================
engine::SceneTables Scene::tables() const {
    const auto& storage = *m_storage;
    if (!storage.device) {
        return {};
    }
    LMX_ASSERT(storage.prepared, "scene tables require prepareFrame before use");
    const uint32_t slot = storage.stats.slot;
    return {.vertices = storage.vertices.get(),
            .indices = storage.indices.get(),
            .meshes = storage.meshTable.buffers[slot].get(),
            .instances = storage.instanceTable.buffers[slot].get(),
            .materials = storage.materialTable.buffers[slot].get(),
            .meshCount = static_cast<uint32_t>(storage.meshRows.size()),
            .instanceCount = static_cast<uint32_t>(storage.instances.size()),
            .materialCount = static_cast<uint32_t>(storage.materials.size()),
            .instanceRows = storage.instanceTable.shadow,
            .instanceCapacity = storage.instanceTable.capacity,
            .lights =
                storage.lightTable.capacity != 0 ? storage.lightTable.buffers[slot].get() : nullptr,
            .lightRowCount = static_cast<uint32_t>(storage.localLightIds.size()),
            .lightCapacity = storage.lightTable.capacity,
            .lightRows = storage.lightTable.shadow,
            .liveLightCount = storage.enabledLightCount};
}

} // namespace lmx::engine
