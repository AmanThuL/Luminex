//----------------------------------------------------------------------------------------------------------------------
/// @file StyleGalleryPanel.cpp
/// @brief Renders isolated theme and component specimens in a transient native window.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Panels/Gallery/StyleGalleryPanel.h"

#include "App/Model/Workspace/GalleryCatalog.h"
#include "App/Panels/Shared/EditorStyle.h"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <string_view>

#pragma clang diagnostic error "-Wswitch"

namespace lmx::app {
namespace {
using namespace editor_style;

class ScopedGalleryPalette {
public:
    //==================================================================================================================
    explicit ScopedGalleryPalette(const ThemePalette& selected);
    ~ScopedGalleryPalette();
    //==================================================================================================================
    ScopedGalleryPalette(const ScopedGalleryPalette&) = delete;
    //==================================================================================================================
    ScopedGalleryPalette& operator=(const ScopedGalleryPalette&) = delete;

private:
    ThemePalette m_parent;
};

//======================================================================================================================
ScopedGalleryPalette::ScopedGalleryPalette(const ThemePalette& selected)
    : m_parent(activePalette()) {
    for (std::size_t i = 0; i < kImGuiSlots.size(); ++i) {
        const auto& slot = kImGuiSlots[i];
        const auto c = selected[static_cast<std::size_t>(slot.role)];
        ImGui::PushStyleColor(static_cast<ImGuiCol>(i),
                              ImVec4{c.r, c.g, c.b, c.a * slot.alphaScale});
    }
    setActivePalette(selected);
}

//======================================================================================================================
ScopedGalleryPalette::~ScopedGalleryPalette() {
    setActivePalette(m_parent);
    ImGui::PopStyleColor(static_cast<int>(kImGuiSlots.size()));
}

//======================================================================================================================
void stateLabel(const char* state) {
    const ScopedType type(TypeRole::Caption);
    ImGui::TextColored(color(ThemeRole::TextSecondary), "%s", state);
}

//======================================================================================================================
void mark(const ImVec2& center, std::string_view kind) {
    auto* draw = ImGui::GetWindowDrawList();
    const float radius = scaled(kActorMarkSize) * 0.5f;
    if (kind == "Proposed" || kind == "Actor") {
        const std::array points{
            ImVec2{center.x, center.y - radius}, ImVec2{center.x + radius, center.y},
            ImVec2{center.x, center.y + radius}, ImVec2{center.x - radius, center.y}};
        if (kind == "Proposed")
            draw->AddPolyline(points.data(), static_cast<int>(points.size()),
                              colorU32(ThemeRole::ActorAgent), ImDrawFlags_Closed,
                              scaled(kShape.border));
        else
            draw->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()),
                                      colorU32(ThemeRole::ActorAgent));
    } else if (kind == "SystemApplied") {
        draw->AddCircle(center, radius, colorU32(ThemeRole::ActorSystem), 0, scaled(kShape.border));
    } else {
        draw->AddCircleFilled(center, radius, colorU32(ThemeRole::ActorOperator));
    }
}

//======================================================================================================================
void dashedLine(ImVec2 start, float width, ThemeRole role) {
    auto* draw = ImGui::GetWindowDrawList();
    for (float x = 0; x < width; x += scaled(5.0f))
        draw->AddLine({start.x + x, start.y},
                      {start.x + std::min(x + scaled(3.0f), width), start.y}, colorU32(role),
                      scaled(kShape.border));
}

//======================================================================================================================
void fixture(const char* text, ThemeRole fill, ThemeRole ink, float radius = kShape.control,
             ThemeRole border = ThemeRole::BorderSubtle, float height = 0.0f,
             std::string_view provenance = {}) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float padding = scaled(8.0f);
    const float width = radius == kShape.pill
                            ? std::min(ImGui::GetContentRegionAvail().x,
                                       ImGui::CalcTextSize(text).x + padding * 2 + scaled(16.0f))
                            : ImGui::GetContentRegionAvail().x;
    const float paddingY = radius == kShape.pill ? scaled(3.5f) : ImGui::GetStyle().FramePadding.y;
    const float markerWidth = provenance.empty() ? 0.0f : scaled(16.0f);
    const float textWidth = std::max(scaled(1.0f), width - padding * 2 - markerWidth);
    const ImVec2 textSize = ImGui::CalcTextSize(text, nullptr, false, textWidth);
    height = std::max(scaled(height), textSize.y + paddingY * 2);
    const ImVec2 end{start.x + width, start.y + height};
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(start, end, colorU32(fill), scaled(radius));
    if (provenance == "AgentFocus")
        attentionRing(start, end);
    else
        draw->AddRect(start, end, colorU32(border), scaled(radius), 0, scaled(kShape.border));
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), {start.x + padding, start.y + paddingY},
                  colorU32(ink), text, nullptr, textWidth);
    if (!provenance.empty()) {
        const ImVec2 center{end.x - padding, start.y + height * 0.5f};
        if (provenance == "Session")
            dashedLine({start.x + padding, end.y - scaled(2.0f)}, textSize.x,
                       ThemeRole::ProvSession);
        else if (provenance != "AgentFocus" && provenance != "SystemApplied")
            mark(center, provenance);
    }
    ImGui::Dummy({width, height});
    if (provenance == "SystemApplied") {
        provenanceMark(
            {Provenance::SystemApplied, Actor::System, "Dynamic resolution · Gallery fixture"},
            true);
        editorTooltip("Dynamic resolution · Gallery fixture");
    }
}

//======================================================================================================================
void controlState(std::string_view state, bool primary = false) {
    const auto fill =
        primary ? (state == "Hover"    ? ThemeRole::AccentOperatorHover
                   : state == "Active" ? ThemeRole::AccentOperatorActive
                                       : ThemeRole::AccentOperator)
                : (state == "Hover"                          ? ThemeRole::SurfaceActive
                   : state == "Active" || state == "Toggled" ? ThemeRole::AccentOperatorSubtle
                                                             : ThemeRole::SurfaceHover);
    ImGui::PushStyleColor(ImGuiCol_Button, color(fill));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, color(fill));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          color(primary ? ThemeRole::TextOnAccent : ThemeRole::TextPrimary));
    ImGui::BeginDisabled(state == "Disabled");
}

//======================================================================================================================
void endControlState() {
    ImGui::EndDisabled();
    ImGui::PopStyleColor(3);
}

//======================================================================================================================
void graphCard(std::string_view kind) {
    const ScopedType type(TypeRole::MonoCaption);
    const auto start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float padding = scaled(10.0f);
    const char* title = kind == "Raster"    ? "lmx.pass.scene · 2.41 ms"
                        : kind == "Compute" ? "lmx.pass.light.cluster.fill · 0.08 ms"
                                            : "lmx.pass.exposure.seed · culled";
    const float bandHeight =
        ImGui::CalcTextSize(title, nullptr, false, width - padding * 2).y + padding;
    const float bodyHeight =
        kind == "Culled" ? 0 : ImGui::GetTextLineHeightWithSpacing() * 3 + padding;
    const ImVec2 end{start.x + width, start.y + bandHeight + bodyHeight};
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(start, end, colorU32(ThemeRole::SurfaceRaised), scaled(kShape.card));
    draw->AddRectFilled(start, {end.x, start.y + bandHeight},
                        colorU32(kind == "Raster"    ? ThemeRole::GraphRaster
                                 : kind == "Compute" ? ThemeRole::GraphCompute
                                                     : ThemeRole::GraphCulled),
                        scaled(kShape.card),
                        kind == "Culled" ? ImDrawFlags_RoundCornersAll
                                         : ImDrawFlags_RoundCornersTop);
    draw->AddRect(start, end, colorU32(ThemeRole::BorderSubtle), scaled(kShape.card), 0,
                  scaled(kShape.border));
    draw->PushClipRect(start, end, true);
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                  {start.x + padding, start.y + padding * 0.5f},
                  colorU32(kind == "Culled" ? ThemeRole::TextSecondary : ThemeRole::TextPrimary),
                  title, nullptr, width - padding * 2);
    if (kind != "Culled") {
        const std::array roles{ThemeRole::GraphLink0, ThemeRole::GraphLink3, ThemeRole::GraphLink4};
        const std::array labels{"scene.color", "scene.depth", "scene.motion"};
        for (std::size_t i = 0; i < labels.size(); ++i) {
            const float y =
                start.y + bandHeight + padding * 0.5f + i * ImGui::GetTextLineHeightWithSpacing();
            draw->AddCircleFilled({start.x + padding, y + ImGui::GetFontSize() * 0.5f},
                                  scaled(4.0f), colorU32(roles[i]));
            draw->AddText({start.x + padding * 2, y}, colorU32(ThemeRole::TextSecondary),
                          labels[i]);
        }
    } else {
        dashedLine({start.x, end.y - scaled(1.0f)}, width, ThemeRole::BorderStrong);
    }
    draw->PopClipRect();
    ImGui::Dummy({width, end.y - start.y});
}

//======================================================================================================================
void activitySpecimens() {
    for (const char* state :
         {"idle", "working", "awaiting", "proposed", "applied", "error", "stale"}) {
        ImGui::PushID(state);
        stateLabel(state);
        const std::string_view lifecycle(state);
        Activity specimen{Actor::Agent, state,
                          lifecycle == "working" ? std::optional{0.375f} : std::nullopt, false,
                          "Reserved lifecycle · Gallery fixture"};
        activityStrip(specimen, activityStripWidth(specimen) <= ImGui::GetContentRegionAvail().x);
        if (lifecycle == "proposed" || lifecycle == "applied") {
            provenanceMark(
                {lifecycle == "proposed" ? Provenance::Proposed : Provenance::AgentApplied,
                 Actor::Agent, "Reserved provenance · Gallery fixture"});
        }
        if (lifecycle == "error" || lifecycle == "stale")
            ImGui::TextColored(
                color(lifecycle == "error" ? ThemeRole::StatusError : ThemeRole::TextDisabled),
                "%s",
                lifecycle == "error" ? "Evidence unavailable (fixture)"
                                     : "Evidence out of date (fixture)");
        ImGui::PopID();
    }
    for (const Activity& specimen :
         {Activity{Actor::Operator, "Measuring", 0.375f, true, "Measurement · Gallery fixture"},
          Activity{Actor::System, "Loading", std::nullopt, false, "Scene load · Gallery fixture"},
          Activity{Actor::System, "Capture pending", std::nullopt, false,
                   "Waiting for a drawable · Gallery fixture"}}) {
        ImGui::PushID(specimen.verb.c_str());
        stateLabel(specimen.verb == "Capture pending"  ? "Capture pending"
                   : specimen.actor == Actor::Operator ? "Operator"
                                                       : "System");
        activityStrip(specimen, activityStripWidth(specimen) <= ImGui::GetContentRegionAvail().x);
        ImGui::PopID();
    }
}

//======================================================================================================================
void proposalSpecimen() {
    for (bool applied : {false, true}) {
        stateLabel(applied ? "Applied" : "Pending");
        SessionProposal specimen;
        specimen.id = applied ? 2 : 1;
        specimen.actor = Actor::Agent;
        specimen.client = "Gallery fixture";
        specimen.summary = "Adjust local-light intensity";
        specimen.state = applied ? SessionState::Applied : SessionState::Proposed;
        specimen.changes = {
            {asset::DocumentChangeOwner::Light, 0, {}, "Intensity", "12.000", "9.500"},
            {asset::DocumentChangeOwner::Light, 0, {}, "Range", "6.000", "8.000"}};
        specimen.evidence = {"comparison report (fixture)"};
        CardLabels labels;
        labels.showDetails = true;
        labels.appliedMessage = "Applied · Revert restores the saved values (fixture).";
        labels.appliedAction = "Revert";
        labels.footer = "Gallery fixture; these controls do not change the scene.";
        proposalCard(specimen, labels);
    }
}

//======================================================================================================================
void drawComponent(GalleryComponent component) {
    const ScopedType body(TypeRole::Body);
    switch (component) {
    case GalleryComponent::ActivityStrip:
        activitySpecimens();
        break;
    case GalleryComponent::AttentionRing:
        stateLabel("Reserved focus");
        fixture("Local Light 9 · attention", ThemeRole::SurfacePanel, ThemeRole::TextPrimary,
                kShape.control, ThemeRole::AccentAgent, 22.0f, "AgentFocus");
        editorTooltip("Software attention · Gallery fixture; operator selection is unchanged.");
        fixture("Intensity 12.000", ThemeRole::SurfaceSunken, ThemeRole::TextPrimary,
                kShape.control, ThemeRole::AccentAgent, 22.0f, "AgentFocus");
        editorTooltip("Software attention · Gallery fixture");
        break;
    case GalleryComponent::ProposalCard:
        proposalSpecimen();
        break;
    case GalleryComponent::Button:
        for (bool primary : {false, true}) {
            stateLabel(primary ? "Primary" : "Neutral");
            for (const char* state : {"Default", "Hover", "Active", "Disabled"}) {
                ImGui::PushID(primary ? 1 : 0);
                ImGui::PushID(state);
                stateLabel(state);
                const ScopedType face(primary ? TypeRole::BodyStrong : TypeRole::Body);
                controlState(state, primary);
                const bool focusBorder = !primary && std::string_view(state) == "Active";
                if (focusBorder)
                    ImGui::PushStyleColor(ImGuiCol_Border, color(ThemeRole::BorderFocus));
                ImGui::Button(primary ? "Save scene" : "Reset camera");
                if (focusBorder)
                    ImGui::PopStyleColor();
                endControlState();
                ImGui::PopID();
                ImGui::PopID();
            }
        }
        break;
    case GalleryComponent::IconButton:
        for (const char* state : {"Default", "Hover", "Active", "Toggled", "Disabled"}) {
            stateLabel(state);
            controlState(state);
            if (std::string_view(state) == "Toggled") {
                ImGui::PushStyleColor(ImGuiCol_Border, color(ThemeRole::AccentOperator));
                ImGui::PushStyleColor(ImGuiCol_Text, color(ThemeRole::AccentOperatorText));
            }
            iconButton(state, EditorIcon::Play, true, "Play; labeled fallback without Codicons.");
            if (std::string_view(state) == "Toggled")
                ImGui::PopStyleColor(2);
            endControlState();
        }
        break;
    case GalleryComponent::Checkbox:
        for (const char* state : {"Off", "On", "Hover", "Disabled"}) {
            stateLabel(state);
            ImGui::PushID(state);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, color(std::string_view(state) == "Hover"
                                                              ? ThemeRole::SurfaceHover
                                                              : ThemeRole::SurfaceSunken));
            ImGui::BeginDisabled(std::string_view(state) == "Disabled");
            bool enabled = std::string_view(state) == "On";
            ImGui::Checkbox("Enabled", &enabled);
            ImGui::EndDisabled();
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        break;
    case GalleryComponent::Chip:
        for (const char* state : {"Severity", "Actor", "Provenance", "Count"}) {
            stateLabel(state);
            const ScopedType type(TypeRole::Caption);
            const std::string_view kind(state);
            if (kind == "Actor")
                actorChip(Actor::Agent, "Agent · working");
            else
                fixture(kind == "Severity"     ? "WARN 12"
                        : kind == "Provenance" ? "not saved"
                                               : "↓ 3 new",
                        kind == "Count" ? ThemeRole::AccentOperatorSubtle : ThemeRole::SurfaceHover,
                        kind == "Severity"     ? ThemeRole::StatusWarning
                        : kind == "Provenance" ? ThemeRole::ProvSession
                                               : ThemeRole::AccentOperatorText,
                        kShape.pill,
                        kind == "Count" ? ThemeRole::AccentOperator : ThemeRole::BorderSubtle,
                        20.0f, kind == "Provenance" ? "Session" : "");
        }
        break;
    case GalleryComponent::FieldText:
        for (const char* state : {"Default", "Focus", "Hint", "Disabled"}) {
            stateLabel(state);
            ImGui::PushID(state);
            ImGui::BeginDisabled(std::string_view(state) == "Disabled");
            ImGui::PushStyleColor(ImGuiCol_Border, color(std::string_view(state) == "Focus"
                                                             ? ThemeRole::BorderFocus
                                                             : ThemeRole::BorderSubtle));
            char text[64] = "Crytek Sponza";
            if (std::string_view(state) == "Hint")
                text[0] = '\0';
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##text", "Search subjects...", text, sizeof(text));
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        break;
    case GalleryComponent::FieldNumber:
        for (const char* state : {"Default", "Active", "Proposed", "SystemApplied", "Disabled"}) {
            stateLabel(state);
            const std::string_view kind(state);
            if (kind == "Proposed") {
                proposedValue({}, "12.000", "9.500");
            } else if (kind == "SystemApplied") {
                fixture("X 12.000 · set by dynamic resolution", ThemeRole::SurfaceSunken,
                        ThemeRole::TextPrimary, kShape.control, ThemeRole::BorderSubtle, 0, kind);
                editorTooltip("System policy: dynamic resolution.");
            } else {
                ImGui::PushID(state);
                ImGui::BeginDisabled(kind == "Disabled");
                ImGui::PushStyleColor(
                    ImGuiCol_Border,
                    color(kind == "Active" ? ThemeRole::BorderFocus : ThemeRole::BorderSubtle));
                float value = 12.0f;
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat("##number", &value, 0.01f, 0, 0, "X %.3f");
                ImGui::PopStyleColor();
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }
        break;
    case GalleryComponent::FieldSelect:
        for (const char* state : {"Closed", "Open", "Disabled"}) {
            stateLabel(state);
            ImGui::PushID(state);
            ImGui::BeginDisabled(std::string_view(state) == "Disabled");
            ImGui::PushStyleColor(ImGuiCol_Border, color(std::string_view(state) == "Open"
                                                             ? ThemeRole::BorderFocus
                                                             : ThemeRole::BorderSubtle));
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##select", "Native TAA")) {
                for (const char* label : {"Off", "Raw", "Native TAA", "MetalFX"}) {
                    ImGui::BeginDisabled(std::string_view(label) == "MetalFX");
                    ImGui::Selectable(label, std::string_view(label) == "Native TAA");
                    editorTooltip("MetalFX is unavailable in this specimen.");
                    ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
            if (std::string_view(state) == "Open") {
                for (const char* label : {"Off", "Raw", "Native TAA", "MetalFX · unsupported here"})
                    fixture(label,
                            std::string_view(label) == "Native TAA" ? ThemeRole::SelectionBg
                                                                    : ThemeRole::SurfaceRaised,
                            std::string_view(label).starts_with("MetalFX")
                                ? ThemeRole::TextDisabled
                                : ThemeRole::TextPrimary);
            }
            ImGui::PopID();
        }
        break;
    case GalleryComponent::FieldSlider:
        for (const char* state : {"Default", "Active", "Disabled"}) {
            stateLabel(state);
            ImGui::PushID(state);
            ImGui::BeginDisabled(std::string_view(state) == "Disabled");
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, color(std::string_view(state) == "Active"
                                                                 ? ThemeRole::AccentOperatorActive
                                                                 : ThemeRole::AccentOperator));
            ImGui::PushStyleColor(ImGuiCol_Border, color(std::string_view(state) == "Active"
                                                             ? ThemeRole::BorderFocus
                                                             : ThemeRole::BorderSubtle));
            float value = 0.375f;
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderFloat("##slider", &value, 0.0f, 1.0f, "%.3f");
            ImGui::PopStyleColor(2);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        break;
    case GalleryComponent::DockTab:
        for (const char* state : {"Selected", "Unselected", "DimmedSelected"}) {
            stateLabel(state);
            const std::string_view kind(state);
            const ScopedType type(kind == "Selected" ? TypeRole::BodyStrong : TypeRole::Body);
            const auto start = ImGui::GetCursorScreenPos();
            fixture("Inspector",
                    kind == "Unselected" ? ThemeRole::SurfaceCanvas : ThemeRole::SurfacePanel,
                    kind == "Unselected" ? ThemeRole::TextSecondary : ThemeRole::TextPrimary);
            ImGui::GetWindowDrawList()->AddLine(
                start, {start.x + ImGui::GetContentRegionAvail().x, start.y},
                colorU32(kind == "Selected"         ? ThemeRole::AccentOperator
                         : kind == "DimmedSelected" ? ThemeRole::BorderStrong
                                                    : ThemeRole::SurfaceCanvas),
                scaled(2.0f));
        }
        break;
    case GalleryComponent::MenuItem:
        for (const char* state : {"Default", "Hover", "Checked", "Disabled"}) {
            stateLabel(state);
            const std::string_view kind(state);
            fixture(kind == "Checked" ? "Auto (system)" : "Frame Selected    F",
                    kind == "Hover" ? ThemeRole::SurfaceHover : ThemeRole::SurfacePanel,
                    kind == "Disabled" ? ThemeRole::TextDisabled : ThemeRole::TextPrimary);
            if (kind == "Checked") {
                // ImGui::MenuItem's own mark geometry, in its trailing mark column.
                const float size = ImGui::GetFontSize();
                ImGui::RenderCheckMark(
                    ImGui::GetWindowDrawList(),
                    {ImGui::GetItemRectMax().x - scaled(8.0f) - size * 0.866f,
                     ImGui::GetItemRectMin().y + ImGui::GetStyle().FramePadding.y + size * 0.067f},
                    colorU32(ThemeRole::TextPrimary), size * 0.866f);
            }
            if (kind == "Disabled")
                editorTooltip("No subject with reliable bounds is selected.");
        }
        break;
    case GalleryComponent::HierarchyRow:
        for (const char* state : {"Authored", "Selected", "Edited", "Session", "Off", "Culled",
                                  "Proposed", "AgentFocus"}) {
            stateLabel(state);
            const std::string_view kind(state);
            fixture(kind == "Session"    ? "> Local Light 9 · not saved"
                    : kind == "Off"      ? "> Local Light 9 [off]"
                    : kind == "Culled"   ? "> Local Light 9 · culled"
                    : kind == "Proposed" ? "> Local Light 9 · proposed"
                                         : "> Local Light 9",
                    kind == "Selected" ? ThemeRole::SelectionBg : ThemeRole::SurfacePanel,
                    kind == "Off"      ? ThemeRole::TextDisabled
                    : kind == "Culled" ? ThemeRole::TextSecondary
                                       : ThemeRole::TextPrimary,
                    kShape.control,
                    kind == "AgentFocus" ? ThemeRole::AccentAgent : ThemeRole::BorderSubtle, 22.0f,
                    kind == "Edited" || kind == "Session" || kind == "Proposed" ||
                            kind == "AgentFocus"
                        ? kind
                        : "");
        }
        break;
    case GalleryComponent::PropertyRow:
        for (const char* state : {"Default", "ReadOnly", "Proposed", "SystemApplied"}) {
            stateLabel(state);
            ImGui::PushID(state);
            if (beginPropertyGrid("##property")) {
                const std::string_view kind(state);
                field(kind == "SystemApplied" ? "Render scale" : "Intensity (relative)");
                if (kind == "Proposed")
                    proposedValue({}, "12.000", "9.500");
                else if (kind == "ReadOnly")
                    message("0.812 · set by dynamic resolution");
                else if (kind == "SystemApplied")
                    fixture("0.812 · auto", ThemeRole::SurfaceSunken, ThemeRole::TextPrimary,
                            kShape.control, ThemeRole::BorderSubtle, 0, kind);
                else {
                    float value = 12.0f;
                    ImGui::DragFloat("##intensity", &value, 0.1f, 0, 0, "%.1f");
                }
                endFields();
            }
            ImGui::PopID();
        }
        break;
    case GalleryComponent::SubjectHeader:
        for (const char* state : {"Default", "Changed"}) {
            stateLabel(state);
            ImGui::PushID(state);
            bool enabled = true;
            ImGui::Checkbox("##enabled", &enabled);
            nextInRow(ImGui::CalcTextSize("Local Light 9").x);
            {
                const ScopedType strong(TypeRole::BodyStrong);
                ImGui::TextUnformatted("Local Light 9");
            }
            nextInRow(ImGui::CalcTextSize("Spot").x);
            ImGui::TextColored(color(ThemeRole::TextSecondary), "Spot");
            nextInRow(iconButtonWidth(EditorIcon::Reset));
            if (std::string_view(state) == "Changed") {
                const auto point = ImGui::GetCursorScreenPos();
                mark({point.x + scaled(4.0f), point.y + ImGui::GetFrameHeight() * 0.5f}, "Edited");
                ImGui::Dummy({scaled(12.0f), ImGui::GetFrameHeight()});
                nextInRow(iconButtonWidth(EditorIcon::Reset));
            }
            iconButton("##reset", EditorIcon::Reset, std::string_view(state) == "Changed",
                       "Reset this subject.");
            ImGui::PopID();
        }
        break;
    case GalleryComponent::TopicHeader:
        for (const char* state : {"Collapsed", "Expanded"}) {
            stateLabel(state);
            ImGui::PushID(state);
            ImGui::SetNextItemOpen(std::string_view(state) == "Expanded", ImGuiCond_Always);
            const float contentRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            const bool wide = ImGui::GetContentRegionAvail().x >= scaled(kPropertyGridMinWidth);
            {
                const ScopedType strong(TypeRole::BodyStrong);
                collapsingHeader("Reconstruction", ImGuiTreeNodeFlags_AllowOverlap);
            }
            if (wide)
                ImGui::SameLine(contentRight - iconButtonWidth(EditorIcon::Reset));
            iconButton("##reset", EditorIcon::Reset, true, "Reset this topic.");
            ImGui::PopID();
        }
        break;
    case GalleryComponent::Notice:
        for (const char* state : {"Success", "Failure"}) {
            stateLabel(state);
            const auto start = ImGui::GetCursorScreenPos();
            const bool success = std::string_view(state) == "Success";
            fixture(success ? "Scene saved\nAssets/Scenes/sponza.scene.gltf · 12:04:31 UTC"
                            : "Save failed\nDocument validation failed: node 14 name mismatch.",
                    ThemeRole::SurfaceOverlay, ThemeRole::TextPrimary, kShape.popup);
            const auto end = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(
                start, {start.x, end.y},
                colorU32(success ? ThemeRole::StatusSuccess : ThemeRole::StatusError),
                scaled(2.0f));
            ImGui::PushID(state);
            primaryButton(success ? "Copy path" : "Copy details");
            if (success) {
                nextInRow(ImGui::CalcTextSize("Reveal").x + ImGui::GetStyle().FramePadding.x * 2);
                primaryButton("Reveal");
            }
            ImGui::PopID();
        }
        break;
    case GalleryComponent::LegendChip:
        for (const char* state : {"Motion", "HZB"}) {
            stateLabel(state);
            const bool hzb = std::string_view(state) == "HZB";
            {
                const ScopedType strong(TypeRole::BodyStrong);
                fixture(hzb ? "HZB level 2    − 2 / 9 +    ×" : "Motion vectors    ×",
                        ThemeRole::SurfaceOverlay, ThemeRole::TextPrimary, kShape.card);
            }
            const auto start = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const std::array roles =
                hzb ? std::array{ThemeRole::SurfaceViewport, ThemeRole::TextSecondary,
                                 ThemeRole::TextPrimary}
                    : std::array{ThemeRole::GraphLink5, ThemeRole::SurfaceHover,
                                 ThemeRole::GraphLink4};
            for (std::size_t i = 0; i < roles.size(); ++i)
                ImGui::GetWindowDrawList()->AddRectFilled(
                    {start.x + width * i / 3.0f, start.y},
                    {start.x + width * (i + 1) / 3.0f, start.y + scaled(10.0f)},
                    colorU32(roles[i]));
            ImGui::Dummy({width, scaled(10.0f)});
            const ScopedType type(TypeRole::MonoCaption);
            ImGui::TextWrapped("%s", hzb ? "near   depth (reversed Z)   far"
                                         : "−1.0   0 texels / frame   +1.0");
        }
        break;
    case GalleryComponent::GraphCard:
        for (const char* state : {"Raster", "Compute", "Culled"}) {
            stateLabel(state);
            graphCard(state);
        }
        break;
    case GalleryComponent::ConsoleRow:
        for (const char* state : {"Trace", "Debug", "Info", "Warning", "Error", "Critical"}) {
            stateLabel(state);
            const std::string_view kind(state);
            const auto role = kind == "Trace"     ? ThemeRole::ConsoleTrace
                              : kind == "Debug"   ? ThemeRole::ConsoleDebug
                              : kind == "Info"    ? ThemeRole::ConsoleInfo
                              : kind == "Warning" ? ThemeRole::ConsoleWarn
                              : kind == "Error"   ? ThemeRole::ConsoleError
                                                  : ThemeRole::ConsoleCritical;
            {
                const ScopedType timestamp(TypeRole::MonoCaption);
                ImGui::TextColored(color(ThemeRole::TextSecondary), "2026-09-30 14:02:11.482");
            }
            {
                const ScopedType caption(TypeRole::Caption);
                fixture(state, ThemeRole::SurfaceHover, role, kShape.pill, ThemeRole::BorderSubtle,
                        20.0f);
            }
            ImGui::PushStyleColor(ImGuiCol_Text, color(role));
            ImGui::TextWrapped("%s", kind == "Info" ? "Editor font: Geist Regular, 16 pt"
                                                    : "Scene loading diagnostic specimen");
            ImGui::PopStyleColor();
        }
        break;
    }
}

//======================================================================================================================
void drawTypeRamp() {
    const ScopedType title(TypeRole::BodyStrong);
    ImGui::SeparatorText("Type ramp");
    for (auto role : {TypeRole::Caption, TypeRole::Body, TypeRole::BodyStrong, TypeRole::Display,
                      TypeRole::MonoCaption, TypeRole::MonoBody}) {
        const ScopedType type(role);
        const auto spec = typeSpec(role);
        ImGui::TextWrapped("%.0f px · %s · 0123456789 1111111111 8888888888 · 12.345 / 678.900",
                           spec.size,
                           spec.face == TypeFace::Mono         ? "Mono"
                           : spec.face == TypeFace::SansMedium ? "Medium"
                                                               : "Regular");
    }
}

} // namespace

//======================================================================================================================
void drawStyleGalleryPanel(StyleGalleryPanelState& state) {
    if (!state.open)
        return;
    const auto* main = ImGui::GetMainViewport();
    // Clearing NoDecoration gives the detached window a native title bar, so it is closable by
    // its close button and by the Window menu's Close like the other detached panels.
    ImGuiWindowClass windowClass;
    windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    windowClass.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoDecoration;
    ImGui::SetNextWindowClass(&windowClass);
    ImGui::SetNextWindowPos({main->WorkPos.x + scaled(40.0f), main->WorkPos.y + scaled(40.0f)},
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({scaled(780.0f), scaled(720.0f)}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
    // The native title bar replaces ImGui's once the previous frame owned a platform window.
    const bool visible =
        ImGui::Begin(kStyleGalleryWindowName, &state.open,
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                         (state.ownsPlatformWindow ? ImGuiWindowFlags_NoTitleBar : 0));
    state.ownsPlatformWindow = ImGui::GetWindowViewport() != main;
    if (visible) {
        ImGui::SetNextItemWidth(std::min(scaled(220.0f), ImGui::GetContentRegionAvail().x));
        ImGui::Combo("Palette", &state.palette, "Current\0Dark\0Light\0");
        const ThemePalette selected = state.palette == 1   ? kDarkPalette
                                      : state.palette == 2 ? kLightPalette
                                                           : activePalette();
        const ScopedGalleryPalette palette(selected);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, color(ThemeRole::SurfacePanel));
        if (ImGui::BeginChild("##specimens", {0, 0})) {
            drawTypeRamp();
            for (const auto& entry : galleryCatalog()) {
                ImGui::PushID(static_cast<int>(entry.component));
                {
                    const ScopedType title(TypeRole::BodyStrong);
                    ImGui::SeparatorText(entry.figmaName.data());
                }
                drawComponent(entry.component);
                ImGui::Spacing();
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::End();
}

} // namespace lmx::app
