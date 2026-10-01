# UX4 — Design system and themes implementation

**Status**: In progress
> Use subagent-driven-development task by task with the execution overrides below.

**Goal:** deliver the binding [UX4 record](../milestones/ux/ux4.md), accepted 2026-09-30, and its [roadmap gate](../roadmap/editor-experience.md#ux4--design-system-and-themes) as one pull request.
**Architecture:** a generator in `Tools/Theme/` emits checked-in C++ token tables; AppModel owns
pure theme, workspace, provenance, activity and menu models, tested first; the shell writes the
palette into its base style between `Render` and `NewFrame`; AppKit stays in `Shell/*.mm`. Stack:
C++23, ImGui 1.93, imgui-node-editor, SDL 3.4, AppKit, Python, Catch2, xmake; Figma pages 03–05.

## Global constraints

- Parent `main` at `28ab04a`, built at `../Luminex-ux4-parent`; branch `feat/ux4-design-system` in
  `../Luminex-ux4`, this plan its first commit; one pull request, squash-merged after owner
  acceptance; subjects `<scope>: <outcome> (UX4)`; no AI co-author, session link or footer.
- Every commit builds and passes `xmake test -P . Tests/unit`, `xmake format -P . --check`, the root
  checkers run from the worktree root (`AGENTS.md`, nested-worktree gotcha; seven from Task 7) and,
  from Task 2, `python3 Tools/Theme/generate_tokens.py --check`; Task 10 also passes
  `MTL_DEBUG_LAYER=1 xmake test -P . Tests/gpu`.
- Nothing under `RojoRHI/`; no new xrepo dependency; ImGui and node-editor pins and patches stay.
  No renderer, capture, manifest, measurement or CLI change beyond `--appearance`. Color, 1 px
  borders and rounding ≤ 6 pt (pill 10) only; no blur, shadow, gradient or glow.
- Geist: `https://github.com/vercel/geist-font/releases/download/v1.7.2/geist-font-v1.7.2.zip`,
  SHA-256 `7fc800d2ac6b92844895196e5041aca55d814c15db70c44f79b3b83ab82b04e2`, OFL 1.1.
- Evidence and the uncommitted instrumentation patches (applied identically to parent and head)
  live in `../Luminex-evidence/ux4/`. `AGENTS.md` ≤ 250 lines, pages ≤ 300, prose `humanizer`-read.

## Review focus

1. A theme switch mid-crossfade, then a scale or density change, ends on the exact target palette
   at the new metrics (Tasks 3, 5).
2. `Header` → `selection/bg` must not turn collapsing headers blue; only selection is accent (10).
3. With native menus, ⌘C/⌘V still edit a focused text field; F, Home and C typed there do nothing;
   ⌘S saves once, not twice (16).
4. Auto with `SDL_SYSTEM_THEME_UNKNOWN` is Dark; forced Light survives a system change to Dark; a
   detached window opened after forcing matches (3, 6).
5. A missing Geist Medium or Mono falls back to Geist Regular with one warning each; a missing
   Regular uses the embedded font as today (9).

## Execution overrides and waves

A fresh implementer handles each task: Astra for Tasks 2, 5, 6, 9, 10, 15, 16; Sol for the rest.
A fresh Astra reviewer reviews each result; the controller commits serially. Only 6/7 may run in
parallel. W1: 1–4, 5, 6/7, 8; W2: 9–12; W3: 13–14; W4: 15–16; W5: 17–19. Stop after three failed
attempts on the same issue, except Task 7 and Task 12's recorded cost failure (owner overrides). The controller performs every checkpoint in the real App via Computer Use
with gestures, settings, screenshots, visible Figma deviations and unverified rows; no owner
stops. Read/change/restore System Appearance and Reduce Motion via Computer Use with original and
restored evidence. Owner overrides fix encoded `#4CABFD`, body 16 unless Task 12 proves a remedy,
and Task 19 gates/push/PR only: no acceptance or merge, plan retained.

### Task 1: Start execution (main thread)
- [x] Create both worktrees (submodule init, `ThirdParty/*`, `Assets/Fetched` symlinked); build the
  parent App; record, roadmap row 15 (renamed UX4.1–UX4.5) and `AGENTS.md` (net zero) say UX4 is in
  progress. Commit with this plan: `docs: start UX4 execution (UX4)`.

### Task 2: Token generator (UX4.1, Astra)
**Files:** create `Tools/Theme/generate_tokens.py` (the owner's `ux4_tokens.py`, math unchanged
but for the composite below), `figma-variables.json` beside it, `Tools/tests/test_theme_tokens.py`;
generated `Source/App/Model/Workspace/EditorThemeTokens.{h,cpp}`; a `--check` step in CI policy.
**Produces:** `enum class ThemeRole : uint16_t` (`surface/canvas` → `SurfaceCanvas`, `graph/link-0`
→ `GraphLink0`, plus `console/{trace,debug,info,warn,error,critical}` and `overlay/text`),
`kThemeRoleCount`; `ThemeColor { float r, g, b, a; }` (encoded sRGB, `byte / 255.0f`);
`ThemePalette = std::array<ThemeColor, kThemeRoleCount>`, `kDarkPalette`, `kLightPalette`;
`SlotRole { std::string_view name; ThemeRole role; float alphaScale; }`, `kImGuiSlots` (63,
`ImGuiCol_` order), `kNodeEditorSlots` (19); `ContrastPair { label; ThemeRole fg, bg;
std::optional<ThemeRole> underlay; double minimum; }`, 24 per theme. `--check` exits 1 naming stale
outputs; `--audit` prints both tables. The one math change: the selection pair composites in encoded
sRGB as ImGui blends, not linear light; Task 18 restates that ratio in the record.
- [x] Unnamed slots: `TextDisabled` → `text/disabled`; `ChildBg`, `TableRowBg` → panel ×0;
  scrollbar grab hovered/active → `text/disabled`/`text/secondary`; `SliderGrabActive` →
  operator-active; `HeaderHovered`/`Active` and `TabHovered` → `surface/hover`/`active`/`hover`;
  `SeparatorHovered` → `border/strong`; resize grip hovered/active, `PlotHistogram` and plot hovers
  → operator-hover/operator; `InputTextCursor` → `text/primary`. Node editor: `Bg` canvas, `NodeBg`
  raised, `GroupBg` panel ×0.5, `Grid` and plain borders subtle, hovered operator-hover, selected,
  pin and flow operator, highlight operator-active, selection rects and `PinRect` operator-subtle.
- [x] Tests first: each hex of the record's semantic table (parsed from `ux4.md`) is generated
  exactly; slot lists are complete and unique; `--check` names a tampered output; output is
  `clang-format` stable. **Verify:** `--audit` 48/48. Commit `tool: generate theme tokens (UX4)`.

### Task 3: Theme model and contrast tests (UX4.1)
**Files:** create `Source/App/Model/Workspace/EditorTheme.{h,cpp}`,
`Tests/App/Model/Workspace/{AppThemeContrastTests,AppEditorThemeTests}.cpp`. **Consumes:** Task 2.
**Produces:** `Appearance { Auto, Light, Dark }`, `Density { Comfortable, Compact }`,
`SystemTheme { Unknown, Light, Dark }`, `ThemeKind { Dark, Light }`; `resolveTheme(Appearance,
SystemTheme)`; `forcedWindowAppearance(Appearance) -> std::optional<ThemeKind>`; `themePalette`;
`composite(top, bottom)`; `contrastRatio(a, b) -> double`; parse/name pairs for `auto|light|dark`,
`comfortable|compact`; `densityMetrics(Density)` → frame padding, item spacing, window padding
(8×5, 8×8, 12 / 6×3, 6×4, 8); `ThemeTransition` `start(from, to, now, reduceMotion)`,
`sample(now)`, `active(now)` over 0.160 s; `AppearanceState { persisted; optional override;
effective(); choose(Appearance) }`, `choose` clearing the override.
- [x] Tests first: all 24 pairs per theme meet their floors through an independent WCAG 2.2
  formula; `resolveTheme(Auto, Unknown) == Dark` and forced modes ignore the system, Auto forcing
  no window appearance (Review focus 4); a transition samples `from` at start and exactly `to` from
  160 ms; a restart mid-fade starts from the sampled color and ends exactly on the new target
  (Review focus 1); reduce motion returns `to` at once. Commit `editor: model editor themes (UX4)`.

### Task 4: Workspace schema 5 and `--appearance` (UX4.1)
**Files:** `WorkspaceModel.{h,cpp}`, `AppWorkspaceModelTests.cpp`, `AppOptions.{h,cpp}`,
`AppOptionsTests.cpp`. **Consumes:** Task 3.
**Produces:** `kWorkspaceSchemaVersion = 5`; `ParsedWorkspaceSettings` and `WorkspaceDecision` gain
`appearance = Auto`, `density = Comfortable`; `writeWorkspaceSettings(..., uiScalePercent,
Appearance = Auto, Density = Comfortable)`; `AppOptions::appearance : std::optional<Appearance>`.
- [x] Tests first: schema 5 round-trips both keys; schema 4 restores docks, visibility and scale
  with Auto and Comfortable and no rebuild; schemas 3 and 2 migrate as today; an unknown value
  keeps the default; `--appearance light` parses; with `--screenshot`, `--capture-sequence` or
  `--measure` it fails naming the windowed editor. Commit `editor: persist appearance (UX4)`.

### Task 5: Apply the theme in the shell (UX4.1, Astra)
**Files:** new `Shell/EditorThemeApply.{h,cpp}`; `EditorShell.{h,cpp}`, `main.cpp`, `EditorMenus`,
`EditorWorkspace`, `RenderGraphCanvas`, `EditorStyle` sources. **Consumes:** Tasks 2–4.
**Produces:** `applyImGuiColors(ImGuiStyle&, const ThemePalette&)`, `applyNodeEditorColors(
ax::NodeEditor::Style&, const ThemePalette&)`, `verifyThemeSlotNames()` (asserts both libraries'
`GetStyleColorName` match the tables), `static_assert`s on `ImGuiCol_COUNT` and `StyleColor_Count`;
`EditorShell::setAppearance(Appearance)`, `onSystemThemeChanged(SystemTheme)`, `uiClearColor()
const -> std::array<float, 4>`; `editor_style::setActivePalette(const ThemePalette&)`,
`color(ThemeRole) -> ImVec4`, `colorU32(ThemeRole, float alphaScale = 1.0f) -> ImU32`.
- [x] Replace `StyleColorsDark` and its overrides. `prepareUIFrame` keys on scale, density, theme
  and transition: a size change rebuilds `m_baseUiStyle` from `densityMetrics` and rescales as
  today; a color change writes only `m_baseUiStyle->Colors` and `style.Colors`. The graph canvas
  applies node-editor colors as it begins; `kUiClearColor` becomes `uiClearColor()`; View >
  Appearance holds Auto (system), Light and Dark, persisted with the density.
- [x] **Verify:** each mode retints the main window and graph canvas; a switch then ⌘+ within 160
  ms ends on the target palette at the new scale (Review focus 1); head-only `theme-switch.patch`
  (twenty switches) keeps `io.Fonts->TexData->UniqueID`, platform texture count and
  `MTLDevice.currentAllocatedSize`. Commit `editor: apply light, dark and auto themes (UX4)`.

### Task 6: System appearance and native windows (UX4.1, Astra)
**Files:** create `Source/App/Shell/AppAppearance.{h,mm}`; modify `main.cpp`, `EditorShell.cpp`.
**Produces:** `systemTheme() -> SystemTheme` (`SDL_GetSystemTheme`), `reduceMotion() -> bool`
(`accessibilityDisplayShouldReduceMotion`), `applyViewportAppearance(std::optional<ThemeKind>)`,
run after `UpdatePlatformWindows`, setting Aqua, Dark Aqua or nil on each viewport's
`PlatformHandleRaw` whose value differs; `NSApp.appearance` is never written.
- [x] Startup reads `systemTheme()`; the SDL theme event calls `onSystemThemeChanged`. **Verify
  (manual, recorded):** under Auto a system change retints editor, Render Graph and Performance
  live; forced Light keeps Aqua title bars under a Dark system; Render Graph opened later matches;
  Reduce Motion snaps. Commit `editor: follow the system appearance (UX4)`.

### Task 7: Editor colors become tokens (UX4.1)
**Files:** `RenderGraphCanvas`, `ViewportPanel`, `ScenePanel`, `ConsolePanel`, `PlaybackToolbar`,
`PerformancePanel`, `EditorStyle` sources; new `Tools/check_literal_colors.py`, run by `xmake policy`
(`xmake/tasks.lua`) and CI's policy job, with `Tools/tests/test_check_literal_colors.py`.
- [x] Test first: under `Source/App/Panels` and `Source/App/Shell` the checker rejects `IM_COL32(`,
  `ImColor(` and `ImVec4` from numeric literals other than all-zero. Graph kinds and links →
  `graph/*`, plots → `plot/*`, overlays and legend chip → `overlay/*`, `surface/overlay`, Console →
  `console/*`; `kAccent`, `kWarning`, `kMuted` → `accent/operator-text`, `status/warning`,
  `text/secondary`. Shader-derived legend swatches stay image data. Commit `editor: draw panel
  colors from tokens (UX4)`.

### Task 8: UX4.1 gate (main thread)
- [x] Gate run recorded: contrast tests; parent/head BMP SHA-256 (six scenes, `--temporal off --frames 1`); 8-round
  `parity_rounds.py` over fifteen cases; Task 5's atlas check; schema 4 docks restore under Auto
  and Comfortable; Task 6's gestures; all in a new `docs/milestones/ux/ux4-validation.md`.
  **Controller check:** both themes against Figma 05; verify one encoded `#4CABFD` outline constant in both themes (owner override of the record's earlier per-theme sentence).

**Result:** exact-image gates failed (5/6 and 9/15); Task 10 verifies the blue outline in both themes; Gallery checks await Tasks 11/17.

### Task 9: Geist replaces Inter (UX4.2, Astra)
**Files:** `xmake/setup.lua`, `Source/App/xmake.lua`, `THIRD_PARTY_NOTICES.md`,
`Shell/EditorFont.{h,cpp}`, `EditorShell.cpp`, `EditorStyle.{h,cpp}`, `EditorTheme.{h,cpp}`, tests.
**Produces:** setup extracts to `ThirdParty/Geist/` with `SOURCE.txt`, checking (under `geist-font/`)
`Geist/ttf/Geist-Regular.ttf` `5c8968eafb98a4c4f47033daf29e38e284a6f2a82eb017d171ab040fe7c4b615`,
`Geist/ttf/Geist-Medium.ttf` `0090e004725f6f64b841715b4167920580f883fcf9b67fc6d744089103fec101`,
`GeistMono/ttf/GeistMono-Regular.ttf` `42d8ad2e610238e64e8abfcde3037c63f7850a73928742b7ab7229d897bcb155`, `OFL.txt`
`c683bfbcc7e087f5d37a54ef628f10387c451a83ddc459b151403a164ac46c90`; Inter's setup and staging go.
`configureEditorFonts() -> EditorFonts { ImFont* sans, *sansMedium, *mono; bool icons; }`; AppModel
`typeSpec(TypeRole { Caption, Body, BodyStrong, Display, MonoCaption, MonoBody }) -> TypeSpec {
TypeFace face; float size; }` (13, 16, 16 Medium, 20 Medium, 13 Mono, 16 Mono), `kDigitAdvanceEm =
0.6f`; `editor_style::setEditorFonts`, `ScopedType(TypeRole)`.
- [x] Tests first: `typeSpec` values; the advance is 9.6 at 16. Sans and Medium merge digits at
  `kDigitAdvanceEm ×` body size, Codicons at 16; Mono keeps its advances; a missing Medium or Mono
  warns once and uses Sans (Review focus 5). Mono serves timestamps, Performance tables, graph
  costs, Diagnostics IDs, hashes, paths and legend ranges; Medium serves subject and topic headers,
  dialog actions and selected tabs of editor-drawn tab bars (dock tabs stay Regular, a recorded
  limit); Display serves dialog titles. **Verify:** clean `xmake setup -P .` fetches and verifies,
  a tampered face fails naming it, a rerun is idempotent, the log names Geist. Commit `editor: set
  the interface in Geist (UX4)`.

### Task 10: Shape, density and restyle (UX4.2, Astra)
**Files:** `EditorTheme.{h,cpp}`, `EditorShell.cpp`, `EditorMenus.cpp`, `EditorStyle.{h,cpp}`,
`RenderingPanel.cpp`, `PerformancePanel.cpp`, `RenderGraphCanvas.cpp`, `ViewportPanel.cpp`,
`check_literal_colors.py`, `Shaders/Passes/SelectionOutline/SelectionOutline.slang`, tests.
**Produces:** `ShapeMetrics { control = 3, popup = 4, card = 6, pill = 10, border = 1, dockGutter
= 2 }` as `kShape`; `editor_style::primaryButton(const char*) -> bool`;
`editor_style::collapsingHeader(const char*, ImGuiTreeNodeFlags = 0) -> bool` pushing
`surface/hover`, `surface/hover`, `surface/active` into the `Header*` slots (Review focus 2).
- [x] Test first: the checker also rejects `ImGui::CollapsingHeader(` outside `EditorStyle`; the
  three calls move to the helper. The base style takes frame, grab, tab and scrollbar rounding
  `control`, popup `popup`, windows and children 0, every border 1, `DockingSeparatorSize` 2 and
  `TreeLinesFlags` `DrawLinesToNodes`; `vector3` fields pass `ImGuiSliderFlags_ColorMarkers`; the
  dirty root draws `UnsavedMarker`. View > Density holds Comfortable and Compact. Dialog confirms,
  Measure Start and notice actions use `primaryButton`; graph cards round 6; notices and the legend
  chip take `surface/overlay`. Use one encoded `#4CABFD` outline shader constant in both themes.
  Commit `editor: add shape and density tokens (UX4)`.

### Task 11: Style Gallery window (UX4.2)
**Files:** create `Source/App/Panels/Gallery/StyleGalleryPanel.{h,cpp}`, `Source/App/Model/Workspace/
GalleryCatalog.{h,cpp}`, `AppGalleryCatalogTests.cpp`; modify `EditorMenus`, `EditorShell` sources.
**Produces:** `GalleryComponent`, `GalleryEntry { GalleryComponent component; std::string_view
figmaName; }`, `galleryCatalog() -> std::span<const GalleryEntry>`; Window > Style Gallery, a
detached window closed at launch and not persisted, whose Current/Dark/Light selector pushes that
palette's 63 colors for its content only.
- [x] Test first: names are unique. Render every state of button, icon-button, checkbox, chip,
  field-text, field-number, field-select, field-slider, dock-tab, menu-item, hierarchy-row,
  property-row, subject-header, topic-header, notice, legend-chip, graph-card and console-row, and
  the type ramp with digit strings at 13, 16 and 20 px. The draw switch has no `default`, so an
  entry without a renderer fails the build. Commit `editor: add the style gallery (UX4)`.

### Task 12: UX4.2 gate (main thread)
- [x] Gate run recorded **FAILED / INCOMPLETE**; owner authorizes continuation. Required [completion gate](../roadmap/editor-experience.md#completion-gate) tasks in both themes,
  maximized and at 1280 × 720. `ui-metrics.patch` logs `io.MetricsRenderIndices` at frame 600 on
  default Sponza from a fresh build-local `imgui.ini`: the parent's one count against head's Dark
  and Light × Comfortable and Compact, each ≤ 1.15×, else `radius/control` drops to 2, then 0. No
  panel stacks labels at the default width; gallery captures at 13, 16 and 20 px on 1× and 2×
  displays show no digit overlap (no 1× display: unverified). Keep body 16 unless the 13/16/20
  captures show specific overlap, clipping or stacking that 17 remedies; record the captures
  and reasoning. If justified, 17 changes only `typeSpec`; this is a controller check.

### Task 13: Provenance and activity models (UX4.3)
**Files:** create `Source/App/Model/Workspace/{Provenance,ActivityModel}.{h,cpp}`,
`Tests/App/Model/Workspace/{AppProvenanceTests,AppActivityModelTests}.cpp`.
**Produces:** `Actor { Operator, System, Agent }`; `Provenance { Authored, Edited, SessionOnly,
SystemApplied, Proposed, AgentApplied }`; `ProvenanceMark { kind; actor; std::string source; }`;
`documentProvenance(bool dirty, string_view path)`, `subjectProvenance(optional<string_view>
generatedBy, optional<string_view> cliFlag, bool edited)`, `resolutionProvenance(bool
controllerOn, float scale, float budgetMs)`, each `-> std::optional<ProvenanceMark>`;
`currentActivity(const ActivityInputs&) -> std::optional<Activity>`, where `ActivityInputs {
optional<MeasureProgress { phase; done; total }> measure; bool capturePending; optional<string>
documentWork; optional<ScaleChange { from; to; at }> controller; double now; }` and `Activity {
actor; verb; optional<float> progress; bool stoppable; tooltip; }`.
- [x] Tests first, case by case: authored has no mark; dirty is operator Edited naming the path;
  generated is SessionOnly "Generated by <lab> · not saved"; a CLI mask names its flag; the
  controller is SystemApplied naming scale and budget; priority is Measure, capture, document work,
  then a 2 s controller change; only Measure stops. Commit `editor: classify provenance (UX4)`.

### Task 14: Marks and activity strip (UX4.3)
**Files:** `EditorStyle.{h,cpp}`, `ScenePanel.cpp`, `Inspector*.cpp`, `EditorTransport.cpp`,
`EditorMenus.cpp`, `MenuBarFit.{h,cpp}`, `AppMenuBarFitTests.cpp`, `StyleGalleryPanel.cpp`,
`GalleryCatalog.cpp`. **Consumes:** Task 13.
**Produces:** `editor_style::actorMark(Actor, float size)`, `provenanceMark(const
ProvenanceMark&)` (dot, diamond, ring, dashed underline, gear; tooltip names `source`),
`activityStrip(const Activity&)` (mark, verb, 2 pt bar, Stop); `MenuBarWidths::activity`.
- [x] Tests first: `fitMenuBar` drops the activity verb (the mark stays), then the readout, then
  zoom. Hierarchy rows, Inspector headers and fields and notices use the marks (the title keeps
  its `*`); every status string stays. The gallery adds activity-strip, attention-ring,
  proposal-card and every agent lifecycle state; nothing else draws agent vocabulary. Commit
  `editor: show actors, provenance and activity (UX4)`.
- [ ] **Controller check (UX4.3):** marks and source tooltips; parent/head screenshots lose no status.

### Task 15: Menu model (UX4.5, Astra)
**Files:** create `Source/App/Model/Workspace/MenuModel.{h,cpp}`, `AppMenuModelTests.cpp`; modify
`EditorMenus.cpp`, `EditorShell.{h,cpp}`, `EditorInput.cpp`.
**Produces:** `MenuCommand` (one value per current File, View, Window, Debug and Help action, plus
`Appearance`, `Density`, `StyleGallery`); `Shortcut { std::string key; bool command, shift; }`;
`MenuItem { label; optional<MenuCommand> command; uint32_t argument; optional<Shortcut> shortcut;
bool checked, enabled, separator; std::string disabledReason; std::vector<MenuItem> children; }`;
`buildMenuModel(const MenuContext&) -> std::vector<MenuItem>`, `MenuContext` holding the state
`EditorMenus.cpp` reads today; `EditorShell::runMenuCommand(MenuCommand, uint32_t argument)`.
- [x] Tests first: each command appears once; shortcuts equal today's (⌘O, ⌘S, ⌘⇧S, ⌘Q, Home, F,
  ⌘−, ⌘+, ⌘0, C); every disabled item has a reason; checked state per case (outline, debug view,
  UI scale, appearance, density, panels). ImGui menus render from the model, reasons as tooltips,
  behavior unchanged. Commit `editor: build menus from one model (UX4)`.

### Task 16: Native macOS menu bar and toolbar row (UX4.5, Astra)
**Files:** create `Source/App/Shell/NativeMenu.{h,mm}`; modify `EditorShell.{h,cpp}`, `main.cpp`,
`EditorMenus.cpp`, `EditorInput.cpp`. **Consumes:** Task 15; `ShortcutContext`, `shortcutAllowed`
(`App/Model/Capture/EditorShortcuts.h`).
**Produces:** `NativeMenuBar` with `static install() -> std::unique_ptr<NativeMenuBar>`,
`update(std::vector<MenuItem>, const ShortcutContext&)`, `takeCommands() ->
std::vector<std::pair<MenuCommand, uint32_t>>`; it replaces SDL's default menu with Luminex (About,
Hide, Hide Others, Show All, Quit → `MenuCommand` Quit), File, Edit, View, Window, Debug and Help;
`update` sets `NSApp.mainMenu.appearance` from `forcedWindowAppearance` (nil under Auto), while the
system menu-bar strip itself follows the system, a recorded limit.
- [x] Fresh implementation review and all 17 global checks PASS; native verification UNVERIFIED. Submenus rebuild in `menuNeedsUpdate:`; disabled reasons use `toolTip`. The
  delegate's `menuHasKeyEquivalent:forEvent:target:action:` resolves target and action itself and
  declines when `shortcutAllowed` refuses, so the key reaches SDL; ImGui stops polling natively
  owned chords (Review focus 3). Edit's Cut, Copy, Paste and Select All post ⌘-chords to a focused
  ImGui text field, else are disabled with "No text field has focus". The row keeps transport,
  activity and zoom; other platforms keep ImGui menus. Commit `editor: use the native macOS menu
  bar (UX4)`. **Verify (main thread, manual):** each command runs once from bar and shortcut;
  Review focus 3; tooltips; the bar follows all modes and serves Render Graph and Performance.

### Task 17: Gallery completion and captures (UX4.4)
**Files:** `GalleryCatalog.cpp`, `AppGalleryCatalogTests.cpp`, `StyleGalleryPanel.cpp`.
- [x] Test first GREEN: exactly 21 names; [2026-10-01 native comparisons](../milestones/ux/ux4-native-followup-validation.md) observed with deviations. **Verify:** capture
  the gallery with `screencapture -o -l <window id>` in both themes at 100% and 150% into
  `../Luminex-evidence/ux4/gallery/`; compare each component with its Figma export. Commit
  `editor: complete the style gallery (UX4)`.

### Task 18: Decision and operator documents (UX4.4)
- [x] Documentation written: ADR 0029 (single color generator, roles, mappings, appearance, schema 5)
  `Proposed`; `docs/architecture/app.md` (split past 300 lines); appearance and density in
  `docs/guides/gpu-debugging.md`; `AGENTS.md` setup, `--appearance`, token check and architecture
  lines; README in shipped-feature terms. Commit `docs: describe the design system (UX4)`.

### Task 19: Final gates and pull request (main thread)
- [x] Combined gate run recorded [FAILED / INCOMPLETE](../milestones/ux/ux4-final-validation.md):
  6/6 BMP, 11/15 publication matrix, Compact/resource failures, GPU PASS; full native gate INCOMPLETE.
  This checkbox records execution only. ADR 0029 stays `Proposed`; record/plan stay `In progress`.
  Published as [draft PR #64](https://github.com/AmanThuL/Luminex/pull/64); [dated native follow-up](../milestones/ux/ux4-native-followup-validation.md) adds scoped evidence; no acceptance, merge or closure. 2026-10-01 independent review and fixes: [record](../milestones/ux/ux4-review-validation.md).
