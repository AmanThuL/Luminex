//----------------------------------------------------------------------------------------------------------------------
/// @file SceneLibrary.h
/// @brief Declares document identities and whole-snapshot scene caching and replacement.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Scenes/SceneDocuments.h"
#include <map>
#include <span>

namespace lmx::scenes {
/// Catalog key or a caller-spelled document path; equality preserves that supplied identity.
struct SceneId {
    std::string key; ///< Stable catalog id, or path as supplied for evidence and UI.
    /// Returns whether key names a built-in document.
    bool isCatalog() const;
    /// Compares scene identity without borrowing either key's storage.
    friend bool operator==(const SceneId&, const SceneId&) = default;
};
/// A scene's user-facing purpose.
enum class SceneRole {
    Showcase,   ///< Full environment demonstrating shipped rendering behavior.
    Sample,     ///< Focused third-party asset sample or path-opened document.
    Diagnostic, ///< Deterministic scene for validation.
};
/// Owned metadata; arbitrary document paths have the same lifetime guarantees as catalog entries.
struct SceneEntry {
    SceneId id;                   ///< API identity.
    std::string stableId;         ///< Catalog key or supplied document path.
    std::string displayName;      ///< Document label, or filename before first successful load.
    SceneRole role;               ///< User-facing purpose.
    std::string assetRequirement; ///< Required setup artifact description.
    bool available = false;       ///< Required files were present when metadata was refreshed.
    std::string hint;             ///< Recovery guidance.
};
/// Returns Sponza, the default catalog document.
SceneId defaultSceneId();
/// Resolves a catalog key only; path inputs use sceneIdFromPath.
std::optional<SceneId> parseSceneId(std::string_view stableId);
/// Keeps a document path exactly as supplied; cache equivalence is handled by SceneLibrary.
SceneId sceneIdFromPath(const std::filesystem::path& path);
/// Borrows the stable key; the SceneId must outlive the returned view.
std::string_view sceneIdString(const SceneId& id);
/// Lists all six built-in document keys in display order.
std::span<const std::string_view> sceneStableIds();

/// Owns complete loaded snapshots and waits for their GPU use before replacement/destruction.
class SceneLibrary {
public:
    /// Explicit optional population overrides affect generators only; absence preserves the
    /// document.
    explicit SceneLibrary(rojoRHI::Device& device, std::optional<uint32_t> labInstances = {},
                          std::optional<uint32_t> labOccluders = {},
                          std::optional<uint32_t> labLights = {},
                          std::optional<uint32_t> labLightPile = {});
    /// Waits for outstanding GPU work before releasing cached scenes.
    ~SceneLibrary();
    /// Returns the built-in entries in stable display order.
    std::span<const SceneEntry> entries() const;
    /// Returns stable owned metadata for either a catalog key or an arbitrary path.
    const SceneEntry& entry(const SceneId& id) const;
    /// Loads once, returning the cached scene until reload explicitly bypasses the cache.
    asset::AssetResult<engine::Scene*> get(const SceneId& id);
    /// Returns a cached complete snapshot, or null before a successful load.
    engine::LoadedScene* loaded(const SceneId& id);
    /// Builds before touching the old snapshot. On success waits for GPU retirement, invokes
    /// beforeReplace with the still-live old value so the caller invalidates session defaults and
    /// selection, then publishes the new scene/binding/document/path/hash together. Failure invokes
    /// no callback and preserves the cached/active scene. The callback must not throw.
    asset::AssetResult<engine::LoadedScene*>
    reload(const SceneId& id, const std::function<void(const engine::LoadedScene&)>& beforeReplace);

    /// Retires and removes a cached scene after a replacement was successfully constructed.
    /// The callback invalidates its session state while the old scene is still alive.
    void forget(const SceneId& id,
                const std::function<void(const engine::LoadedScene&)>& beforeRemove);
    /// Adopts a verified canonical save without rebuilding the live scene or changing bindings.
    /// Rekeying retains LoadedScene's address. A cached destination retires first and is
    /// invalidated through beforeReplace; source and destination metadata publish only after
    /// verification by the caller; the old key's cached path metadata is discarded and rebuilt from
    /// disk on demand. source must belong to this library; document/path/hash must
    /// describe that save.
    engine::LoadedScene&
    adoptSaved(engine::LoadedScene& source, const SceneId& destination,
               asset::SceneDocument document, std::filesystem::path path, std::string hash,
               const std::function<void(const engine::LoadedScene&)>& beforeReplace);

private:
    asset::AssetResult<std::filesystem::path> documentPath(const SceneId& id) const;
    std::string cacheKey(const SceneId& id) const;
    rojoRHI::Device& m_device;
    GeneratorOverrides m_overrides;
    std::vector<SceneEntry> m_entries;
    mutable std::map<std::string, SceneEntry> m_pathEntries;
    std::map<std::string, engine::LoadedScene> m_scenes;
};
} // namespace lmx::scenes
