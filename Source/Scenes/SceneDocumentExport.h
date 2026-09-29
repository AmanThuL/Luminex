//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentExport.h
/// @brief Declares pure saved-scene derivation and canonical byte comparison.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Scenes/SceneDocuments.h"

#include <string>
#include <vector>

namespace lmx::scenes {

/// One orientation saved as the nearest quaternion because no exact glTF preimage exists.
struct ExportApproximation {
    uint32_t node = 0; ///< Document node whose rotation was approximated.
    std::string what;  ///< Names the value, such as "spot light direction" or "scene camera".
};
/// Approximations made by one export, in node order.
struct ExportReport {
    std::vector<ExportApproximation> approximations; ///< Empty when every value was exact.
};

/// Derives a saved model from live persistent subjects, preserving every unchanged loaded value.
/// The scene and state must belong to loaded; capture state before editing/playback. Generated
/// edits, animated imported poses and the editor view are excluded. No disk or GPU work occurs.
/// A finite unit direction or camera without an exact glTF quaternion (steep pitches have none)
/// saves the nearest seed quaternion instead; report, when supplied, records each one so the
/// caller can log it and adopt the decoded value. Nonfinite or non-unit orientations, divergent
/// source primitives, nonfinite object poses/look values and invalid directional strengths fail
/// explicitly. A failed export is dirty and must prevent Save; the caller surfaces its AssetError.
asset::AssetResult<asset::SceneDocument> exportSceneDocument(const engine::LoadedScene& loaded,
                                                             const engine::Scene& scene,
                                                             const SessionDocumentState& state,
                                                             ExportReport* report = nullptr);

/// Compares canonical JSON and animation-buffer bytes for two valid models using one neutral
/// buffer URI. Source formatting, paths and read warnings do not participate in dirty state.
bool documentDirty(const asset::SceneDocument& loaded, const asset::SceneDocument& exported);

} // namespace lmx::scenes
