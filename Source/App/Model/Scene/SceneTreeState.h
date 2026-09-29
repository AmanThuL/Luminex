//----------------------------------------------------------------------------------------------------------------------
/// @file SceneTreeState.h
/// @brief Declares per-scene Hierarchy expansion and a cached tree build, independent of ImGui.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Scene/SceneTree.h"

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>

namespace lmx::app {

/// Hashes everything a built tree shows that edit generations do not cover: own and effective
/// enabled flags of nodes, imported nodes, objects and lights, plus the object and light
/// populations. One allocation-free pass with no label formatting.
uint64_t sceneTreeFingerprint(const engine::LoadedScene& loaded,
                              const scenes::SessionDocumentState& state,
                              const SceneSession* session);

/// Borrowed inputs for one tree lookup.
struct SceneTreeInputs {
    const engine::LoadedScene& loaded;         ///< Document, binding and scene.
    const scenes::SessionDocumentState& state; ///< Document enabled state.
    const SceneSession* session = nullptr;     ///< Current own/effective flags, or null.
    std::string_view filter;                   ///< Search text.
    uint64_t sceneGeneration = 0;              ///< Active scene identity revision.
    uint64_t editGeneration = 0;               ///< Persistent edit counter.
};

/// Owns collapse choices by scene key and the last built tree. A lookup rebuilds only when the
/// scene identity, either generation, the enabled-state fingerprint, the filter or the scene's
/// collapse choices changed since the previous build.
class SceneTreeState {
public:
    /// Returns the cached tree, rebuilding it when the key changed. The reference stays valid
    /// until the next lookup, collapse change or rekey.
    const SceneTreeView& view(const std::string& sceneKey, const SceneTreeInputs& inputs);
    /// Whether a group key (document node or imported key) is collapsed for this scene.
    bool collapsed(const std::string& sceneKey, uint32_t key) const;
    /// Records a group toggle; only a changed value invalidates the cached tree.
    void setCollapsed(const std::string& sceneKey, uint32_t key, bool collapsed);
    /// Whether the scene's root row is collapsed.
    bool rootCollapsed(const std::string& sceneKey) const;
    /// Records the root toggle. The root never changes built rows, so nothing is invalidated.
    void setRootCollapsed(const std::string& sceneKey, bool collapsed);
    /// Moves a scene's choices to its new key after Save As adopts another document path.
    void rekey(const std::string& from, const std::string& to);
    /// Number of tree builds performed, for tests and diagnostics.
    size_t buildCount() const { return m_buildCount; }

private:
    struct Expansion {
        std::set<uint32_t> collapsed;
        bool rootCollapsed = false;
        uint64_t version = 0;
    };
    struct Key {
        std::string sceneKey;
        const engine::LoadedScene* loaded = nullptr;
        uint64_t sceneGeneration = 0;
        uint64_t editGeneration = 0;
        uint64_t fingerprint = 0;
        uint64_t expansionVersion = 0;
        std::string filter;
        bool operator==(const Key&) const = default;
    };
    std::unordered_map<std::string, Expansion> m_expansion;
    Key m_key;
    SceneTreeView m_view;
    bool m_valid = false;
    size_t m_buildCount = 0;
};

} // namespace lmx::app
