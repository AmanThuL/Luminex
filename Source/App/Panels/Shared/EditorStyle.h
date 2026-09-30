//----------------------------------------------------------------------------------------------------------------------
/// @file EditorStyle.h
/// @brief Shares responsive field rows and readable state treatments across editor panels.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Capture/NoticeQueue.h"
#include "App/Model/Workspace/EditorIcon.h"
#include "App/Model/Workspace/EditorTheme.h"
#include "App/Model/Workspace/EditorThemeTokens.h"
#include "App/Shell/EditorFont.h"

#include <imgui.h>

#include <cfloat>

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

/// Minimum property-grid width in base UI points before labels stack above values.
inline constexpr float kPropertyGridMinWidth = 260.0f;

/// Width of an icon button at the current font and scale, including its labelled fallback.
float iconButtonWidth(EditorIcon icon);
/// Draws a square glyph button, or a label-sized fallback, with delayed help when disabled too.
bool iconButton(const char* id, EditorIcon icon, bool enabled, const char* tooltip);
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
    }
    return true;
}

/// Starts a labeled row and sizes its following widget to the available width.
inline void field(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", label);
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
                             ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::PushID(id);
    bool changed = false;
    if (ImGui::CalcTextSize(label).x <= labelWidth) {
        changed = ImGui::Checkbox(label, value);
    } else {
        changed = ImGui::Checkbox("##value", value);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextWrapped("%s", label);
    }
    ImGui::PopID();
    return changed;
}

/// Draws explicitly labeled XYZ or RGB components with enough width for each scalar value.
inline bool vector3(const char* label, const char* id, float* values, float speed,
                    float minimum = 0.0f, float maximum = 0.0f, const char* format = "%.3f",
                    ImGuiSliderFlags flags = 0, bool rgb = false) {
    field(label);
    const int columns = ImGui::GetContentRegionAvail().x >= scaled(300.0f) ? 3 : 1;
    bool changed = false;
    if (ImGui::BeginTable(id, columns, ImGuiTableFlags_SizingStretchSame)) {
        constexpr const char* kAxes[] = {"X", "Y", "Z"};
        constexpr const char* kChannels[] = {"R", "G", "B"};
        for (int axis = 0; axis < 3; ++axis) {
            ImGui::TableNextColumn();
            ImGui::PushID(axis);
            ImGui::TextUnformatted(rgb ? kChannels[axis] : kAxes[axis]);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::DragFloat("##component", values + axis, speed, minimum, maximum,
                                        format, flags);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    return changed;
}

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
