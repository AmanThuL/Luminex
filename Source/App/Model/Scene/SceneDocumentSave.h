//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentSave.h
/// @brief Declares verified save and atomic application adoption of a live document.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/Scene/EditorSelection.h"
#include "App/Model/Scene/SceneSession.h"
#include "Scenes/SceneLibrary.h"

namespace lmx::app {
/// File operations used by save; empty callables use the production asset reader/writer/hash.
/// Overrides support deterministic filesystem failure testing without mutating live state.
struct SceneDocumentSaveIO {
    std::function<asset::AssetResult<void>(const asset::SceneDocument&,
                                           const std::filesystem::path&)>
        write; ///< Writes both files transactionally for ordinary reported write failures.
    std::function<asset::AssetResult<asset::SceneDocument>(const std::filesystem::path&)>
        read; ///< Reads the completed canonical model before adoption.
    std::function<asset::AssetResult<std::string>(const std::filesystem::path&)>
        hash; ///< Hashes the completed glTF and companion before adoption.
};
/// Exports, writes, reads and hashes before publishing any new metadata or reset baseline.
/// Failure preserves all live/session/library state; read/hash failures can follow a completed disk
/// save and report that error without adoption. Save As rejects aliases of the active file or bin.
/// The caller requires stopped playback and no Measure. Active identity changes only on success;
/// same-scene authored flags, immutable imported baselines, live IDs and generation are preserved.
asset::AssetResult<void> saveSessionDocument(scenes::SceneLibrary& library, SceneSession& session,
                                             scenes::SceneId& activeId,
                                             const std::filesystem::path& path, bool saveAs,
                                             const SceneDocumentSaveIO& io = {});
/// Constructs a fresh target before discarding any current edits, even for cached paths or Revert.
/// Failure retains the active scene/session/id. On success beforeDeactivate stops any playback
/// against the still-live old scene, then old defaults/bindings are invalidated and the target is
/// activated. The shell resets selection only after this succeeds.
asset::AssetResult<void> replaceSessionDocument(
    scenes::SceneLibrary& library, SceneSession& session, scenes::SceneId& activeId,
    const scenes::SceneId& target, const std::function<void()>& beforeDeactivate,
    const std::function<asset::AssetResult<void>(const engine::LoadedScene&)>& validate = {});

/// Checks whether a proposed document retains the selected semantic subject. A removed or
/// retyped subject must be reviewed after the operator explicitly changes selection.
bool canPreserveSessionSelection(const EditorSelection& selection,
                                 const engine::LoadedScene& loaded,
                                 const asset::SceneDocument& proposed);

/// Reloads a stopped session document while retaining its camera and selected semantic subject.
/// A supplied proposal and its expected hash are checked before replacement; failure leaves
/// session and selection intact. The optional verifier is a deterministic I/O test seam.
asset::AssetResult<void> replaceSessionDocumentPreservingView(
    scenes::SceneLibrary& library, SceneSession& session, scenes::SceneId& activeId,
    EditorSelection& selection, const std::optional<asset::SceneDocument>& proposal = {},
    std::string_view expectedHash = {},
    const std::function<asset::AssetResult<std::string>(const std::filesystem::path&)>& verifyHash =
        {});
} // namespace lmx::app
