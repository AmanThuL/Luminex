//----------------------------------------------------------------------------------------------------------------------
/// @file SceneLibrary.cpp
/// @brief Implements the scene catalog and lazy scene construction.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Catalog/SceneLibrary.h"

#include "Asset/RepositoryAsset.h"

#include "Core/Assert.h"

#include <array>
#include <filesystem>
#include <utility>

namespace lmx::scene {

namespace {

using SceneBuilder = asset::AssetResult<std::unique_ptr<Scene>> (*)(rojoRHI::Device&);

struct SceneDescriptor {
    std::string_view stableId;
    std::string_view displayName;
    SceneRole role;
    std::string_view assetRequirement;
    std::string_view availabilityPath;
    bool isDefault;
    SceneBuilder build;
};

constexpr std::array kSceneDescriptors{
    SceneDescriptor{"sponza", "Sponza", SceneRole::Showcase, "Crytek Sponza archive",
                    "Assets/Fetched/Sponza/Sponza.gltf", true, &loadSponzaScene},
    SceneDescriptor{"damaged-helmet", "Damaged Helmet", SceneRole::Sample,
                    "Khronos DamagedHelmet sample",
                    "Assets/Fetched/DamagedHelmet/DamagedHelmet.glb", false, &loadHelmetScene},
    SceneDescriptor{"milk-truck", "Milk Truck", SceneRole::Sample, "Khronos CesiumMilkTruck sample",
                    "Assets/Fetched/CesiumMilkTruck/CesiumMilkTruck.glb", false,
                    &loadMilkTruckScene},
    // Code-generated diagnostics with an internal environment fallback: no catalog-level asset
    // requirement, so an empty availabilityPath keeps it always available (see
    // findRepositoryAsset).
    SceneDescriptor{"material-lab", "MaterialLab", SceneRole::Diagnostic, "", "", false,
                    &loadMaterialLabScene},
    SceneDescriptor{"temporal-lab", "TemporalLab", SceneRole::Diagnostic, "", "", false,
                    &loadTemporalLabScene},
    SceneDescriptor{"san-miguel", "San Miguel", SceneRole::Showcase,
                    "Optional San Miguel realtime archive",
                    "Assets/Fetched/SanMiguel/SanMiguel.gltf", false, &loadSanMiguelScene},
    SceneDescriptor{"visibility-lab", "VisibilityLab", SceneRole::Diagnostic, "", "", false,
                    nullptr},
    SceneDescriptor{"light-lab", "LightLab", SceneRole::Diagnostic, "", "", false, nullptr},
};

static_assert([] {
    size_t defaults = 0;
    for (const SceneDescriptor& descriptor : kSceneDescriptors) {
        defaults += descriptor.isDefault ? 1 : 0;
    }
    return defaults == 1;
}());

//======================================================================================================================
size_t descriptorIndex(SceneId id) {
    LMX_ASSERT(id.catalogIndex < kSceneDescriptors.size(), "unknown SceneId");
    return id.catalogIndex;
}

} // namespace

//======================================================================================================================
SceneId defaultSceneId() {
    for (size_t i = 0; i < kSceneDescriptors.size(); ++i) {
        if (kSceneDescriptors[i].isDefault) {
            return SceneId{i};
        }
    }
    LMX_ASSERT(false, "scene catalog has no default");
    return SceneId{0};
}

//======================================================================================================================
std::optional<SceneId> parseSceneId(std::string_view stableId) {
    for (size_t i = 0; i < kSceneDescriptors.size(); ++i) {
        if (kSceneDescriptors[i].stableId == stableId) {
            return SceneId{i};
        }
    }
    return std::nullopt;
}

//======================================================================================================================
std::string_view sceneIdString(SceneId id) {
    return kSceneDescriptors[descriptorIndex(id)].stableId;
}

//======================================================================================================================
std::span<const std::string_view> sceneStableIds() {
    static const std::vector<std::string_view> ids = [] {
        std::vector<std::string_view> result;
        result.reserve(kSceneDescriptors.size());
        for (const SceneDescriptor& descriptor : kSceneDescriptors) {
            result.push_back(descriptor.stableId);
        }
        return result;
    }();
    return ids;
}

//======================================================================================================================
SceneLibrary::SceneLibrary(rojoRHI::Device& device, uint32_t labInstances, uint32_t labOccluders,
                           uint32_t labLights, uint32_t labLightPile)
    : m_device(device), m_labInstances(labInstances), m_labOccluders(labOccluders),
      m_labLights(labLights), m_labLightPile(labLightPile) {
    m_entries.reserve(kSceneDescriptors.size());
    m_scenes.resize(kSceneDescriptors.size());
    for (size_t i = 0; i < kSceneDescriptors.size(); ++i) {
        const SceneDescriptor& descriptor = kSceneDescriptors[i];
        const bool available = descriptor.availabilityPath.empty() ||
                               asset::findRepositoryAsset(descriptor.availabilityPath).has_value();
        m_entries.push_back({.id = SceneId{i},
                             .stableId = descriptor.stableId,
                             .displayName = descriptor.displayName,
                             .role = descriptor.role,
                             .assetRequirement = descriptor.assetRequirement,
                             .available = available,
                             .hint = available ? ""
                                     : descriptor.stableId == "san-miguel"
                                         ? "run `xmake setup --san-miguel`"
                                         : "run `xmake setup`"});
    }
}

//======================================================================================================================
std::span<const SceneEntry> SceneLibrary::entries() const {
    return m_entries;
}

//======================================================================================================================
const SceneEntry& SceneLibrary::entry(SceneId id) const {
    return m_entries[descriptorIndex(id)];
}

//======================================================================================================================
asset::AssetResult<Scene*> SceneLibrary::get(SceneId id) {
    const size_t index = descriptorIndex(id);
    if (m_scenes[index] != nullptr) {
        return m_scenes[index].get();
    }
    if (!m_entries[index].available) {
        return std::unexpected(asset::AssetError{
            asset::AssetErrorCode::NotFound,
            "scene '" + std::string(m_entries[index].stableId) + "' requires " +
                std::string(m_entries[index].assetRequirement) + "; run `xmake setup`"});
    }

    auto built = kSceneDescriptors[index].stableId == "visibility-lab"
                     ? loadVisibilityLabScene(m_device, m_labInstances, m_labOccluders)
                 : kSceneDescriptors[index].stableId == "light-lab"
                     ? loadLightLabScene(m_device, m_labLights, m_labLightPile)
                     : kSceneDescriptors[index].build(m_device);
    if (!built) {
        return std::unexpected(built.error());
    }
    m_scenes[index] = std::move(*built);
    return m_scenes[index].get();
}

} // namespace lmx::scene
