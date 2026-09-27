//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentExport.h
/// @brief Declares pure saved-scene derivation and canonical byte comparison.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Scenes/SceneDocuments.h"

namespace lmx::scenes {

/// Derives a saved model from live persistent subjects, preserving every unchanged loaded value.
/// The scene and state must belong to loaded; capture state before editing/playback. Generated
/// edits, animated imported poses and the editor view are excluded. No disk or GPU work occurs.
/// Exact orientation conversion failures return Unsupported with a node pointer; divergent source
/// primitives, nonfinite object poses and invalid directional strengths fail explicitly.
/// A failed export is dirty and must prevent Save; the caller surfaces its AssetError.
asset::AssetResult<asset::SceneDocument> exportSceneDocument(const engine::LoadedScene& loaded,
                                                             const engine::Scene& scene,
                                                             const SessionDocumentState& state);

/// Compares canonical JSON and animation-buffer bytes for two valid models using one neutral
/// buffer URI. Source formatting, paths and read warnings do not participate in dirty state.
bool documentDirty(const asset::SceneDocument& loaded, const asset::SceneDocument& exported);

} // namespace lmx::scenes
