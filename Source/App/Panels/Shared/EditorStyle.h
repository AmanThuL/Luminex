//----------------------------------------------------------------------------------------------------------------------
/// @file EditorStyle.h
/// @brief Shares responsive field rows and readable state treatments across editor panels.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <cfloat>

namespace lmx::app::editor_style {

/// Small gap in logical points.
inline constexpr float kSpaceSmall = 4.0f;
/// Standard gap in logical points.
inline constexpr float kSpaceMedium = 8.0f;
/// Section gap in logical points.
inline constexpr float kSpaceLarge = 12.0f;
/// Control row target in logical points.
inline constexpr float kControlHeight = 24.0f;
/// Selection and active-mode text color in the encoded UI domain.
inline const ImVec4 kAccent{0.40f, 0.72f, 0.95f, 1.0f};
/// Warning text color; always accompanied by an explanation.
inline const ImVec4 kWarning{1.0f, 0.76f, 0.38f, 1.0f};
/// Secondary readable text color.
inline const ImVec4 kMuted{0.65f, 0.69f, 0.74f, 1.0f};

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
    ImGui::PushStyleColor(ImGuiCol_Text, warning ? kWarning : kMuted);
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
