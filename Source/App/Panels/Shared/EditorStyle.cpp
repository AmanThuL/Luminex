//----------------------------------------------------------------------------------------------------------------------
/// @file EditorStyle.cpp
/// @brief Draws shared editor actions, property grids and transient notices.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Shared/EditorStyle.h"

#include "App/Panels/Shared/ActionFeedback.h"

#include <algorithm>
#include <string>

namespace lmx::app::editor_style {
namespace {

bool iconFontAvailable = false;

} // namespace

//======================================================================================================================
void setIconFontAvailable(bool available) {
    iconFontAvailable = available;
}

//======================================================================================================================
float iconButtonWidth(EditorIcon icon) {
    return iconFontAvailable ? ImGui::GetFrameHeight()
                             : std::max(ImGui::GetFrameHeight(),
                                        ImGui::CalcTextSize(editorIconInfo(icon).label.data()).x +
                                            2.0f * ImGui::GetStyle().FramePadding.x);
}

//======================================================================================================================
bool iconButton(const char* id, EditorIcon icon, bool enabled, const char* tooltip) {
    const auto info = editorIconInfo(icon);
    const std::string label =
        iconFontAvailable ? encodeUtf8(info.codepoint) : std::string(info.label);
    const float height = ImGui::GetFrameHeight();
    const float width = iconButtonWidth(icon);
    ImGui::PushID(id);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::Button(label.c_str(), {width, height});
    ImGui::EndDisabled();
    editorTooltip(tooltip ? tooltip : info.label.data());
    ImGui::PopID();
    return clicked;
}

//======================================================================================================================
void beginHeaderRow() {
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
}

//======================================================================================================================
void endHeaderRow() {
    ImGui::EndGroup();
}

//======================================================================================================================
bool overflowMenu(const char* id) {
    if (iconButton(id, EditorIcon::More, true, "More actions")) {
        ImGui::OpenPopup(id);
    }
    return ImGui::BeginPopup(id);
}

//======================================================================================================================
bool beginPropertyGrid(const char* id) {
    return beginFields(id, kPropertyGridMinWidth);
}

//======================================================================================================================
bool beginDiagnostics() {
    return ImGui::CollapsingHeader("Diagnostics");
}

//======================================================================================================================
void nextInRow(float width) {
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (right - ImGui::GetItemRectMax().x >= width + ImGui::GetStyle().ItemSpacing.x) {
        ImGui::SameLine();
    }
}

//======================================================================================================================
void drawNotice(NoticeQueue& notices, double nowSeconds) {
    auto* result = notices.current(nowSeconds);
    if (!result) {
        return;
    }
    const auto* viewport = ImGui::GetMainViewport();
    const float margin = scaled(kSpaceLarge);
    const float width = std::min(scaled(420.0f), viewport->WorkSize.x - 2.0f * margin);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x - margin,
                             viewport->WorkPos.y + viewport->WorkSize.y - margin},
                            ImGuiCond_Always, {1.0f, 1.0f});
    ImGui::SetNextWindowSize({width, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavFocus;
    if (ImGui::Begin("Notice##Editor", nullptr, kFlags)) {
        const float closeWidth = iconButtonWidth(EditorIcon::Close);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
                             closeWidth);
        if (iconButton("dismiss", EditorIcon::Close, true, "Dismiss notice")) {
            notices.dismiss();
        }
        drawActionFeedback("result", *result);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace lmx::app::editor_style
