//----------------------------------------------------------------------------------------------------------------------
/// @file SceneTree.h
/// @brief Declares a document-ordered, selectable scene tree independent of ImGui.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Scene/EditorSelection.h"
#include "Engine/Scene/SceneInstantiate.h"
#include "Scenes/SceneDocuments.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::app {

class SceneSession;

/// One visible document, imported-source or generated subject. `node` is a document index;
/// `importedNode` separately names an imported source binding, never a document node.
struct SceneTreeRow {
    EditorSubject subject = EditorSubject::None; ///< Subject selected by this row.
    size_t index = 0;                            ///< Representative object or directional slot.
    engine::LightId lightId{};                   ///< Complete local-light identity when applicable.
    uint32_t node = engine::kGeneratedNode;      ///< Document node or sentinel for the root/look.
    uint32_t importedNode = engine::kGeneratedNode; ///< Source binding index or sentinel.
    uint32_t depth = 0;                             ///< Root is zero; each parent adds one.
    std::string label;      ///< Display name, including primitive fanout scope.
    bool group = false;     ///< Has structural or generated children.
    bool generated = false; ///< Session-only generated subject.
    bool enabled = true;    ///< Authored or session-only own flag.
    bool effective = true;  ///< Ancestor AND including session masks.
};

/// Visible rows plus counts before collapse: search counts include retained ancestors and every
/// disabled subject, while total counts the whole document/generated population.
struct SceneTreeView {
    std::vector<SceneTreeRow> rows; ///< Flattened rows currently visible under collapse/search.
    size_t matchedCount = 0;        ///< Retained subject rows before collapse.
    size_t totalCount = 0;          ///< All subject rows before search or collapse.
};

/// Collapse keys for imported nodes occupy the high half, disjoint from document indices.
uint32_t sceneTreeImportedKey(uint32_t importedNode);

/// Builds rows and both counts in one traversal of the live scene population.
SceneTreeView buildSceneTreeView(const engine::LoadedScene& loaded,
                                 const scenes::SessionDocumentState& state, std::string_view filter,
                                 const std::set<uint32_t>& collapsed,
                                 const SceneSession* session = nullptr);

/// Builds only rows visible under filter and collapsed groups. Search retains ancestors and
/// temporarily reveals matching descendants. The optional session supplies current generated own
/// flags and CLI masks; null uses the immutable binding's initial state for CPU fixtures.
std::vector<SceneTreeRow> buildSceneTree(const engine::LoadedScene& loaded,
                                         const scenes::SessionDocumentState& state,
                                         std::string_view filter,
                                         const std::set<uint32_t>& collapsed,
                                         const SceneSession* session = nullptr);

/// Counts selectable subjects in already built rows, including disabled subjects and Environment.
size_t sceneTreeSubjectCount(std::span<const SceneTreeRow> rows);

/// Compares complete document/source/generated selection identity for one visible row.
bool sceneTreeRowSelected(const SceneTreeRow& row, const EditorSelection& selection);

/// Reports whether a selection is absent from the current filtered document rows. Camera and
/// Environment selections retain their Inspector context even when their rows are filtered out.
bool sceneTreeSelectionHidden(std::span<const SceneTreeRow> rows, const EditorSelection& selection,
                              std::string_view filter);

/// Returns the next keyboard row. An absent/hidden selection enters at the first visible row.
std::optional<SceneTreeRow> sceneTreeKeyboardTarget(std::span<const SceneTreeRow> rows,
                                                    const EditorSelection& selection, bool down);

/// Search temporarily reveals a collapsed root without changing its stored collapse choice.
bool sceneTreeRootOpen(bool collapsed, std::string_view filter);

/// Records a user root toggle only outside search; searching preserves the prior choice.
bool sceneTreeRootCollapsedAfterDraw(bool collapsed, bool opened, std::string_view filter);

} // namespace lmx::app
