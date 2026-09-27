//----------------------------------------------------------------------------------------------------------------------
/// @file SceneLibrary.cpp
/// @brief Caches whole document snapshots and safely replaces them after successful construction.
//----------------------------------------------------------------------------------------------------------------------

#include "Scenes/SceneLibrary.h"
#include "Engine/Asset/RepositoryAsset.h"
#include <algorithm>
#include <array>

namespace lmx::scenes {
namespace {
constexpr std::array<std::string_view, 6> kIds{"sponza",     "material-lab",   "temporal-lab",
                                               "san-miguel", "visibility-lab", "light-lab"};
constexpr std::array<std::string_view, 6> kNames{"Sponza",     "MaterialLab",   "TemporalLab",
                                                 "San Miguel", "VisibilityLab", "LightLab"};
//======================================================================================================================
bool available(const asset::SceneDocument& document) {
    const auto root = asset::findRepositoryAsset("Assets");
    if (!root)
        return false;
    std::error_code error;
    for (const auto& node : document.nodes)
        if (node.asset && !std::filesystem::is_regular_file(*root / node.asset->uri, error))
            return false;
    const auto& hdri = document.look.environment.hdri;
    return !hdri || std::filesystem::is_regular_file(*root / hdri->uri, error);
}
} // namespace
//======================================================================================================================
bool SceneId::isCatalog() const {
    return parseSceneId(key).has_value();
}
//======================================================================================================================
SceneId defaultSceneId() {
    return {"sponza"};
}
//======================================================================================================================
std::optional<SceneId> parseSceneId(std::string_view stableId) {
    for (auto id : kIds)
        if (id == stableId)
            return SceneId{std::string(id)};
    return {};
}
//======================================================================================================================
SceneId sceneIdFromPath(const std::filesystem::path& path) {
    return {path.string()};
}
//======================================================================================================================
std::string_view sceneIdString(const SceneId& id) {
    return id.key;
}
//======================================================================================================================
std::span<const std::string_view> sceneStableIds() {
    return kIds;
}
//======================================================================================================================
SceneLibrary::SceneLibrary(rojoRHI::Device& device, std::optional<uint32_t> instances,
                           std::optional<uint32_t> occluders, std::optional<uint32_t> lights,
                           std::optional<uint32_t> pile)
    : m_device(device), m_overrides{instances, occluders, lights, pile} {
    for (size_t i = 0; i < kIds.size(); ++i) {
        auto document = readCatalogDocument(kIds[i]);
        const bool ready = document && available(*document);
        m_entries.push_back({{std::string(kIds[i])},
                             std::string(kIds[i]),
                             std::string(kNames[i]),
                             i == 0 || i == 3 ? SceneRole::Showcase : SceneRole::Diagnostic,
                             "document and its referenced assets",
                             ready,
                             ready    ? ""
                             : i == 3 ? "run `xmake setup --san-miguel`"
                                      : "run `xmake setup`"});
    }
}
//======================================================================================================================
SceneLibrary::~SceneLibrary() {
    m_device.waitIdle();
}
//======================================================================================================================
std::span<const SceneEntry> SceneLibrary::entries() const {
    return m_entries;
}
//======================================================================================================================
const SceneEntry& SceneLibrary::entry(const SceneId& id) const {
    for (const auto& entry : m_entries)
        if (entry.id == id)
            return entry;
    const auto key = cacheKey(id);
    auto [found, inserted] = m_pathEntries.try_emplace(key);
    if (inserted) {
        std::error_code error;
        found->second = {id,
                         id.key,
                         std::filesystem::path(id.key).filename().string(),
                         SceneRole::Sample,
                         "scene document",
                         std::filesystem::is_regular_file(id.key, error),
                         "check the document path"};
    }
    return found->second;
}
//======================================================================================================================
asset::AssetResult<std::filesystem::path> SceneLibrary::documentPath(const SceneId& id) const {
    if (id.isCatalog())
        return catalogDocumentPath(id.key);
    return std::filesystem::path(id.key);
}
//======================================================================================================================
std::string SceneLibrary::cacheKey(const SceneId& id) const {
    const auto path = documentPath(id);
    if (!path)
        return id.key;
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(*path, error);
    return error ? path->lexically_normal().string() : canonical.string();
}
//======================================================================================================================
engine::LoadedScene* SceneLibrary::loaded(const SceneId& id) {
    const auto found = m_scenes.find(cacheKey(id));
    return found == m_scenes.end() ? nullptr : &found->second;
}
//======================================================================================================================
asset::AssetResult<engine::Scene*> SceneLibrary::get(const SceneId& id) {
    if (auto* existing = loaded(id))
        return existing->scene.get();
    auto result = reload(id, {});
    if (!result)
        return std::unexpected(result.error());
    return (*result)->scene.get();
}
//======================================================================================================================
asset::AssetResult<engine::LoadedScene*>
SceneLibrary::reload(const SceneId& id,
                     const std::function<void(const engine::LoadedScene&)>& beforeReplace) {
    const auto path = documentPath(id);
    if (!path)
        return std::unexpected(path.error());
    auto replacement = loadSceneDocument(m_device, *path, m_overrides);
    if (!replacement)
        return std::unexpected(replacement.error());
    const auto key = cacheKey(id);
    auto old = m_scenes.find(key);
    if (old != m_scenes.end()) {
        m_device.waitIdle();
        if (beforeReplace)
            beforeReplace(old->second);
        old->second = std::move(*replacement);
    } else
        old = m_scenes.emplace(key, std::move(*replacement)).first;
    if (!id.isCatalog()) {
        entry(id);
        auto& metadata = m_pathEntries.at(key);
        metadata.displayName = old->second.document.name;
        metadata.available = true;
        metadata.hint.clear();
    }
    return &old->second;
}
} // namespace lmx::scenes
