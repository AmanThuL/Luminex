//----------------------------------------------------------------------------------------------------------------------
/// @file SceneLibrary.cpp
/// @brief Implements the scene catalog and lazy scene construction.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/SceneLibrary.h"

#include "Core/Assert.h"

#include <array>
#include <filesystem>
#include <utility>

namespace lmx::engine {

namespace {

using SceneBuilder = AssetResult<std::unique_ptr<Scene>> (*)(rhi::Device&);

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
    // Code-generated diagnostics with an internal environment fallback: no catalog-level asset
    // requirement, so an empty availabilityPath keeps it always available (see repoPathExists).
    SceneDescriptor{"material-lab", "MaterialLab", SceneRole::Diagnostic, "", "", false,
                    &loadMaterialLabScene},
};

static_assert([] {
    size_t defaults = 0;
    for (const SceneDescriptor& descriptor : kSceneDescriptors) {
        defaults += descriptor.isDefault ? 1 : 0;
    }
    return defaults == 1;
}());

//======================================================================================================================
bool repoPathExists(std::string_view relative) {
    std::filesystem::path dir = std::filesystem::current_path();
    for (int i = 0; i < 8; ++i) {
        if (std::filesystem::exists(dir / relative)) {
            return true;
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir) {
            break;
        }
        dir = dir.parent_path();
    }
    return false;
}

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
SceneLibrary::SceneLibrary(rhi::Device& device) : m_device(device) {
    m_entries.reserve(kSceneDescriptors.size());
    m_scenes.resize(kSceneDescriptors.size());
    for (size_t i = 0; i < kSceneDescriptors.size(); ++i) {
        const SceneDescriptor& descriptor = kSceneDescriptors[i];
        const bool available =
            descriptor.availabilityPath.empty() || repoPathExists(descriptor.availabilityPath);
        m_entries.push_back({.id = SceneId{i},
                             .stableId = descriptor.stableId,
                             .displayName = descriptor.displayName,
                             .role = descriptor.role,
                             .assetRequirement = descriptor.assetRequirement,
                             .available = available,
                             .hint = available ? "" : "run `xmake setup`"});
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
AssetResult<Scene*> SceneLibrary::get(SceneId id) {
    const size_t index = descriptorIndex(id);
    if (m_scenes[index] != nullptr) {
        return m_scenes[index].get();
    }
    if (!m_entries[index].available) {
        return std::unexpected(
            AssetError{AssetErrorCode::NotFound,
                       "scene '" + std::string(m_entries[index].stableId) + "' requires " +
                           std::string(m_entries[index].assetRequirement) + "; run `xmake setup`"});
    }

    auto built = kSceneDescriptors[index].build(m_device);
    if (!built) {
        return std::unexpected(built.error());
    }
    m_scenes[index] = std::move(*built);
    return m_scenes[index].get();
}

} // namespace lmx::engine
