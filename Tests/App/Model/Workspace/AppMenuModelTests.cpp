#include "App/Model/Workspace/MenuModel.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>

using namespace lmx::app;

namespace {
using Identity = std::pair<MenuCommand, uint32_t>;

//======================================================================================================================
void collect(const std::vector<MenuItem>& items, std::vector<const MenuItem*>& result) {
    for (const auto& item : items) {
        result.push_back(&item);
        collect(item.children, result);
    }
}

//======================================================================================================================
std::vector<const MenuItem*> flatten(const std::vector<MenuItem>& items) {
    std::vector<const MenuItem*> result;
    collect(items, result);
    return result;
}

//======================================================================================================================
const MenuItem& action(const std::vector<MenuItem>& items, MenuCommand command,
                       uint32_t argument = 0) {
    for (const auto* item : flatten(items))
        if (item->command == command && item->argument == argument)
            return *item;
    FAIL("Missing command " << static_cast<int>(command) << " argument " << argument);
    return items.front();
}

//======================================================================================================================
const MenuItem& named(const std::vector<MenuItem>& items, const std::string& label) {
    for (const auto* item : flatten(items))
        if (item->label == label)
            return *item;
    FAIL("Missing menu row: " << label);
    return items.front();
}

//======================================================================================================================
MenuContext ready() {
    MenuContext context;
    context.scenes = {{"Sponza", true, true, {}},
                      {"Material Lab", true, false, {}},
                      {"San Miguel", false, false, "Run setup --san-miguel."}};
    context.canFrame = context.objectSelected = context.outlineReady = true;
    context.captureAvailable = true;
    context.scenes.push_back({"Temporal Lab", true, false, {}});
    context.scenes.push_back({"Visibility Lab", true, false, {}});
    context.scenes.push_back({"Light Lab", true, false, {}});
    for (uint8_t value = 1; value <= 6; ++value)
        context.debugEntries.push_back({{DebugViewTopic::Temporal, value},
                                        "Temporal " + std::to_string(value),
                                        value < 4,
                                        value < 4 ? "" : "Use Native TAA."});
    for (uint8_t value = 1; value <= 3; ++value)
        context.debugEntries.push_back(
            {{DebugViewTopic::Lighting, value}, "Lighting " + std::to_string(value), true, {}});
    for (uint8_t value = 0; value <= 30; ++value)
        context.debugEntries.push_back({{DebugViewTopic::Occlusion, value},
                                        "HZB level " + std::to_string(value),
                                        false,
                                        "Enable GPU occlusion."});
    return context;
}

//======================================================================================================================
void requireReasons(const std::vector<MenuItem>& items) {
    REQUIRE_FALSE(items.empty());
    for (const auto* item : flatten(items)) {
        INFO(item->label);
        if (!item->separator)
            CHECK(item->enabled == item->disabledReason.empty());
        if (!item->enabled)
            for (const auto* child : flatten(item->children))
                if (!child->separator)
                    CHECK_FALSE(child->enabled);
    }
}
} // namespace

//======================================================================================================================
TEST_CASE("menu has exactly one of every action and option identity", "[app][menu-model]") {
    auto context = ready();
    context.canRetryScene = true;
    context.sceneFailure = "San Miguel could not load: missing file. Current scene kept.";
    const auto items = buildMenuModel(context);
    REQUIRE(items.size() == 5);
    CHECK(items[0].label == "File");
    CHECK(items[1].label == "View");
    CHECK(items[2].label == "Window");
    CHECK(items[3].label == "Debug");
    CHECK(items[4].label == "Help");
    std::set<Identity> expected;
    for (auto command :
         {MenuCommand::Open, MenuCommand::RetryScene, MenuCommand::Save, MenuCommand::SaveAs,
          MenuCommand::Revert, MenuCommand::Quit, MenuCommand::SetSceneCamera,
          MenuCommand::ResetCamera, MenuCommand::FrameSelected, MenuCommand::SelectionOutline,
          MenuCommand::EditorCamera, MenuCommand::ZoomOut, MenuCommand::ZoomIn,
          MenuCommand::ResetUiScale, MenuCommand::StyleGallery, MenuCommand::ResetLayout,
          MenuCommand::Capture, MenuCommand::GizmoSpace})
        expected.emplace(command, 0);
    for (auto tool : {GizmoTool::View, GizmoTool::Move, GizmoTool::Rotate, GizmoTool::Scale,
                      GizmoTool::Combined})
        expected.emplace(MenuCommand::GizmoTool, static_cast<uint32_t>(tool));
    for (uint32_t index = 0; index < context.scenes.size(); ++index)
        expected.emplace(MenuCommand::OpenCatalog, index);
    for (auto value : {Appearance::Auto, Appearance::Light, Appearance::Dark})
        expected.emplace(MenuCommand::Appearance, static_cast<uint32_t>(value));
    for (auto value : {Density::Comfortable, Density::Compact})
        expected.emplace(MenuCommand::Density, static_cast<uint32_t>(value));
    for (auto value : kUiScalePresets)
        expected.emplace(MenuCommand::UiScale, value);
    for (auto value :
         {EditorPanel::Scene, EditorPanel::Viewport, EditorPanel::Inspector, EditorPanel::Rendering,
          EditorPanel::PerformanceSummary, EditorPanel::Performance, EditorPanel::RenderGraph,
          EditorPanel::Console, EditorPanel::Session})
        expected.emplace(MenuCommand::Panel, static_cast<uint32_t>(value));
    expected.emplace(MenuCommand::DebugView, 0);
    for (const auto& entry : context.debugEntries)
        expected.emplace(MenuCommand::DebugView, menuDebugArgument(entry.view));
    std::set<Identity> actual;
    for (const auto* item : flatten(items)) {
        if (item->command) {
            CHECK(item->children.empty());
            CHECK(actual.emplace(*item->command, item->argument).second);
        }
    }
    CHECK(actual == expected);
    context.canRetryScene = false;
    const auto noRetry = buildMenuModel(context);
    for (const auto* item : flatten(noRetry))
        CHECK(item->command != MenuCommand::RetryScene);
}

//======================================================================================================================
TEST_CASE("menu shortcut keys and modifiers equal the existing bindings", "[app][menu-model]") {
    const std::map<MenuCommand, Shortcut> expected{
        {MenuCommand::Open, {"O", true, false}},
        {MenuCommand::Save, {"S", true, false}},
        {MenuCommand::SaveAs, {"S", true, true}},
        {MenuCommand::Quit, {"Q", true, false}},
        {MenuCommand::ResetCamera, {"Home", false, false}},
        {MenuCommand::FrameSelected, {"F", false, false}},
        {MenuCommand::ZoomOut, {"-", true, false}},
        {MenuCommand::ZoomIn, {"+", true, false}},
        {MenuCommand::ResetUiScale, {"0", true, false}},
        {MenuCommand::Capture, {"C", false, false}}};
    const auto items = buildMenuModel(ready());
    size_t count = 0;
    for (const auto* item : flatten(items)) {
        if (!item->shortcut || item->command == MenuCommand::GizmoTool ||
            item->command == MenuCommand::GizmoSpace)
            continue;
        REQUIRE(item->command);
        REQUIRE(expected.contains(*item->command));
        const auto& shortcut = expected.at(*item->command);
        CHECK(item->shortcut->key == shortcut.key);
        CHECK(item->shortcut->command == shortcut.command);
        CHECK(item->shortcut->shift == shortcut.shift);
        ++count;
    }
    REQUIRE(count == expected.size());
}

//======================================================================================================================
TEST_CASE("menu workflow and measurement restrictions retain enabled exceptions",
          "[app][menu-model]") {
    for (bool idle : {false, true}) {
        for (bool stopped : {false, true}) {
            for (bool measuring : {false, true}) {
                auto context = ready();
                context.documentIdle = idle;
                context.stopped = stopped;
                context.measuring = measuring;
                context.canRetryScene = true;
                const auto items = buildMenuModel(context);
                requireReasons(items);
                CHECK(action(items, MenuCommand::Open).enabled == (idle && !measuring));
                CHECK(named(items, "Open Scene").enabled == (idle && !measuring));
                CHECK(action(items, MenuCommand::RetryScene).enabled == (idle && !measuring));
                for (auto command : {MenuCommand::Save, MenuCommand::SaveAs, MenuCommand::Revert})
                    CHECK(action(items, command).enabled == (idle && stopped && !measuring));
                CHECK(action(items, MenuCommand::Quit).enabled);
                CHECK(named(items, "View").enabled == idle);
                CHECK(named(items, "Window").enabled == idle);
                CHECK(named(items, "Debug").enabled == idle);
                CHECK(named(items, "Help").enabled == idle);
                CHECK(action(items, MenuCommand::SetSceneCamera).enabled ==
                      (idle && stopped && !measuring));
                for (auto command : {MenuCommand::ResetCamera, MenuCommand::FrameSelected,
                                     MenuCommand::SelectionOutline, MenuCommand::EditorCamera})
                    CHECK(action(items, command).enabled == (idle && !measuring));
                CHECK(named(items, "Debug View").enabled == (idle && !measuring));
                CHECK(named(items, "Appearance").enabled == idle);
                CHECK(named(items, "Density").enabled == idle);
                CHECK(named(items, "UI Scale").enabled == idle);
                CHECK(action(items, MenuCommand::Capture).enabled == idle);
            }
        }
    }
}

//======================================================================================================================
TEST_CASE("menu selection and outline availability keep their recovery reasons",
          "[app][menu-model]") {
    for (bool bounds : {false, true})
        for (bool object : {false, true})
            for (bool readyOutline : {false, true})
                for (bool shown : {false, true}) {
                    auto context = ready();
                    context.canFrame = bounds;
                    context.objectSelected = object;
                    context.outlineReady = readyOutline;
                    context.showOutline = shown;
                    const auto items = buildMenuModel(context);
                    requireReasons(items);
                    CHECK(action(items, MenuCommand::FrameSelected).enabled == bounds);
                    const auto& outline = action(items, MenuCommand::SelectionOutline);
                    CHECK(outline.enabled == (object && readyOutline));
                    CHECK(outline.checked == shown);
                    if (!readyOutline)
                        CHECK(outline.disabledReason ==
                              "Outline allocation failed. Resize the viewport to retry.");
                }
}

//======================================================================================================================
TEST_CASE("menu debug identities distinguish topics and Final", "[app][menu-model]") {
    auto context = ready();
    for (int active = -1; active < static_cast<int>(context.debugEntries.size()); ++active) {
        context.debug =
            active < 0 ? std::nullopt : std::optional(context.debugEntries[active].view);
        const auto items = buildMenuModel(context);
        requireReasons(items);
        CHECK(action(items, MenuCommand::DebugView).checked == (active == -1));
        for (size_t index = 0; index < context.debugEntries.size(); ++index) {
            const auto& entry = context.debugEntries[index];
            const auto argument = menuDebugArgument(entry.view);
            const auto& item = action(items, MenuCommand::DebugView, argument);
            CHECK(item.enabled == entry.available);
            CHECK(item.disabledReason == entry.reason);
            CHECK(item.checked == (static_cast<int>(index) == active));
            REQUIRE(menuDebugView(argument));
            CHECK(menuDebugView(argument)->topic == entry.view.topic);
            CHECK(menuDebugView(argument)->value == entry.view.value);
        }
    }
    CHECK_FALSE(menuDebugView(0));
}

//======================================================================================================================
TEST_CASE("menu checked preferences use every current choice including intermediate scale",
          "[app][menu-model]") {
    for (auto appearance : {Appearance::Auto, Appearance::Light, Appearance::Dark})
        for (auto density : {Density::Comfortable, Density::Compact})
            for (auto scale : {75u, 80u, 90u, 100u, 110u, 125u, 150u, 101u}) {
                auto context = ready();
                context.appearance = appearance;
                context.density = density;
                context.uiScalePercent = scale;
                const auto items = buildMenuModel(context);
                requireReasons(items);
                CHECK(action(items, MenuCommand::ZoomOut).enabled == (scale > 75));
                CHECK(action(items, MenuCommand::ZoomIn).enabled == (scale < 150));
                for (const auto* item : flatten(items)) {
                    if (item->command == MenuCommand::Appearance)
                        CHECK(item->checked ==
                              (item->argument == static_cast<uint32_t>(appearance)));
                    if (item->command == MenuCommand::Density)
                        CHECK(item->checked == (item->argument == static_cast<uint32_t>(density)));
                    if (item->command == MenuCommand::UiScale)
                        CHECK(item->checked == (item->argument == scale));
                }
            }
}

//======================================================================================================================
TEST_CASE("menu panel and gallery checks reflect every visibility independently",
          "[app][menu-model]") {
    auto context = ready();
    for (auto panel :
         {EditorPanel::Scene, EditorPanel::Viewport, EditorPanel::Inspector, EditorPanel::Rendering,
          EditorPanel::PerformanceSummary, EditorPanel::Performance, EditorPanel::RenderGraph,
          EditorPanel::Console, EditorPanel::Session}) {
        for (bool visible : {false, true}) {
            context.visibility.setVisible(panel, visible);
            context.styleGallery = visible;
            const auto items = buildMenuModel(context);
            for (const auto* item : flatten(items))
                if (item->command == MenuCommand::Panel)
                    CHECK(item->checked ==
                          context.visibility.isVisible(static_cast<EditorPanel>(item->argument)));
            CHECK(action(items, MenuCommand::Panel, static_cast<uint32_t>(panel)).checked ==
                  visible);
            CHECK(action(items, MenuCommand::StyleGallery).checked == visible);
        }
    }
}

//======================================================================================================================
TEST_CASE("menu capture pending and unavailable states name the actual recovery",
          "[app][menu-model]") {
    for (bool available : {false, true})
        for (bool pending : {false, true}) {
            auto context = ready();
            context.captureAvailable = available;
            context.capturePending = pending;
            context.captureReason =
                available
                    ? "Waiting for the next drawable frame."
                    : "Relaunch with MTL_CAPTURE_ENABLED=1 xmake run App to enable GPU capture.";
            const auto items = buildMenuModel(context);
            requireReasons(items);
            const auto& capture = action(items, MenuCommand::Capture);
            CHECK(capture.enabled == (available && !pending));
            if (!capture.enabled)
                CHECK(capture.disabledReason == context.captureReason);
        }
}

//======================================================================================================================
TEST_CASE("menu keeps catalog failure text asset hints and scene-specific Controls reachable",
          "[app][menu-model]") {
    auto context = ready();
    context.canRetryScene = true;
    context.sceneFailure = "San Miguel could not load: bad file\nCurrent scene kept. Fix the cause "
                           "and retry, or choose another scene.";
    context.labControls = "Visibility Lab: use GPU classification to inspect the stress grid.";
    const auto items = buildMenuModel(context);
    CHECK(action(items, MenuCommand::OpenCatalog, 0).checked);
    CHECK_FALSE(action(items, MenuCommand::OpenCatalog, 1).checked);
    CHECK(action(items, MenuCommand::OpenCatalog, 0).enabled);
    CHECK(action(items, MenuCommand::OpenCatalog, 2).disabledReason == "Run setup --san-miguel.");
    CHECK(named(items, context.sceneFailure).command == std::nullopt);
    CHECK(named(items, "Run setup --san-miguel.").command == std::nullopt);
    CHECK(named(items, "Controls").children.size() >= 8);
    CHECK(named(items, context.labControls).command == std::nullopt);
    CHECK(named(items, "Hold RMB over the image to look.").enabled);
    CHECK(named(items, "Text editing, popups and RMB look suppress these shortcuts.").enabled);
    context.scenes[2].reason.clear();
    CHECK(action(buildMenuModel(context), MenuCommand::OpenCatalog, 2).disabledReason ==
          "Required scene assets are unavailable.");
}

//======================================================================================================================
TEST_CASE("default menu snapshot explains unavailable capture", "[app][menu-model]") {
    const auto items = buildMenuModel(MenuContext{});
    requireReasons(items);
    CHECK_FALSE(action(items, MenuCommand::Capture).enabled);
    CHECK(action(items, MenuCommand::Capture).disabledReason ==
          "Relaunch with MTL_CAPTURE_ENABLED=1 xmake run App to enable GPU capture.");
}

//======================================================================================================================
TEST_CASE("save chords report their reason instead of running while playing or measuring",
          "[app][menu-model][shortcuts]") {
    for (const auto command : {MenuCommand::Save, MenuCommand::SaveAs}) {
        INFO(static_cast<int>(command));
        auto context = ready();
        const auto stopped = keyboardDecision(buildMenuModel(context), command, 0, {});
        CHECK(stopped.outcome == KeyboardOutcome::Run);
        CHECK(stopped.reason.empty());
        context.stopped = false;
        const auto playing = keyboardDecision(buildMenuModel(context), command, 0, {});
        CHECK(playing.outcome == KeyboardOutcome::Report);
        CHECK(playing.reason == "Stop playback before saving or reverting the scene.");
        context.measuring = true;
        const auto measuring = keyboardDecision(buildMenuModel(context), command, 0, {});
        CHECK(measuring.outcome == KeyboardOutcome::Report);
        CHECK(measuring.reason == "Stop measurement before saving or reverting the scene.");
        const auto typing =
            keyboardDecision(buildMenuModel(context), command, 0, {.textInput = true});
        CHECK(typing.outcome == KeyboardOutcome::Focus);
        CHECK(typing.reason.empty());
    }
    auto context = ready();
    context.measuring = true;
    const auto open = keyboardDecision(buildMenuModel(context), MenuCommand::Open, 0, {});
    CHECK(open.outcome == KeyboardOutcome::Report);
    CHECK(open.reason == "Stop measurement before opening a scene.");
}

//======================================================================================================================
TEST_CASE("the quit chord runs whatever owns keyboard focus", "[app][menu-model][shortcuts]") {
    auto context = ready();
    context.stopped = false;
    context.measuring = true;
    context.documentIdle = false;
    const auto items = buildMenuModel(context);
    for (unsigned flags = 0; flags < 16; ++flags) {
        INFO(flags);
        const ShortcutContext focus{.textInput = bool(flags & 1),
                                    .cameraLook = bool(flags & 2),
                                    .popupOpen = bool(flags & 4),
                                    .otherSurfaceFocused = bool(flags & 8)};
        CHECK(keyboardDecision(items, MenuCommand::Quit, 0, focus).outcome == KeyboardOutcome::Run);
    }
}

//======================================================================================================================
TEST_CASE("keyboard chords separate focus refusals from unavailable commands",
          "[app][menu-model][shortcuts]") {
    auto context = ready();
    context.uiScalePercent = kUiScalePresets.front();
    context.captureAvailable = false;
    context.canFrame = false;
    const auto items = buildMenuModel(context);
    const ShortcutContext selected{.hasSelection = true};
    CHECK(keyboardDecision(items, MenuCommand::ZoomOut, 0, selected).outcome ==
          KeyboardOutcome::Disabled);
    CHECK(keyboardDecision(items, MenuCommand::ZoomIn, 0, selected).outcome ==
          KeyboardOutcome::Run);
    CHECK(keyboardDecision(items, MenuCommand::FrameSelected, 0, selected).outcome ==
          KeyboardOutcome::Disabled);
    CHECK(keyboardDecision(items, MenuCommand::FrameSelected, 0, {}).outcome ==
          KeyboardOutcome::Unmet);
    CHECK(keyboardDecision(items, MenuCommand::FrameSelected, 0, {.otherSurfaceFocused = true})
              .outcome == KeyboardOutcome::OtherSurface);
    CHECK(keyboardDecision(items, MenuCommand::Capture, 0, selected).outcome ==
          KeyboardOutcome::Run);
    CHECK(keyboardDecision(items, MenuCommand::ResetCamera, 0, {.otherSurfaceFocused = true})
              .outcome == KeyboardOutcome::OtherSurface);
    CHECK(keyboardDecision(items, MenuCommand::ResetCamera, 0,
                           {.textInput = true, .otherSurfaceFocused = true})
              .outcome == KeyboardOutcome::Focus);
    for (const auto focus :
         {ShortcutContext{.textInput = true}, ShortcutContext{.cameraLook = true},
          ShortcutContext{.popupOpen = true}})
        CHECK(keyboardDecision(items, MenuCommand::Capture, 0, focus).outcome ==
              KeyboardOutcome::Focus);
    context = ready();
    context.documentIdle = false;
    CHECK(keyboardDecision(buildMenuModel(context), MenuCommand::ResetCamera, 0, {}).outcome ==
          KeyboardOutcome::Disabled);
    CHECK(keyboardDecision(buildMenuModel(context), MenuCommand::Capture, 0, {}).outcome ==
          KeyboardOutcome::Disabled);
}

//======================================================================================================================
TEST_CASE("model shortcuts leave the standard window chords to the platform",
          "[app][menu-model][shortcuts]") {
    for (const auto* item : flatten(buildMenuModel(ready()))) {
        if (!item->shortcut)
            continue;
        INFO(item->label);
        CHECK_FALSE((item->shortcut->command && item->shortcut->key == "W"));
        CHECK_FALSE((item->shortcut->command && item->shortcut->key == "M"));
    }
}

//======================================================================================================================
TEST_CASE("plain typing in a text field leaves no refused-chord record",
          "[app][menu-model][shortcuts]") {
    const ShortcutContext typing{.textInput = true};
    CHECK(keyboardRecord(KeyboardOutcome::Focus, typing, false) == KeyboardRecord::None);
    CHECK(keyboardRecord(KeyboardOutcome::Focus, {.textInput = true, .popupOpen = true}, false) ==
          KeyboardRecord::None);
    CHECK(keyboardRecord(KeyboardOutcome::Focus, {.textInput = true, .otherSurfaceFocused = true},
                         false) == KeyboardRecord::None);
    CHECK(keyboardRecord(KeyboardOutcome::Focus, typing, true) == KeyboardRecord::Debug);
    for (const auto focus :
         {ShortcutContext{.cameraLook = true}, ShortcutContext{.popupOpen = true}})
        for (const bool command : {false, true})
            CHECK(keyboardRecord(KeyboardOutcome::Focus, focus, command) == KeyboardRecord::Debug);
    for (const bool command : {false, true}) {
        CHECK(keyboardRecord(KeyboardOutcome::OtherSurface, {.otherSurfaceFocused = true},
                             command) == KeyboardRecord::Debug);
        CHECK(keyboardRecord(KeyboardOutcome::Unmet, {}, command) == KeyboardRecord::Warn);
        CHECK(keyboardRecord(KeyboardOutcome::Disabled, {}, command) == KeyboardRecord::Warn);
        CHECK(keyboardRecord(KeyboardOutcome::Run, {}, command) == KeyboardRecord::None);
        CHECK(keyboardRecord(KeyboardOutcome::Report, {}, command) == KeyboardRecord::None);
    }
}

//======================================================================================================================
TEST_CASE("gizmo menu exposes Unity tools and one space toggle", "[app][menu-model][gizmo-tools]") {
    const auto items = buildMenuModel(ready());
    const auto& gizmo = named(items, "Gizmo");
    REQUIRE(gizmo.children.size() == 6);
    const std::pair<const char*, const char*> expected[] = {
        {"View", "Q"},  {"Move", "W"},      {"Rotate", "E"},
        {"Scale", "R"}, {"Transform", "Y"}, {"World/Local (World)", "X"}};
    const GizmoTool tools[] = {GizmoTool::View, GizmoTool::Move, GizmoTool::Rotate,
                               GizmoTool::Scale, GizmoTool::Combined};
    for (size_t index = 0; index < gizmo.children.size(); ++index) {
        const auto& entry = gizmo.children[index];
        CHECK(entry.command == (index < 5 ? MenuCommand::GizmoTool : MenuCommand::GizmoSpace));
        CHECK(entry.argument == (index < 5 ? static_cast<uint32_t>(tools[index]) : 0));
        CHECK(entry.label == expected[index].first);
        REQUIRE(entry.shortcut);
        CHECK(entry.shortcut->key == expected[index].second);
        CHECK_FALSE(entry.shortcut->command);
        CHECK_FALSE(entry.shortcut->shift);
        CHECK(entry.enabled);
        CHECK(entry.checked == (index == 1 || index == 5));
    }
}

//======================================================================================================================
TEST_CASE("gizmo choices reflect session state and stay available during measurement",
          "[app][menu-model][gizmo-tools]") {
    auto context = ready();
    context.measuring = true;
    context.stopped = false;
    for (auto tool : {GizmoTool::View, GizmoTool::Move, GizmoTool::Rotate, GizmoTool::Scale,
                      GizmoTool::Combined}) {
        for (auto space : {GizmoSpace::World, GizmoSpace::Local}) {
            context.gizmo = {tool, space};
            const auto items = buildMenuModel(context);
            const auto& gizmo = named(items, "Gizmo");
            CHECK(gizmo.enabled);
            size_t checkedTools = 0;
            for (const auto& entry : gizmo.children) {
                CHECK(entry.enabled);
                if (entry.command == MenuCommand::GizmoTool) {
                    CHECK(entry.checked == (entry.argument == static_cast<uint32_t>(tool)));
                    checkedTools += entry.checked;
                    CHECK(shortcutPolicy(*entry.command) == EditorShortcut::Gizmo);
                    CHECK(keyboardDecision(items, *entry.command, entry.argument, {}).outcome ==
                          KeyboardOutcome::Run);
                }
            }
            CHECK(checkedTools == 1);
            const auto& toggle = action(items, MenuCommand::GizmoSpace);
            CHECK(toggle.label ==
                  (space == GizmoSpace::World ? "World/Local (World)" : "World/Local (Local)"));
            CHECK(toggle.checked == (space == GizmoSpace::World));
            CHECK(shortcutPolicy(MenuCommand::GizmoSpace) == EditorShortcut::Gizmo);
            CHECK(keyboardDecision(items, MenuCommand::GizmoSpace, 0, {}).outcome ==
                  KeyboardOutcome::Run);
            for (unsigned flags = 1; flags < 16; ++flags) {
                const ShortcutContext focus{bool(flags & 1), bool(flags & 2), bool(flags & 4),
                                            bool(flags & 8)};
                for (const auto& entry : gizmo.children)
                    CHECK(keyboardDecision(items, *entry.command, entry.argument, focus).outcome ==
                          (flags & 7 ? KeyboardOutcome::Focus : KeyboardOutcome::OtherSurface));
            }
        }
    }
    context.documentIdle = false;
    const auto busy = buildMenuModel(context);
    CHECK_FALSE(named(busy, "Gizmo").enabled);
    for (const auto& entry : named(busy, "Gizmo").children)
        CHECK_FALSE(entry.enabled);
}
