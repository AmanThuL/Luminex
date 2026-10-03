//----------------------------------------------------------------------------------------------------------------------
/// @file EditorStyle.h
/// @brief Shares responsive field rows and readable state treatments across editor panels.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Capture/NoticeQueue.h"
#include "App/Model/Session/SessionProposal.h"
#include "App/Model/Workspace/ActivityModel.h"
#include "App/Model/Workspace/EditorIcon.h"
#include "App/Model/Workspace/EditorTheme.h"
#include "App/Model/Workspace/EditorThemeTokens.h"
#include "App/Shell/EditorFont.h"

#include <imgui.h>

#include <cfloat>
#include <string_view>

namespace lmx::app::editor_style {

/// Borrows atlas faces on the UI thread; call again with {} before destroying the context.
void setEditorFonts(const EditorFonts& fonts);

/// Applies a semantic face and unscaled size until destruction; never rebuilds the atlas.
class ScopedType {
public:
    /// Pushes the requested role, resolving unavailable faces to ImGui's default font.
    explicit ScopedType(TypeRole role);
    /// Restores the previous face and size.
    ~ScopedType();
    ScopedType(const ScopedType&) = delete;            ///< Font stack scopes cannot be copied.
    ScopedType& operator=(const ScopedType&) = delete; ///< Font stack scopes cannot be assigned.
};

/// Begins an editor-owned tab with Medium when selected, reserving width for either weight.
/// Pair success with ImGui::EndTabItem.
/// Dock tabs are drawn by ImGui and retain Regular.
bool beginTabItem(const char* label, ImGuiTabItemFlags flags = 0);

/// Copies the frame's palette; call on the UI thread before building any panels.
void setActivePalette(const ThemePalette& palette);
/// Returns the owned frame palette; valid until the next setActivePalette call.
const ThemePalette& activePalette();
/// Returns an encoded-sRGB semantic color with straight alpha.
ImVec4 color(ThemeRole role);
/// Packs an encoded color, multiplying its alpha by alphaScale in [0, 1].
ImU32 colorU32(ThemeRole role, float alphaScale = 1.0f);

/// Actor symbol diameter in base UI points.
inline constexpr float kActorMarkSize = 8.0f;
/// Draws a filled operator dot, system ring or software diamond in the actor's semantic role.
/// size is a positive diameter in base UI points; this UI-thread item borrows no state.
void actorMark(Actor actor, float size = kActorMarkSize);
/// Draws a compact actor-labelled chip using the actor's semantic ink and symbol.
void actorChip(Actor actor, std::string_view label);
/// Outlines a screen-space item rectangle with the 2 pt software attention stroke.
void attentionRing(ImVec2 min, ImVec2 max);
/// Draws a non-editable before/after row; an empty field omits the inline field name.
void proposedValue(std::string_view field, std::string_view before, std::string_view after);

/// Action selected from a proposal card in the current UI frame.
enum class CardAction {
    None,   ///< No action was selected.
    Show,   ///< Reveal the proposal's change details.
    Accept, ///< Request operator acceptance.
    Reject  ///< Request operator rejection.
};

/// Visible action labels and optional card details supplied by the caller.
struct CardLabels {
    const char* accept = "Accept";        ///< Primary action label.
    const char* reject = "Reject";        ///< Secondary action label.
    bool show = true;                     ///< Whether the Show action is available.
    std::string acceptDisabledReason;     ///< Explanation when acceptance is unavailable.
    bool showDetails = false;             ///< Include change rows and evidence links in the card.
    const char* appliedMessage = nullptr; ///< Optional status text for an applied card.
    const char* appliedAction = nullptr;  ///< Optional action label for an applied card.
    const char* footer = nullptr;         ///< Optional explanatory text after the controls.
};

/// Draws a proposal card and reports only a clicked action; it never changes proposal state.
CardAction proposalCard(const SessionProposal& proposal, const CardLabels& labels = {});
/// Draws the provenance symbol with a source tooltip; authored values emit no item.
/// Session-only marks underline the preceding item's label and retain its source text unchanged.
/// overlay places the symbol inside the preceding row without changing its hit target or layout;
/// the row consumer supplies a combined source/status tooltip in that mode.
void provenanceMark(const ProvenanceMark& mark, bool overlay = false);
/// Full or contracted strip width in scaled UI points, including its optional Stop button.
float activityStripWidth(const Activity& activity, bool showVerb = true);
/// Draws one nonwrapping activity, with a 2 pt progress bar; true requests the existing Stop path.
/// showVerb contracts only the verb. The actor and stoppable action always remain visible.
bool activityStrip(const Activity& activity, bool showVerb = true);
/// Sets owned provenance for the next field label only; consumed by field or checkbox.
void setNextFieldProvenance(std::optional<ProvenanceMark> mark);
/// Draws and clears the pending field mark beside the preceding label, if any.
void consumeFieldProvenance();
/// Space always reserved beside a field label for a mark, including the gap, so a mark that
/// appears or clears never changes the row's layout.
float fieldProvenanceWidth();

/// Minimum property-grid width in base UI points before labels stack above values.
inline constexpr float kPropertyGridMinWidth = 260.0f;

/// Width of an icon button at the current font and scale, including its labelled fallback.
/// fallbackLabel overrides the action text when the glyph is unavailable; null uses the icon label.
float iconButtonWidth(EditorIcon icon, const char* fallbackLabel = nullptr);
/// Draws a square glyph button, or a label-sized fallback, with delayed help when disabled too.
/// fallbackLabel uses the same width policy as iconButtonWidth and is borrowed only for this call.
bool iconButton(const char* id, EditorIcon icon, bool enabled, const char* tooltip,
                const char* fallbackLabel = nullptr);
/// Draws an accent-filled primary action in the strong body face; true when activated.
bool primaryButton(const char* label);
/// Draws a neutral topic header; does not use the selected-row background.
bool collapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0);
/// Begins a panel's grouped action row; place following controls with nextInRow or SameLine.
void beginHeaderRow();
/// Ends the current panel header group.
void endHeaderRow();
/// Opens a More-button popup; the caller calls ImGui::EndPopup when this returns true.
bool overflowMenu(const char* id);
/// Begins a property grid using the shared reflow threshold; pair success with endFields.
bool beginPropertyGrid(const char* id);
/// Opens the collapsed-by-default Diagnostics tree node, returning whether to draw its contents;
/// pair a true result with endDiagnostics.
bool beginDiagnostics();
/// Closes a Diagnostics section that beginDiagnostics opened.
void endDiagnostics();
/// Keeps the next item on this line if its width fits, otherwise leaves it on the next line.
void nextInRow(float width);
/// Draws the current notice at the main viewport's bottom-right work-area corner.
void drawNotice(NoticeQueue& notices, double nowSeconds);

/// Small gap in logical points.
inline constexpr float kSpaceSmall = 4.0f;
/// Standard gap in logical points.
inline constexpr float kSpaceMedium = 8.0f;
/// Section gap in logical points.
inline constexpr float kSpaceLarge = 12.0f;
/// Control row target in logical points.
inline constexpr float kControlHeight = 24.0f;
/// Converts a base UI measurement to the user's global editor scale (not framebuffer pixels).
inline float scaled(float points) {
    return points * ImGui::GetStyle().FontScaleMain;
}

/// Begins a responsive field table; call endFields only when this returns true. The optional
/// two-column threshold is in base UI points; narrower tables stack labels above controls.
inline bool beginFields(const char* id, float twoColumnWidth = 360.0f) {
    const bool wide = ImGui::GetContentRegionAvail().x >= scaled(twoColumnWidth);
    if (!ImGui::BeginTable(id, wide ? 2 : 1, ImGuiTableFlags_SizingStretchProp)) {
        return false;
    }
    if (wide) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.43f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.57f);
    } else {
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    }
    return true;
}

/// Starts a labeled row and sizes its following widget to the available width.
inline void field(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    const float labelWidth = ImGui::GetContentRegionAvail().x - fieldProvenanceWidth();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (labelWidth > 0 ? labelWidth : 1.0f));
    ImGui::TextUnformatted(label);
    ImGui::PopTextWrapPos();
    consumeFieldProvenance();
    if (ImGui::TableGetColumnCount() == 1) {
        ImGui::TableNextRow();
    }
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

/// Ends a successfully opened field table.
inline void endFields() {
    ImGui::EndTable();
}

/// Draws a wrapped, non-editable value in the current responsive field table.
inline void readOnly(const char* label, const char* value) {
    field(label);
    ImGui::TextWrapped("%s", value);
}

/// Draws a wrapped explanation with secondary or warning text emphasis.
inline void message(const char* text, bool warning = false) {
    ImGui::PushStyleColor(ImGuiCol_Text,
                          color(warning ? ThemeRole::StatusWarning : ThemeRole::TextSecondary));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

/// Draws a clamped scalar edit in the current field table; true when edited.
inline bool slider(const char* label, const char* id, float* value, float minimum, float maximum,
                   const char* format = "%.2f") {
    field(label);
    return ImGui::SliderFloat(id, value, minimum, maximum, format, ImGuiSliderFlags_AlwaysClamp);
}

/// Draws a boolean edit in the current field table; true when toggled.
inline bool checkbox(const char* label, const char* id, bool* value) {
    if (ImGui::TableGetColumnCount() > 1) {
        field(label);
        return ImGui::Checkbox(id, value);
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    const float labelWidth = ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() -
                             ImGui::GetStyle().ItemInnerSpacing.x - fieldProvenanceWidth();
    ImGui::PushID(id);
    bool changed = false;
    if (ImGui::CalcTextSize(label).x <= labelWidth) {
        changed = ImGui::Checkbox(label, value);
    } else {
        changed = ImGui::Checkbox("##value", value);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        const float wrappedWidth = ImGui::GetContentRegionAvail().x - fieldProvenanceWidth();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (wrappedWidth > 0 ? wrappedWidth : 1.0f));
        ImGui::TextUnformatted(label);
        ImGui::PopTextWrapPos();
    }
    consumeFieldProvenance();
    ImGui::PopID();
    return changed;
}

/// Draws a label and its explicitly labeled XYZ or RGB components on one row when the grid has
/// room for three fields; otherwise the components stack beside or below the label so no value
/// clips. An optional tooltip explains the label and every component. True when edited.
bool vector3(const char* label, const char* id, float* values, float speed, float minimum = 0.0f,
             float maximum = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0,
             bool rgb = false, const char* tooltip = nullptr);
/// Draws an encoded color as labeled R, G and B components in [0, 1] followed by a swatch that
/// opens the picker. values holds three floats, or four when alpha is set; alpha is edited in
/// the picker. Returns whether any component changed this frame.
bool colorRgb(const char* label, const char* id, float* values, bool alpha = false,
              const char* tooltip = nullptr);

} // namespace lmx::app::editor_style

namespace lmx::app {

/// Shows delayed contextual help for the preceding item, including disabled controls.
inline void editorTooltip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

} // namespace lmx::app
