#pragma once

#include "Engine/Scene.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::engine {

struct SceneId {
    size_t catalogIndex = 0;
    friend bool operator==(SceneId, SceneId) = default;
};

enum class SceneRole {
    Showcase,
    Sample,
    Diagnostic,
};

struct SceneEntry {
    SceneId id;
    std::string_view stableId;
    std::string_view displayName;
    SceneRole role;
    std::string_view assetRequirement;
    bool available = false;
    std::string hint;
};

SceneId defaultSceneId();
std::optional<SceneId> parseSceneId(std::string_view stableId);
std::string_view sceneIdString(SceneId id);

class SceneLibrary {
public:
    explicit SceneLibrary(rhi::Device& device);

    std::span<const SceneEntry> entries() const;
    const SceneEntry& entry(SceneId id) const;
    AssetResult<Scene*> get(SceneId id);

private:
    rhi::Device& m_device;
    std::vector<SceneEntry> m_entries;
    std::vector<std::unique_ptr<Scene>> m_scenes;
};

} // namespace lmx::engine
