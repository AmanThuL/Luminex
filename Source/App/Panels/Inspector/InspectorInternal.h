//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorInternal.h
/// @brief Declares private section and row boundaries for the Inspector panel.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/Workspace/Provenance.h"
#include "App/Panels/Inspector/InspectorPanel.h"

#include <cstddef>
#include <string>

namespace lmx::app {

/// Draws the subject name, kind, optional enable checkbox and reset icon; the icon is enabled only
/// when `changed` reports something to restore. Returns whether reset was clicked.
bool drawInspectorHeader(const char* name, const char* kind, const char* resetTooltip, bool changed,
                         bool* enabled = nullptr, const std::optional<ProvenanceMark>& mark = {},
                         const std::optional<ProvenanceMark>& enabledMark = {});
/// Compares this subject's editable values with its existing reset baseline, excluding playback.
bool inspectorSubjectEdited(const SceneSession& session, const EditorSelection& selection);
/// Classifies current generated, preview, edited and effective CLI-mask sources without mutation.
/// enabled includes inherited CLI masking; preview applies only to unsaved animation-owned edits.
std::optional<ProvenanceMark> inspectorProvenance(const SceneSession& session,
                                                  const EditorSelection& selection, bool edited,
                                                  std::string_view field = {}, bool enabled = false,
                                                  bool preview = false);
/// Supplies a mark for the next property label using that field's baseline comparison.
void markInspectorField(const InspectorPanelContext& context, bool edited, std::string_view field,
                        bool preview = false);
void beginFieldRow(const char* label);
void valueRow(const char* label, const std::string& value,
              const std::optional<ProvenanceMark>& mark = {});
/// Camera property identity used only to attribute the existing view and saved request.
enum class CameraProvenanceField {
    Subject,  ///< Whole view; may combine automatic pose and manual lens edits.
    Position, ///< Rail-owned world position.
    Yaw,      ///< Rail-owned yaw.
    Pitch,    ///< Rail-owned pitch.
    FovY,     ///< Operator-controlled lens.
    NearZ,    ///< Operator-controlled near clip.
    FarZ,     ///< Operator-controlled far clip.
    Speed,    ///< Session-only fly speed.
};
/// Attributes a changed camera field from existing saved requests and current rail samples.
/// label is borrowed for the call and copied into the result. Unfollowed rail-scene poses have
/// no reliable origin and return no actor claim; lens and speed remain independently attributed.
std::optional<ProvenanceMark> inspectorCameraProvenance(const SceneSession& session,
                                                        bool followRail, bool edited,
                                                        CameraProvenanceField field,
                                                        std::string_view label);
void drawCameraSection(const InspectorPanelContext& context);
void drawDirectionalLightSection(const InspectorPanelContext& context, size_t index);
void drawObjectSection(const InspectorPanelContext& context, size_t index);
void drawGroupSection(const InspectorPanelContext& context);
void drawEnvironmentSection(const InspectorPanelContext& context);

} // namespace lmx::app
