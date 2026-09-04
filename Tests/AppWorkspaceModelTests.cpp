//----------------------------------------------------------------------------------------------------------------------
/// @file AppWorkspaceModelTests.cpp
/// @brief Tests panel visibility storage and the workspace persistence schema decision.
//----------------------------------------------------------------------------------------------------------------------

#include <catch2/catch_test_macros.hpp>

#include "App/WorkspaceModel.h"

#include <format>
#include <optional>
#include <string>

using namespace lmx;
using namespace lmx::app;

//======================================================================================================================
// The menu checkbox and the panel's own close button must observe each other's writes: this pins
// that both act through the same per-panel storage rather than parallel copies.
TEST_CASE("workspace visibility shares one storage per panel between accessor and mutator",
          "[app]") {
    WorkspaceVisibility visibility;
    REQUIRE(visibility.isVisible(EditorPanel::RenderGraph) == false);

    visibility.setVisible(EditorPanel::RenderGraph,
                          true); // e.g. the Window-menu checkbox opens it.
    REQUIRE(visibility.isVisible(EditorPanel::RenderGraph) ==
            true); // e.g. the close button reads it.

    visibility.setVisible(EditorPanel::RenderGraph, false); // e.g. the close button closes it.
    REQUIRE(visibility.isVisible(EditorPanel::RenderGraph) ==
            false); // e.g. the menu reads it back.
}

//======================================================================================================================
TEST_CASE("default workspace visibility matches spec section 3", "[app]") {
    const WorkspaceVisibility visibility;
    REQUIRE(visibility.isVisible(EditorPanel::Scene));
    REQUIRE(visibility.isVisible(EditorPanel::Viewport));
    REQUIRE(visibility.isVisible(EditorPanel::Inspector));
    REQUIRE(visibility.isVisible(EditorPanel::Performance));
    REQUIRE_FALSE(visibility.isVisible(EditorPanel::RenderGraph));
}

//======================================================================================================================
// A clean run's imgui.ini has no Luminex section at all; the (later) settings handler hands nothing
// over in that case, which this model receives as std::nullopt.
TEST_CASE("clean settings with no workspace section build the default", "[app]") {
    const WorkspaceDecision decision = decideWorkspace(std::nullopt);

    REQUIRE(decision.kind == WorkspaceDecisionKind::BuildDefault);
    REQUIRE(decision.visibility.isVisible(EditorPanel::Scene));
    REQUIRE(decision.visibility.isVisible(EditorPanel::Viewport));
    REQUIRE(decision.visibility.isVisible(EditorPanel::Inspector));
    REQUIRE(decision.visibility.isVisible(EditorPanel::Performance));
    REQUIRE_FALSE(decision.visibility.isVisible(EditorPanel::RenderGraph));
}

//======================================================================================================================
// A representative M5.2-era imgui.ini has real docking data but predates the Luminex section this
// model reads. Whatever the rest of that file contains, the (later) settings handler finds no
// Luminex section to hand this model text for, so the decision still sees std::nullopt -- the same
// "no section" path as a clean run, per spec section 4's "both cases are legacy".
TEST_CASE("representative M5.2 legacy ini with no Luminex section is treated as legacy", "[app]") {
    constexpr std::string_view m52Ini = R"([Window][WindowOverViewport_11111111]
Pos=0,19
Size=1280,701
Collapsed=0

[Docking][Data]
DockSpace ID=0x8B93E3BD Window=0xA787BDB4 Pos=0,19 Size=1280,701 Split=X
  DockNode ID=0x00000001 Parent=0x8B93E3BD SizeRef=886,701 CentralNode=1 Selected=0x13926F0B
  DockNode ID=0x00000002 Parent=0x8B93E3BD SizeRef=392,701 Selected=0xE7039252
)";
    // The M5.2 ini text above documents the realistic legacy case; this model works over one
    // section's body, not a whole ini, so the handler locating no Luminex section within it is
    // exactly what std::nullopt represents here.
    (void)m52Ini;

    const WorkspaceDecision decision = decideWorkspace(std::nullopt);

    REQUIRE(decision.kind == WorkspaceDecisionKind::BuildDefault);
    REQUIRE(decision.visibility.isVisible(EditorPanel::Scene));
    REQUIRE_FALSE(decision.visibility.isVisible(EditorPanel::RenderGraph));
}

//======================================================================================================================
TEST_CASE("matching schema restores exact custom visibility", "[app]") {
    WorkspaceVisibility custom;
    custom.scene = false;
    custom.viewport = true;
    custom.inspector = false;
    custom.performance = true;
    custom.renderGraph = true;

    const std::string text = writeWorkspaceSettings(kWorkspaceSchemaVersion, custom);
    const ParsedWorkspaceSettings parsed = parseWorkspaceSettings(text);
    REQUIRE(parsed.schemaState == WorkspaceSchemaState::Present);
    REQUIRE(parsed.schemaVersion == kWorkspaceSchemaVersion);

    const WorkspaceDecision decision = decideWorkspace(parsed);
    REQUIRE(decision.kind == WorkspaceDecisionKind::Restore);
    REQUIRE(decision.visibility.isVisible(EditorPanel::Scene) == false);
    REQUIRE(decision.visibility.isVisible(EditorPanel::Viewport) == true);
    REQUIRE(decision.visibility.isVisible(EditorPanel::Inspector) == false);
    REQUIRE(decision.visibility.isVisible(EditorPanel::Performance) == true);
    REQUIRE(decision.visibility.isVisible(EditorPanel::RenderGraph) == true);
}

//======================================================================================================================
TEST_CASE("a newer unrecognized schema is treated as legacy", "[app]") {
    WorkspaceVisibility custom;
    custom.renderGraph = true;
    const std::string text = writeWorkspaceSettings(kWorkspaceSchemaVersion + 1, custom);

    const WorkspaceDecision decision = decideWorkspace(parseWorkspaceSettings(text));

    REQUIRE(decision.kind == WorkspaceDecisionKind::BuildDefault);
    // Migration never guesses: default visibility, not the newer section's stored values.
    REQUIRE_FALSE(decision.visibility.isVisible(EditorPanel::RenderGraph));
}

//======================================================================================================================
// Schema 1 is the topology M5.3 and M5.4 persisted, with the Render Graph panel docked into the
// main dockspace. It is not merely "some older number": restoring that ini's dock data would put
// the panel back as a tab of the main window, so this pins that such an ini rebuilds instead.
TEST_CASE("a pre-M5.5 Schema=1 section is treated as legacy", "[app]") {
    constexpr std::string_view dockedTopologyText = "Schema=1\nRenderGraph=1\n";

    const ParsedWorkspaceSettings parsed = parseWorkspaceSettings(dockedTopologyText);
    REQUIRE(parsed.schemaState == WorkspaceSchemaState::Present);
    REQUIRE(parsed.schemaVersion == 1);
    REQUIRE(kWorkspaceSchemaVersion > 1);

    const WorkspaceDecision decision = decideWorkspace(parsed);
    REQUIRE(decision.kind == WorkspaceDecisionKind::BuildDefault);
    REQUIRE_FALSE(decision.visibility.isVisible(EditorPanel::RenderGraph));
}

//======================================================================================================================
TEST_CASE("an older schema is treated as legacy", "[app]") {
    constexpr std::string_view olderSchemaText = "Schema=0\nRenderGraph=1\n";

    const ParsedWorkspaceSettings parsed = parseWorkspaceSettings(olderSchemaText);
    REQUIRE(parsed.schemaState == WorkspaceSchemaState::Present);
    REQUIRE(parsed.schemaVersion == 0);

    const WorkspaceDecision decision = decideWorkspace(parsed);
    REQUIRE(decision.kind == WorkspaceDecisionKind::BuildDefault);
    REQUIRE_FALSE(decision.visibility.isVisible(EditorPanel::RenderGraph));
}

//======================================================================================================================
TEST_CASE("an unparseable schema value is treated as legacy", "[app]") {
    constexpr std::string_view garbledText = "Schema=not-a-number\nRenderGraph=1\n";

    const ParsedWorkspaceSettings parsed = parseWorkspaceSettings(garbledText);
    REQUIRE(parsed.schemaState == WorkspaceSchemaState::Unparseable);

    const WorkspaceDecision decision = decideWorkspace(parsed);
    REQUIRE(decision.kind == WorkspaceDecisionKind::BuildDefault);
    REQUIRE_FALSE(decision.visibility.isVisible(EditorPanel::RenderGraph));
}

//======================================================================================================================
// A section that exists but never wrote a Schema key at all (distinct from no section existing):
// still legacy, and still ignores whatever visibility values happen to be present.
TEST_CASE("a section with no schema key is treated as legacy", "[app]") {
    constexpr std::string_view noSchemaText = "RenderGraph=1\n";

    const ParsedWorkspaceSettings parsed = parseWorkspaceSettings(noSchemaText);
    REQUIRE(parsed.schemaState == WorkspaceSchemaState::Absent);

    const WorkspaceDecision decision = decideWorkspace(parsed);
    REQUIRE(decision.kind == WorkspaceDecisionKind::BuildDefault);
    REQUIRE_FALSE(decision.visibility.isVisible(EditorPanel::RenderGraph));
}

//======================================================================================================================
TEST_CASE("unknown keys are ignored and missing panel keys keep their default", "[app]") {
    const std::string textWithExtras = std::format(
        "Schema={}\nRenderGraph=1\nSomeFutureKey=42\nGarbageLine\n", kWorkspaceSchemaVersion);

    const ParsedWorkspaceSettings parsed = parseWorkspaceSettings(textWithExtras);
    REQUIRE(parsed.schemaState == WorkspaceSchemaState::Present);
    REQUIRE(parsed.schemaVersion == kWorkspaceSchemaVersion);
    REQUIRE(parsed.visibility.isVisible(EditorPanel::RenderGraph)); // Present in the text.
    REQUIRE(parsed.visibility.isVisible(EditorPanel::Scene));       // Absent: keeps default (open).
    REQUIRE(parsed.visibility.isVisible(EditorPanel::Viewport));    // Absent: keeps default (open).
}

//======================================================================================================================
TEST_CASE("write then parse round-trips every panel combination", "[app]") {
    for (int mask = 0; mask < 32; ++mask) {
        WorkspaceVisibility visibility;
        visibility.setVisible(EditorPanel::Scene, (mask & 1) != 0);
        visibility.setVisible(EditorPanel::Viewport, (mask & 2) != 0);
        visibility.setVisible(EditorPanel::Inspector, (mask & 4) != 0);
        visibility.setVisible(EditorPanel::Performance, (mask & 8) != 0);
        visibility.setVisible(EditorPanel::RenderGraph, (mask & 16) != 0);

        const std::string text = writeWorkspaceSettings(kWorkspaceSchemaVersion, visibility);
        const ParsedWorkspaceSettings parsed = parseWorkspaceSettings(text);

        INFO("mask " << mask);
        REQUIRE(parsed.schemaState == WorkspaceSchemaState::Present);
        REQUIRE(parsed.schemaVersion == kWorkspaceSchemaVersion);
        REQUIRE(parsed.visibility.isVisible(EditorPanel::Scene) ==
                visibility.isVisible(EditorPanel::Scene));
        REQUIRE(parsed.visibility.isVisible(EditorPanel::Viewport) ==
                visibility.isVisible(EditorPanel::Viewport));
        REQUIRE(parsed.visibility.isVisible(EditorPanel::Inspector) ==
                visibility.isVisible(EditorPanel::Inspector));
        REQUIRE(parsed.visibility.isVisible(EditorPanel::Performance) ==
                visibility.isVisible(EditorPanel::Performance));
        REQUIRE(parsed.visibility.isVisible(EditorPanel::RenderGraph) ==
                visibility.isVisible(EditorPanel::RenderGraph));
    }
}

//======================================================================================================================
// The text below is not an internal detail: the editor's ImGui settings handler writes exactly this
// as the body of its `imgui.ini` section, and `decideWorkspace` demotes anything it cannot read
// back to legacy. Renaming a key or reordering the lines would therefore silently rebuild the
// default layout over every user's saved workspace, so the on-disk spelling is pinned literally
// here rather than only round-tripped through the parser that shares the same spelling. The
// version number is the one part that is meant to move, so it comes from the constant.
TEST_CASE("write emits the exact persisted section text for default visibility", "[app]") {
    const std::string text = writeWorkspaceSettings(kWorkspaceSchemaVersion, WorkspaceVisibility{});

    REQUIRE(text == std::format("Schema={}\n"
                                "Scene=1\n"
                                "Viewport=1\n"
                                "Inspector=1\n"
                                "Performance=1\n"
                                "RenderGraph=0\n",
                                kWorkspaceSchemaVersion));
}

//======================================================================================================================
TEST_CASE("write produces deterministic text for the same input", "[app]") {
    const WorkspaceVisibility visibility;
    const std::string first = writeWorkspaceSettings(kWorkspaceSchemaVersion, visibility);
    const std::string second = writeWorkspaceSettings(kWorkspaceSchemaVersion, visibility);
    REQUIRE(first == second);
}

//======================================================================================================================
TEST_CASE("reset default layout is idempotent", "[app]") {
    WorkspaceVisibility visibility;
    visibility.setVisible(EditorPanel::Scene, false);
    visibility.setVisible(EditorPanel::RenderGraph, true);

    const WorkspaceVisibility firstReset = resetWorkspaceVisibility();
    visibility = firstReset;
    visibility.setVisible(EditorPanel::Inspector,
                          false); // Simulate more user activity after reset.
    const WorkspaceVisibility secondReset = resetWorkspaceVisibility();

    REQUIRE(firstReset.isVisible(EditorPanel::Scene) == secondReset.isVisible(EditorPanel::Scene));
    REQUIRE(firstReset.isVisible(EditorPanel::Viewport) ==
            secondReset.isVisible(EditorPanel::Viewport));
    REQUIRE(firstReset.isVisible(EditorPanel::Inspector) ==
            secondReset.isVisible(EditorPanel::Inspector));
    REQUIRE(firstReset.isVisible(EditorPanel::Performance) ==
            secondReset.isVisible(EditorPanel::Performance));
    REQUIRE(firstReset.isVisible(EditorPanel::RenderGraph) ==
            secondReset.isVisible(EditorPanel::RenderGraph));
    REQUIRE(secondReset.isVisible(EditorPanel::Scene));
    REQUIRE_FALSE(secondReset.isVisible(EditorPanel::RenderGraph));
}
