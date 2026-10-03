//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorSubject.h
/// @brief Declares Inspector enabled-state routing for document and generated subjects.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Scene/EditorSelection.h"
#include "App/Model/Scene/SceneSession.h"

#include <cstddef>
#include <optional>
#include <string>

namespace lmx::app {

/// Current own flag, inherited result and reset value for one selectable subject. A generatedBy
/// name marks a session-only generator child; source-object labels state their primitive fanout.
struct InspectorEnabledState {
    std::string label;         ///< Subject name shown in the header.
    std::string kind;          ///< Document group, imported source, object or light kind.
    std::string generatedBy;   ///< Generator name, empty for saved document subjects.
    bool own = true;           ///< Authored or session own flag, independent of ancestors.
    bool effective = true;     ///< Result after all ancestor and CLI masks.
    bool baseline = true;      ///< Loaded or adopted authored reset value.
    size_t primitiveCount = 0; ///< Number of imported material primitives controlled together.
};

/// Resolves an enabled header subject, or null for Environment, root and stale selections.
std::optional<InspectorEnabledState> inspectorEnabledState(const SceneSession& session,
                                                           const EditorSelection& selection);

/// Loaded mobility for saved objects and authored lights; absent for generated or other subjects.
/// Inherited mobility is independent of animation and measurement pose locks.
std::optional<bool> inspectorIsStatic(const engine::LoadedScene& loaded,
                                      const EditorSelection& selection);
/// Resolves authored mobility from the active loaded scene, without changing it.
std::optional<bool> inspectorIsStatic(const SceneSession& session,
                                      const EditorSelection& selection);
/// Compares the subject's fields with saved reset baselines; camera navigation alone is excluded.
bool inspectorSubjectEdited(const SceneSession& session, const EditorSelection& selection);

/// Changes only the selected subject's own flag through SceneSession's validated edit path.
rojoRHI::Result<void> setInspectorEnabled(SceneSession& session, const EditorSelection& selection,
                                          bool enabled);

/// Restores the selected subject's loaded/saved own flag without changing its other fields.
rojoRHI::Result<void> resetInspectorEnabled(SceneSession& session,
                                            const EditorSelection& selection);

} // namespace lmx::app
