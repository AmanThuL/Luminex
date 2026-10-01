//----------------------------------------------------------------------------------------------------------------------
/// @file EditorStyle.cpp
/// @brief Draws shared editor actions, property grids and transient notices.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Shared/EditorStyle.h"

#include "App/Panels/Shared/ActionFeedback.h"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>

namespace lmx::app::editor_style {
namespace {

bool iconFontAvailable = false;
EditorFonts editorFonts;
ThemePalette palette = kDarkPalette;
std::optional<ProvenanceMark> nextFieldMark;

//======================================================================================================================
ThemeRole actorRole(Actor actor) {
    switch (actor) {
    case Actor::Operator:
        return ThemeRole::ActorOperator;
    case Actor::System:
        return ThemeRole::ActorSystem;
    case Actor::Agent:
        return ThemeRole::ActorAgent;
    }
    return ThemeRole::ActorSystem;
}

//======================================================================================================================
void drawActor(ImVec2 center, Actor actor, float size, bool ghost = false) {
    auto* draw = ImGui::GetWindowDrawList();
    const float radius = scaled(size) * 0.5f;
    const auto ink = colorU32(actorRole(actor));
    if (actor == Actor::Agent) {
        const std::array points{
            ImVec2{center.x, center.y - radius}, ImVec2{center.x + radius, center.y},
            ImVec2{center.x, center.y + radius}, ImVec2{center.x - radius, center.y}};
        if (ghost)
            draw->AddPolyline(points.data(), points.size(), ink, ImDrawFlags_Closed,
                              scaled(kShape.border));
        else
            draw->AddConvexPolyFilled(points.data(), points.size(), ink);
    } else if (actor == Actor::System) {
        draw->AddCircle(center, radius, ink, 0, scaled(kShape.border));
    } else {
        draw->AddCircleFilled(center, radius, ink);
    }
}

//======================================================================================================================
void drawGear(ImVec2 center) {
    std::array<ImVec2, 32> teeth;
    for (size_t i = 0; i < teeth.size(); ++i) {
        const float angle = static_cast<float>(i) * 2.0f * std::numbers::pi_v<float> /
                            static_cast<float>(teeth.size());
        const float radius = scaled(kActorMarkSize) * (i % 4 < 2 ? 0.5f : 0.36f);
        teeth[i] = {center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
    }
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddPolyline(teeth.data(), teeth.size(), colorU32(ThemeRole::ActorSystem),
                      ImDrawFlags_Closed, scaled(kShape.border));
    draw->AddCircle(center, scaled(1.5f), colorU32(ThemeRole::ActorSystem), 0,
                    scaled(kShape.border));
}

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
void actorMark(Actor actor, float size) {
    const auto start = ImGui::GetCursorScreenPos();
    ImGui::Dummy({scaled(size), ImGui::GetTextLineHeight()});
    drawActor({start.x + scaled(size) * 0.5f, start.y + ImGui::GetTextLineHeight() * 0.5f}, actor,
              size);
}

//======================================================================================================================
void actorChip(Actor actor, std::string_view label) {
    const ScopedType type(TypeRole::Caption);
    const std::string text(label);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float padding = scaled(8.0f);
    const float width = std::min(ImGui::GetContentRegionAvail().x,
                                 ImGui::CalcTextSize(text.c_str()).x + padding * 2 + scaled(16.0f));
    const float paddingY = scaled(3.5f);
    const float textWidth = std::max(scaled(1.0f), width - padding * 2 - scaled(16.0f));
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str(), nullptr, false, textWidth);
    const float height = std::max(scaled(20.0f), textSize.y + paddingY * 2);
    const ImVec2 end{start.x + width, start.y + height};
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(start, end, colorU32(ThemeRole::SurfaceHover), scaled(kShape.pill));
    draw->AddRect(start, end, colorU32(ThemeRole::BorderSubtle), scaled(kShape.pill), 0,
                  scaled(kShape.border));
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), {start.x + padding, start.y + paddingY},
                  colorU32(actor == Actor::Agent ? ThemeRole::AccentAgentText : actorRole(actor)),
                  text.c_str(), nullptr, textWidth);
    drawActor({end.x - padding, start.y + height * 0.5f}, actor, kActorMarkSize);
    ImGui::Dummy({width, height});
}

//======================================================================================================================
void attentionRing(ImVec2 min, ImVec2 max) {
    ImGui::GetWindowDrawList()->AddRect(min, max, colorU32(ThemeRole::AccentAgent),
                                        scaled(kShape.control), 0, scaled(2.0f));
}

//======================================================================================================================
void proposedValue(std::string_view field, std::string_view before, std::string_view after) {
    const ScopedType type(TypeRole::Body);
    const std::string fieldText(field);
    const std::string oldText(before);
    const std::string newText(after);
    const auto start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float padding = scaled(8.0f);
    const auto fieldSize = field.empty() ? ImVec2{} : ImGui::CalcTextSize(fieldText.c_str());
    const auto oldSize = ImGui::CalcTextSize(oldText.c_str());
    const auto newSize = ImGui::CalcTextSize(newText.c_str());
    const float valuesWidth = oldSize.x + newSize.x + padding * 4;
    const bool stacked = width < valuesWidth;
    const bool fieldInline = field.empty() || width >= fieldSize.x + padding + valuesWidth;
    const float lines = (stacked ? 2.0f : 1.0f) + (fieldInline ? 0.0f : 1.0f);
    const float height = ImGui::GetFontSize() * lines + padding * 2;
    const ImVec2 end{start.x + width, start.y + height};
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(start, end, colorU32(ThemeRole::SurfaceSunken), scaled(kShape.control));
    draw->AddRect(start, end, colorU32(ThemeRole::AccentAgent), scaled(kShape.control), 0,
                  scaled(kShape.border));
    draw->PushClipRect(start, end, true);
    const ImVec2 fieldPosition{start.x + padding, start.y + padding};
    if (!field.empty())
        draw->AddText(fieldPosition, colorU32(ThemeRole::TextSecondary), fieldText.c_str());
    const ImVec2 oldPosition{!field.empty() && fieldInline ? fieldPosition.x + fieldSize.x + padding
                                                           : fieldPosition.x,
                             fieldInline ? fieldPosition.y : fieldPosition.y + fieldSize.y};
    draw->AddText(oldPosition, colorU32(ThemeRole::TextDisabled), oldText.c_str());
    draw->AddLine({oldPosition.x, oldPosition.y + oldSize.y * 0.5f},
                  {oldPosition.x + oldSize.x, oldPosition.y + oldSize.y * 0.5f},
                  colorU32(ThemeRole::TextDisabled), scaled(kShape.border));
    const ImVec2 newPosition{stacked ? oldPosition.x : oldPosition.x + oldSize.x + padding,
                             stacked ? oldPosition.y + oldSize.y : oldPosition.y};
    draw->AddText(newPosition, colorU32(ThemeRole::AccentAgentText), newText.c_str());
    drawActor({end.x - padding, end.y - padding - newSize.y * 0.5f}, Actor::Agent, kActorMarkSize,
              true);
    draw->PopClipRect();
    ImGui::Dummy({width, height});
    editorTooltip("Proposed value; the current value is unchanged.");
}

//======================================================================================================================
CardAction proposalCard(const SessionProposal& proposal, const CardLabels& labels) {
    CardAction action = CardAction::None;
    const bool applied = proposal.state == SessionState::Applied;
    ImGui::PushID(static_cast<int>(proposal.id));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, scaled(kShape.card));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, color(ThemeRole::SurfaceRaised));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          color(applied ? ThemeRole::BorderSubtle : ThemeRole::AccentAgent));
    if (ImGui::BeginChild("##proposal", {0, 0},
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        {
            const ScopedType strong(TypeRole::BodyStrong);
            ImGui::TextWrapped("%s", proposal.summary.c_str());
        }
        actorMark(proposal.actor);
        const std::string source = proposal.client + " · proposal source";
        editorTooltip(source.c_str());
        ImGui::SameLine();
        ImGui::TextUnformatted(proposal.actor == Actor::Agent ? "Agent" : "System");
        ImGui::SameLine();
        ImGui::TextColored(color(ThemeRole::TextSecondary), "%zu changes · %s",
                           proposal.changes.size(), sessionStateLabel(proposal.state).data());
        if (proposal.state == SessionState::Error && !proposal.error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, color(ThemeRole::StatusError));
            ImGui::TextWrapped("%s", proposal.error.c_str());
            ImGui::PopStyleColor();
        }
        if (labels.showDetails) {
            for (const auto& change : proposal.changes)
                proposedValue(change.property, change.before, change.after);
            for (const auto& evidence : proposal.evidence) {
                const std::string text = "Evidence: " + evidence;
                ImGui::TextLink(text.c_str());
            }
        }
        if (applied) {
            if (labels.appliedMessage)
                message(labels.appliedMessage);
            if (labels.appliedAction && ImGui::Button(labels.appliedAction))
                action = CardAction::Accept;
        } else if (proposal.state == SessionState::Proposed ||
                   proposal.state == SessionState::Awaiting) {
            if (labels.show && ImGui::Button("Show"))
                action = CardAction::Show;
            if (labels.show)
                nextInRow(ImGui::CalcTextSize(labels.accept).x +
                          ImGui::GetStyle().FramePadding.x * 2);
            ImGui::BeginDisabled(!labels.acceptDisabledReason.empty());
            if (primaryButton(labels.accept))
                action = CardAction::Accept;
            if (!labels.acceptDisabledReason.empty())
                editorTooltip(labels.acceptDisabledReason.c_str());
            ImGui::EndDisabled();
            nextInRow(ImGui::CalcTextSize(labels.reject).x + ImGui::GetStyle().FramePadding.x * 2);
            if (ImGui::Button(labels.reject))
                action = CardAction::Reject;
        } else if (proposal.state == SessionState::Error && ImGui::Button(labels.reject)) {
            action = CardAction::Reject;
        }
        if (labels.footer)
            message(labels.footer);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PopID();
    return action;
}

//======================================================================================================================
void provenanceMark(const ProvenanceMark& mark, bool overlay) {
    if (mark.kind == Provenance::Authored)
        return;
    if (mark.kind == Provenance::SessionOnly) {
        const auto start = ImGui::GetItemRectMin();
        const auto end = ImGui::GetItemRectMax();
        const float width = std::min(end.x - start.x, ImGui::GetContentRegionAvail().x);
        for (float x = 0; x < width; x += scaled(5.0f))
            ImGui::GetWindowDrawList()->AddLine(
                {start.x + x, end.y}, {start.x + std::min(x + scaled(3.0f), width), end.y},
                colorU32(ThemeRole::ProvSession), scaled(kShape.border));
    } else {
        ImVec2 center;
        if (overlay) {
            const auto minimum = ImGui::GetItemRectMin();
            const auto maximum = ImGui::GetItemRectMax();
            const float right = std::min(maximum.x, ImGui::GetWindowDrawList()->GetClipRectMax().x);
            center = {right - scaled(kActorMarkSize) * 0.5f - ImGui::GetStyle().FramePadding.x,
                      (minimum.y + maximum.y) * 0.5f};
        } else {
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            const auto start = ImGui::GetCursorScreenPos();
            ImGui::Dummy({scaled(kActorMarkSize), ImGui::GetTextLineHeight()});
            center = {start.x + scaled(kActorMarkSize) * 0.5f,
                      start.y + ImGui::GetTextLineHeight() * 0.5f};
        }
        switch (mark.kind) {
        case Provenance::Edited:
            drawActor(center, Actor::Operator, kActorMarkSize);
            break;
        case Provenance::SystemApplied:
            drawGear(center);
            break;
        case Provenance::Proposed:
            drawActor(center, Actor::Agent, kActorMarkSize, true);
            break;
        case Provenance::AgentApplied:
            ImGui::GetWindowDrawList()->AddCircleFilled(center, scaled(kActorMarkSize) * 0.5f,
                                                        colorU32(ThemeRole::ActorAgent));
            break;
        case Provenance::Authored:
        case Provenance::SessionOnly:
            break;
        }
    }
    if (!overlay)
        editorTooltip(mark.source.c_str());
}

//======================================================================================================================
float activityStripWidth(const Activity& activity, bool showVerb) {
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    return scaled(kActorMarkSize) +
           (showVerb ? spacing + ImGui::CalcTextSize(activity.verb.c_str()).x : 0) +
           (activity.stoppable
                ? spacing + ImGui::CalcTextSize("Stop").x + ImGui::GetStyle().FramePadding.x * 2.0f
                : 0);
}

//======================================================================================================================
bool activityStrip(const Activity& activity, bool showVerb) {
    const float width = activityStripWidth(activity, showVerb);
    const float height = ImGui::GetFrameHeight();
    const auto start = ImGui::GetCursorScreenPos();
    const float markWidth = scaled(kActorMarkSize);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::BeginGroup();
    ImGui::Dummy({markWidth, height});
    auto* draw = ImGui::GetWindowDrawList();
    drawActor({start.x + markWidth * 0.5f, start.y + height * 0.5f}, activity.actor,
              kActorMarkSize);
    if (showVerb) {
        ImGui::SameLine(0.0f, spacing);
        ImGui::Dummy({ImGui::CalcTextSize(activity.verb.c_str()).x, height});
        draw->AddText(
            {start.x + markWidth + spacing, start.y + (height - ImGui::GetTextLineHeight()) * 0.5f},
            colorU32(ThemeRole::TextPrimary), activity.verb.c_str());
    }
    bool stop = false;
    if (activity.stoppable) {
        ImGui::SameLine(0.0f, spacing);
        stop = ImGui::SmallButton("Stop##Activity");
    }
    const float stopWidth = ImGui::CalcTextSize("Stop").x + ImGui::GetStyle().FramePadding.x * 2;
    const float barWidth = activity.stoppable ? width - stopWidth - spacing : width;
    const ImVec2 barStart{start.x, start.y + height - scaled(2.0f)};
    const ImVec2 barEnd{start.x + barWidth, start.y + height};
    draw->AddRectFilled(barStart, barEnd, colorU32(ThemeRole::SurfaceSunken));
    const float progress = activity.progress.value_or(0.0f);
    if (activity.progress)
        draw->AddRectFilled(barStart, {barStart.x + barWidth * progress, barEnd.y},
                            colorU32(actorRole(activity.actor)));
    else
        draw->AddLine(barStart, {barEnd.x, barStart.y}, colorU32(actorRole(activity.actor)),
                      scaled(kShape.border));
    ImGui::EndGroup();
    editorTooltip(activity.tooltip.c_str());
    return stop;
}

//======================================================================================================================
void setNextFieldProvenance(std::optional<ProvenanceMark> mark) {
    nextFieldMark = std::move(mark);
}

//======================================================================================================================
void consumeFieldProvenance() {
    if (nextFieldMark)
        provenanceMark(*nextFieldMark);
    nextFieldMark.reset();
}

//======================================================================================================================
float fieldProvenanceWidth() {
    return nextFieldMark && nextFieldMark->kind != Provenance::Authored &&
                   nextFieldMark->kind != Provenance::SessionOnly
               ? scaled(kActorMarkSize) + ImGui::GetStyle().ItemInnerSpacing.x
               : 0.0f;
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
bool primaryButton(const char* label) {
    const ScopedType type(TypeRole::BodyStrong);
    ImGui::PushStyleColor(ImGuiCol_Button, color(ThemeRole::AccentOperator));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color(ThemeRole::AccentOperatorHover));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color(ThemeRole::AccentOperatorActive));
    ImGui::PushStyleColor(ImGuiCol_Text, color(ThemeRole::TextOnAccent));
    const bool clicked = ImGui::Button(label);
    ImGui::PopStyleColor(4);
    return clicked;
}

//======================================================================================================================
bool collapsingHeader(const char* label, ImGuiTreeNodeFlags flags) {
    ImGui::PushStyleColor(ImGuiCol_Header, color(ThemeRole::SurfaceHover));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, color(ThemeRole::SurfaceHover));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, color(ThemeRole::SurfaceActive));
    const bool open = ImGui::CollapsingHeader(label, flags);
    ImGui::PopStyleColor(3);
    return open;
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
bool vector3(const char* label, const char* id, float* values, float speed, float minimum,
             float maximum, const char* format, ImGuiSliderFlags flags, bool rgb) {
    flags |= ImGuiSliderFlags_ColorMarkers;
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
            // The N-component API supplies markers automatically; separate responsive scalar
            // rows need the pinned ImGui RGB component colors before submitting the field.
            const ImVec4 marker{(axis == 0 ? 240.0f : 20.0f) / 255.0f,
                                (axis == 1 ? 240.0f : 20.0f) / 255.0f,
                                (axis == 2 ? 240.0f : 20.0f) / 255.0f, 1.0f};
            ImGui::SetNextItemColorMarker(ImGui::ColorConvertFloat4ToU32(marker));
            changed |= ImGui::DragFloat("##component", values + axis, speed, minimum, maximum,
                                        format, flags);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    return changed;
}

//======================================================================================================================
bool colorRgb(const char* label, const char* id, float* values, bool alpha) {
    // ImGui's own color inputs drop their R:/G:/B: prefixes whenever component markers are
    // drawn, so the components use the labeled vector row and the picker keeps only its swatch.
    bool changed = vector3(label, id, values, 1.0f / 255.0f, 0.0f, 1.0f, "%.3f", 0, true);
    ImGui::PushID(id);
    const ImGuiColorEditFlags flags =
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_Float;
    changed |= alpha ? ImGui::ColorEdit4("##swatch", values, flags | ImGuiColorEditFlags_AlphaBar)
                     : ImGui::ColorEdit3("##swatch", values, flags);
    ImGui::PopID();
    return changed;
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, scaled(kShape.popup));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, color(ThemeRole::SurfaceOverlay));
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
        actorMark(result->actor);
        const std::string source = "Editor status report · " + result->message +
                                   (result->path.empty() ? "" : " · " + result->path);
        editorTooltip(source.c_str());
        ImGui::SameLine();
        drawActionFeedback("result", *result);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

} // namespace lmx::app::editor_style
