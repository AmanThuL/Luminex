//----------------------------------------------------------------------------------------------------------------------
/// @file SceneStorage.h
/// @brief Declares the scene's private identity, geometry and table storage.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Containers/SlotAllocator.h"
#include "Core/Diagnostics/Assert.h"
#include "Engine/Scene/PacedTable.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneTables.h"
#include <rojoRHI/RHI.h>

#include <glm/glm.hpp>

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace lmx::engine {

/// Returns a process-unique store identity, so one scene's handles never resolve in another.
inline uint16_t nextStore() {
    static std::atomic<uint32_t> next{1};
    uint32_t value = next.load(std::memory_order_relaxed);
    do {
        LMX_ASSERT(value <= std::numeric_limits<uint16_t>::max(),
                   "scene store identities exhausted");
    } while (!next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed));
    return static_cast<uint16_t>(value);
}

/// Returns whether `id` belongs to `store` and names a live slot of `slots`.
template <typename Id>
bool resolves(Id id, uint16_t store, const SlotAllocator& slots) {
    return id.store == store && slots.resolves(id.slot, id.generation);
}

/// The texture-alpha coverage inputs of one material; a change invalidates occlusion history.
struct MaterialCoverage {
    std::optional<TextureId> diffuse;
    glm::mat4 uvTransform{1.0f};
    float alpha = 1.0f;
    float cutoff = 0.5f;
    engine::AlphaMode mode = engine::AlphaMode::Opaque;
    bool doubleSided = false;

    bool operator==(const MaterialCoverage&) const = default;
};

/// Every identity, pooled geometry buffer and paced table a `Scene` owns.
struct Scene::Storage {
    uint16_t store = nextStore();
    SlotAllocator instances;
    SlotAllocator textureIds;
    SlotAllocator localLightIds;
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

} // namespace lmx::engine
