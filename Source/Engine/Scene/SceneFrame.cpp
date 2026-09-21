//----------------------------------------------------------------------------------------------------------------------
/// @file SceneFrame.cpp
/// @brief Implements scene finalization, paced per-frame table updates and table views.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Scene/Scene.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Diagnostics/Log.h"
#include "Core/Math/Aabb.h"
#include "Engine/Scene/PacedTable.h"
#include "Engine/Scene/SceneStorage.h"
#include "Engine/Types/LocalLightMath.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace lmx::engine {

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
        LMX_ASSERT(seen[slot] ==
                       m_storage->instances.resolves(slot, m_storage->instances.generation(slot)),
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
    if (storage.localLightIds.size() != 0) {
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
        if (!storage.instances.resolves(i, storage.instances.generation(i))) {
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
        if (storage.localLightIds.resolves(i, storage.localLightIds.generation(i))) {
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
