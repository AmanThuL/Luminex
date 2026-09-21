//----------------------------------------------------------------------------------------------------------------------
/// @file SceneLibrary.h
/// @brief Declares stable scene identifiers, catalog entries, and scene lookup.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Scene/Scene.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::scenes {

/// Stable catalog handle whose index is meaningful only for the built-in scene catalog.
struct SceneId {
    size_t catalogIndex = 0; ///< Zero-based index into the static catalog.
    /// Compares catalog identity.
    friend bool operator==(SceneId, SceneId) = default;
};

/// Classifies a scene's user-facing purpose.
enum class SceneRole {
    Showcase,   ///< Full environment demonstrating shipped rendering behavior.
    Sample,     ///< Focused third-party asset sample.
    Diagnostic, ///< Deterministic scene used for visual validation.
};

/// Immutable catalog metadata plus runtime availability information.
struct SceneEntry {
    SceneId id;                        ///< Catalog handle used by API calls.
    std::string_view stableId;         ///< Stable CLI identifier.
    std::string_view displayName;      ///< User-facing label.
    SceneRole role;                    ///< User-facing scene category.
    std::string_view assetRequirement; ///< Setup artifact required to build the scene.
    bool available = false;            ///< Whether its required assets are present.
    std::string hint;                  ///< Availability guidance shown to users.
};

/// Returns the scene selected when the caller supplies no explicit ID.
SceneId defaultSceneId();
/// Resolves a stable command-line identifier to its catalog handle.
std::optional<SceneId> parseSceneId(std::string_view stableId);
/// Returns the stable command-line identifier for `id`.
std::string_view sceneIdString(SceneId id);

/// Every catalog entry's stableId, in catalog order. Device-free (the catalog itself is static
/// data), so callers that only need to name valid IDs -- CLI usage/error text -- do not need a
/// device just to keep that text in sync with the catalog.
std::span<const std::string_view> sceneStableIds();

/// Provides catalog metadata and lazily constructs device-owned scene resources.
class SceneLibrary {
public:
    /// Creates a catalog whose loaded scenes use `device` for their full lifetime.
    /// labInstances sets the total VisibilityLab population; its builder validates 1..1,048,576.
    /// labOccluders adds 0..1,024 optional slabs without changing the default lab.
    /// labLights sets LightLab's grid local-light population; its builder validates
    /// 1..engine::kMaxLocalLights. labLightPile adds that many extra lights stacked at one point.
    explicit SceneLibrary(rojoRHI::Device& device, uint32_t labInstances = 4096,
                          uint32_t labOccluders = 0, uint32_t labLights = 256,
                          uint32_t labLightPile = 0);

    /// Returns every catalog entry in stable display order.
    std::span<const SceneEntry> entries() const;
    /// Returns metadata for a valid catalog handle.
    const SceneEntry& entry(SceneId id) const;
    /// Loads a scene on first access and returns the library-owned instance.
    asset::AssetResult<engine::Scene*> get(SceneId id);

private:
    rojoRHI::Device& m_device;
    uint32_t m_labInstances;
    uint32_t m_labOccluders;
    uint32_t m_labLights;
    uint32_t m_labLightPile;
    std::vector<SceneEntry> m_entries;
    std::vector<std::unique_ptr<engine::Scene>> m_scenes;
};

} // namespace lmx::scenes
