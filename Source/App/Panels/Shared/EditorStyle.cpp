//----------------------------------------------------------------------------------------------------------------------
/// @file EditorStyle.cpp
/// @brief Draws shared editor actions, property grids and transient notices.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Shared/EditorStyle.h"

#include "App/Panels/Shared/ActionFeedback.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace lmx::app::editor_style {
namespace {

bool iconFontAvailable = false;
EditorFonts editorFonts;
ThemePalette palette = kDarkPalette;

//======================================================================================================================
ImVec4 glyphInkBounds(const ImFontGlyph& glyph, const ImTextureData& texture) {
    if (!texture.Pixels)
        return {glyph.X0, glyph.Y0, glyph.X1, glyph.Y1};
    const int x0 =
        std::clamp(static_cast<int>(std::round(glyph.U0 * texture.Width)), 0, texture.Width);
    const int y0 =
        std::clamp(static_cast<int>(std::round(glyph.V0 * texture.Height)), 0, texture.Height);
    const int x1 =
        std::clamp(static_cast<int>(std::round(glyph.U1 * texture.Width)), 0, texture.Width);
    const int y1 =
        std::clamp(static_cast<int>(std::round(glyph.V1 * texture.Height)), 0, texture.Height);
    int left = x1, top = y1, right = x0, bottom = y0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const auto alpha =
                texture.Pixels[(y * texture.Width + x + 1) * texture.BytesPerPixel - 1];
            if (alpha == 0)
                continue;
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x + 1);
            bottom = std::max(bottom, y + 1);
        }
    }
    if (left >= right || top >= bottom)
        return {glyph.X0, glyph.Y0, glyph.X1, glyph.Y1};
    const float dx = (glyph.X1 - glyph.X0) / static_cast<float>(x1 - x0);
    const float dy = (glyph.Y1 - glyph.Y0) / static_cast<float>(y1 - y0);
    return {glyph.X0 + (left - x0) * dx, glyph.Y0 + (top - y0) * dy, glyph.X0 + (right - x0) * dx,
            glyph.Y0 + (bottom - y0) * dy};
}

//======================================================================================================================
float tabLabelWidth(const char* label, ImGuiTabItemFlags flags) {
    float width = 0.0f;
    for (const auto role : {TypeRole::Body, TypeRole::BodyStrong}) {
        const ScopedType type(role);
        width = std::max(
            width,
            ImGui::TabItemCalcSize(label, (flags & ImGuiTabItemFlags_UnsavedDocument) != 0).x);
    }
    return width;
}

} // namespace

//======================================================================================================================
void setEditorFonts(const EditorFonts& fonts) {
    editorFonts = fonts;
    iconFontAvailable = fonts.icons;
}

//======================================================================================================================
ScopedType::ScopedType(TypeRole role) {
    const auto spec = typeSpec(role);
    auto* face = spec.face == TypeFace::Mono         ? editorFonts.mono
                 : spec.face == TypeFace::SansMedium ? editorFonts.sansMedium
                                                     : editorFonts.sans;
    ImGui::PushFont(face ? face : ImGui::GetIO().FontDefault, spec.size);
}

//======================================================================================================================
ScopedType::~ScopedType() {
    ImGui::PopFont();
}

//======================================================================================================================
bool beginTabItem(const char* label, ImGuiTabItemFlags flags) {
    auto* bar = ImGui::GetCurrentContext()->CurrentTabBar;
    // ImGui lays out every retained tab in the first submitted tab's font. Refresh widths before
    // that layout, including scale changes, so either face fits without changing selection.
    if (bar && bar->WantLayout) {
        for (auto& tab : bar->Tabs)
            tab.RequestedWidth = tabLabelWidth(ImGui::TabBarGetTabName(bar, &tab), tab.Flags);
    }
    ImGui::SetNextItemWidth(tabLabelWidth(label, flags));
    const auto id = bar ? ImGui::GetID(label) : 0;
    // Pending selection is committed by the first tab's layout. Requests queued during tab
    // submission take effect next frame, so later labels use the selection already laid out.
    const auto selectedId = bar && bar->WantLayout && bar->NextSelectedTabId != 0
                                ? bar->NextSelectedTabId
                            : bar ? bar->SelectedTabId
                                  : 0;
    const bool selected =
        bar && (selectedId == id ||
                (selectedId == 0 && (bar->Tabs.empty() || bar->Tabs.front().ID == id)));
    const ScopedType type(selected ? TypeRole::BodyStrong : TypeRole::Body);
    return ImGui::BeginTabItem(label, nullptr, flags);
}

//======================================================================================================================
void setActivePalette(const ThemePalette& active) {
    palette = active;
}

//======================================================================================================================
const ThemePalette& activePalette() {
    return palette;
}

//======================================================================================================================
ImVec4 color(ThemeRole role) {
    const auto c = palette[static_cast<std::size_t>(role)];
    return {c.r, c.g, c.b, c.a};
}

//======================================================================================================================
ImU32 colorU32(ThemeRole role, float alphaScale) {
    auto c = color(role);
    c.w *= alphaScale;
    return ImGui::ColorConvertFloat4ToU32(c);
}

//======================================================================================================================
float iconButtonWidth(EditorIcon icon) {
    const ScopedType type(TypeRole::Body);
    const auto info = editorIconInfo(icon);
    const bool hasGlyph = iconFontAvailable && ImGui::GetFontBaked()->FindGlyphNoFallback(
                                                   static_cast<ImWchar>(info.codepoint));
    return hasGlyph
               ? ImGui::GetFrameHeight()
               : std::max(ImGui::GetFrameHeight(), ImGui::CalcTextSize(info.label.data()).x +
                                                       2.0f * ImGui::GetStyle().FramePadding.x);
}

//======================================================================================================================
bool iconButton(const char* id, EditorIcon icon, bool enabled, const char* tooltip) {
    const ScopedType type(TypeRole::Body);
    const auto info = editorIconInfo(icon);
    const float height = ImGui::GetFrameHeight();
    const float width = iconButtonWidth(icon);
    const bool hasGlyph = iconFontAvailable && ImGui::GetFontBaked()->FindGlyphNoFallback(
                                                   static_cast<ImWchar>(info.codepoint));
    ImGui::PushID(id);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::Button(hasGlyph ? "##glyph" : info.label.data(), {width, height});
    if (hasGlyph && ImGui::IsItemVisible()) {
        auto* baked = ImGui::GetFontBaked();
        const auto glyph = *baked->FindGlyph(static_cast<ImWchar>(info.codepoint));
        const auto* atlas = ImGui::GetFont()->OwnerAtlas;
        const auto bounds = glyphInkBounds(glyph, *atlas->TexData);
        const float scale = ImGui::GetFontSize() / baked->Size;
        const auto minimum = ImGui::GetItemRectMin();
        const auto maximum = ImGui::GetItemRectMax();
        const auto density = ImGui::GetWindowViewport()->FramebufferScale;
        // Atlas quads can contain asymmetric transparent padding. Center their actual ink and
        // snap to framebuffer pixels; text rendering would truncate to whole logical points.
        const ImVec2 origin{
            std::round((minimum.x + maximum.x - (bounds.x + bounds.z) * scale) * 0.5f * density.x) /
                density.x,
            std::round((minimum.y + maximum.y - (bounds.y + bounds.w) * scale) * 0.5f * density.y) /
                density.y};
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(minimum, maximum, true);
        draw->AddImage(atlas->TexRef, {origin.x + glyph.X0 * scale, origin.y + glyph.Y0 * scale},
                       {origin.x + glyph.X1 * scale, origin.y + glyph.Y1 * scale},
                       {glyph.U0, glyph.V0}, {glyph.U1, glyph.V1},
                       ImGui::GetColorU32(ImGuiCol_Text));
        draw->PopClipRect();
    }
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
    // A plain tree node, not a framed header, so it never reads as another Rendering topic.
    const bool open = ImGui::TreeNodeEx("Diagnostics", ImGuiTreeNodeFlags_SpanAvailWidth);
    if (open)
        ImGui::PushFont(editorFonts.mono, typeSpec(TypeRole::MonoBody).size);
    return open;
}

//======================================================================================================================
void endDiagnostics() {
    ImGui::PopFont();
    ImGui::TreePop();
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
