//----------------------------------------------------------------------------------------------------------------------
/// @file AppWorkspaceModelTests.cpp
/// @brief Tests panel visibility storage and the workspace persistence schema decision.
//----------------------------------------------------------------------------------------------------------------------

#include <catch2/catch_test_macros.hpp>

#include "App/Model/Workspace/WorkspaceModel.h"

#include <format>
#include <limits>
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
TEST_CASE("default workspace opens docked panels and closes detached diagnostics",
          "[app][workspace]") {
    const WorkspaceVisibility visibility;
    REQUIRE(visibility.isVisible(EditorPanel::Scene));
    REQUIRE(visibility.isVisible(EditorPanel::Viewport));
    REQUIRE(visibility.isVisible(EditorPanel::Inspector));
    REQUIRE(visibility.isVisible(EditorPanel::Console));
    REQUIRE_FALSE(visibility.isVisible(EditorPanel::Performance));
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
    REQUIRE(decision.visibility.isVisible(EditorPanel::Console));
    REQUIRE_FALSE(decision.visibility.isVisible(EditorPanel::Performance));
    REQUIRE_FALSE(decision.visibility.isVisible(EditorPanel::RenderGraph));
    REQUIRE(decision.uiScalePercent == kDefaultUiScalePercent);
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
    custom.console = false;

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
    REQUIRE_FALSE(decision.visibility.isVisible(EditorPanel::Console));
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
    for (int mask = 0; mask < 64; ++mask) {
        WorkspaceVisibility visibility;
        visibility.setVisible(EditorPanel::Scene, (mask & 1) != 0);
        visibility.setVisible(EditorPanel::Viewport, (mask & 2) != 0);
        visibility.setVisible(EditorPanel::Inspector, (mask & 4) != 0);
        visibility.setVisible(EditorPanel::Performance, (mask & 8) != 0);
        visibility.setVisible(EditorPanel::RenderGraph, (mask & 16) != 0);
        visibility.setVisible(EditorPanel::Console, (mask & 32) != 0);

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
        REQUIRE(parsed.visibility.isVisible(EditorPanel::Console) ==
                visibility.isVisible(EditorPanel::Console));
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
                                "Performance=0\n"
                                "RenderGraph=0\n"
                                "Console=1\n"
                                "UiScalePercent=100\n",
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
    REQUIRE(secondReset.isVisible(EditorPanel::Console));
    REQUIRE_FALSE(secondReset.isVisible(EditorPanel::Performance));
    REQUIRE_FALSE(secondReset.isVisible(EditorPanel::RenderGraph));
}

//======================================================================================================================
TEST_CASE("schema three restores console visibility without resetting saved docks",
          "[app][workspace]") {
    REQUIRE(kWorkspaceSchemaVersion == 3);
    const auto parsedCurrent =
        parseWorkspaceSettings("Schema=3\nScene=0\nPerformance=0\nRenderGraph=1\n");
    const auto restored = decideWorkspace(parsedCurrent);
    REQUIRE(restored.kind == WorkspaceDecisionKind::Restore);
    REQUIRE_FALSE(restored.visibility.scene);
    REQUIRE_FALSE(restored.visibility.performance);
    REQUIRE(restored.visibility.renderGraph);
    REQUIRE(restored.visibility.console);
    auto customized = restored.visibility;
    customized.setVisible(EditorPanel::Console, false);
    const auto parsed = parseWorkspaceSettings(writeWorkspaceSettings(3, customized));
    REQUIRE_FALSE(parsed.visibility.isVisible(EditorPanel::Console));
    REQUIRE(decideWorkspace(parsed).kind == WorkspaceDecisionKind::Restore);
}

//======================================================================================================================
TEST_CASE("schema two rebuilds console-only default docks and preserves valid UI scale",
          "[app][workspace]") {
    for (const uint32_t scale : {75u, 113u, 150u}) {
        const auto parsed = parseWorkspaceSettings(
            std::format("Schema=2\nScene=0\nViewport=0\nInspector=0\nPerformance=1\nRenderGraph=1\n"
                        "Console=0\nUiScalePercent={}\n",
                        scale));
        const auto migrated = decideWorkspace(parsed);
        REQUIRE(migrated.kind == WorkspaceDecisionKind::BuildDefault);
        REQUIRE(migrated.uiScalePercent == scale);
        REQUIRE(migrated.visibility.scene);
        REQUIRE(migrated.visibility.viewport);
        REQUIRE(migrated.visibility.inspector);
        REQUIRE(migrated.visibility.console);
        REQUIRE_FALSE(migrated.visibility.performance);
        REQUIRE_FALSE(migrated.visibility.renderGraph);
        const auto persisted = writeWorkspaceSettings(kWorkspaceSchemaVersion, migrated.visibility,
                                                      migrated.uiScalePercent);
        const auto reopened = decideWorkspace(parseWorkspaceSettings(persisted));
        REQUIRE(reopened.kind == WorkspaceDecisionKind::Restore);
        REQUIRE(reopened.uiScalePercent == scale);
        REQUIRE(reopened.visibility.console);
        REQUIRE_FALSE(reopened.visibility.performance);
        REQUIRE_FALSE(reopened.visibility.renderGraph);
    }
}

//======================================================================================================================
TEST_CASE("UI scale normalizes invalid values without rounding supported percentages",
          "[app][workspace]") {
    for (const uint32_t value : {0u, 74u, 151u, std::numeric_limits<uint32_t>::max()}) {
        REQUIRE(normalizedUiScalePercent(value) == 100);
    }
    for (const uint32_t value : {75u, 76u, 99u, 100u, 149u, 150u}) {
        REQUIRE(normalizedUiScalePercent(value) == value);
    }
}

//======================================================================================================================
TEST_CASE("UI scale stepping uses strict neighboring presets and saturates", "[app][workspace]") {
    constexpr std::array<uint32_t, 7> expected{75, 80, 90, 100, 110, 125, 150};
    REQUIRE(kUiScalePresets == expected);
    for (size_t index = 0; index < expected.size(); ++index) {
        const size_t next = index + 1 < expected.size() ? index + 1 : index;
        const size_t previous = index > 0 ? index - 1 : 0;
        REQUIRE(stepUiScalePercent(expected[index], true) == expected[next]);
        REQUIRE(stepUiScalePercent(expected[index], false) == expected[previous]);
    }
    REQUIRE(stepUiScalePercent(76, true) == 80);
    REQUIRE(stepUiScalePercent(76, false) == 75);
    REQUIRE(stepUiScalePercent(124, true) == 125);
    REQUIRE(stepUiScalePercent(124, false) == 110);
    REQUIRE(stepUiScalePercent(0, true) == 110);
    REQUIRE(stepUiScalePercent(151, false) == 90);
}

//======================================================================================================================
TEST_CASE("workspace scale accepts optional integers and safely defaults malformed values",
          "[app][workspace]") {
    for (const std::string_view value :
         {"", "no", "74", "151", "-1", "100.0", "100%", " 100", "100 ", "4294967296", "+100"}) {
        INFO(value);
        for (const uint32_t schema : {2u, 3u}) {
            const auto parsed = parseWorkspaceSettings(
                std::format("Schema={}\nUiScalePercent={}\n", schema, value));
            REQUIRE(parsed.uiScalePercent == 100);
            const auto decision = decideWorkspace(parsed);
            REQUIRE(decision.kind == (schema == 3 ? WorkspaceDecisionKind::Restore
                                                  : WorkspaceDecisionKind::BuildDefault));
            REQUIRE(decision.uiScalePercent == 100);
        }
    }
    REQUIRE(parseWorkspaceSettings("Schema=2\nUiScalePercent=125\nUiScalePercent=no\n")
                .uiScalePercent == 100);
    REQUIRE(parseWorkspaceSettings("Schema=2\r\nUiScalePercent=110\r\n").uiScalePercent == 110);
    const auto existing =
        decideWorkspace(parseWorkspaceSettings("Schema=3\nScene=0\nRenderGraph=1\n"));
    REQUIRE(existing.kind == WorkspaceDecisionKind::Restore);
    REQUIRE(existing.uiScalePercent == 100);
    REQUIRE_FALSE(existing.visibility.scene);
    REQUIRE(existing.visibility.renderGraph);
}

//======================================================================================================================
TEST_CASE("unknown workspace schemas reset UI scale and visibility", "[app][workspace]") {
    for (const std::string_view schema :
         {"Schema=0\n", "Schema=1\n", "Schema=4\n", "Schema=no\n", "Schema=2\nSchema=no\n", ""}) {
        const auto parsed = parseWorkspaceSettings(
            std::string(schema) + "UiScalePercent=125\nPerformance=1\nRenderGraph=1\nConsole=0\n");
        REQUIRE(parsed.uiScalePercent == 125);
        const auto decision = decideWorkspace(parsed);
        REQUIRE(decision.kind == WorkspaceDecisionKind::BuildDefault);
        REQUIRE(decision.uiScalePercent == 100);
        REQUIRE(decision.visibility.console);
        REQUIRE_FALSE(decision.visibility.performance);
        REQUIRE_FALSE(decision.visibility.renderGraph);
    }
    REQUIRE(decideWorkspace(std::nullopt).uiScalePercent == 100);
    const auto restored = decideWorkspace(parseWorkspaceSettings("Schema=3\nUiScalePercent=125\n"));
    REQUIRE(restored.kind == WorkspaceDecisionKind::Restore);
    REQUIRE(restored.uiScalePercent == 125);
}

//======================================================================================================================
TEST_CASE("workspace scale persistence is deterministic and round-trips every supported percentage",
          "[app][workspace]") {
    WorkspaceVisibility visibility;
    visibility.inspector = false;
    for (uint32_t percent = 75; percent <= 150; ++percent) {
        const std::string text = writeWorkspaceSettings(3, visibility, percent);
        REQUIRE(text == writeWorkspaceSettings(3, visibility, percent));
        REQUIRE(text.ends_with(std::format("UiScalePercent={}\n", percent)));
        const auto parsed = parseWorkspaceSettings(text);
        REQUIRE(parsed.uiScalePercent == percent);
        REQUIRE_FALSE(parsed.visibility.inspector);
        REQUIRE(decideWorkspace(parsed).uiScalePercent == percent);
    }
    REQUIRE(parseWorkspaceSettings(writeWorkspaceSettings(3, visibility, 999)).uiScalePercent ==
            100);
}
