# UX2 — Editor surfaces implementation

**Status**: In progress

> **For agentic workers:** REQUIRED SUB-SKILL: use superpowers:subagent-driven-development (or
> superpowers:executing-plans) task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** deliver the [UX2 record](../milestones/ux/ux2.md) as one pull request: every editor
control has one home, the viewport carries no chrome, panels read controls first, and status shows
only when abnormal, with no change to rendered images, capture, measurement or the CLI.

**Architecture:** each decision is a pure AppModel function or class with Catch2 tests written
first; ImGui code in `Source/App/Panels` and `Source/App/Shell` only draws what those models decide.
Shared drawing primitives live in `Panels/Shared/EditorStyle.h`.

**Tech stack:** C++23, Dear ImGui 1.92 docking, SDL3, xmake, Catch2, Codicons 0.0.46-24.
**Spec:** [`docs/milestones/ux/ux2.md`](../milestones/ux/ux2.md), open points decided with the owner
and the record approved on 2026-09-25. Its placement map and conventions are binding.

## Global constraints

- Parent: `docs/ux2-design` at `de6bdcb` (approved record and plan). Branch `feat/ux2-editor-surfaces`; one pull
  request, squash-merged only after the owner accepts it.
- Editor only: no change under `Source/Render`, `Source/Engine`, `Source/Scenes`, `Shaders/`,
  `RojoRHI/`, `Source/App/Headless`, or to `AppOptions` parsing, measurement schema 4, capture
  manifests or PNG metadata. `git diff <parent> -- <those paths>` is empty at every commit.
- AppModel keeps no SDL/ImGui/Metal/RenderGraph dependency; `check_module_deps.py` passes.
- Every commit builds and passes `xmake test Tests/unit`, `xmake format --check` and the root
  checkers. Line budgets: `AGENTS.md` ≤ 250; this plan and architecture/guide/milestone pages ≤ 300.
- Commits follow `docs/conventions/commits.md` (scopes `editor`, `app`, `build`, `docs`); no AI
  co-author or session trailer in any commit or pull request text.
- Codicons: npm `@vscode/codicons` 0.0.46-24 from
  `https://registry.npmjs.org/@vscode/codicons/-/codicons-0.0.46-24.tgz`, SHA-256 `d77bf2ed152e82c4b81288c5271a3481c61559d1a5416593756e3b0fe8a02bf1`;
  `package/dist/codicon.ttf` `3819e4ae4b87350e7c37a5d8f24e71ada2f1f2ee58f7ce5ebc1f88e3c8c38c80`;
  `package/LICENSE` (CC BY 4.0) `af5e030844efddbc7ab00dcfea8b019703753d4d9f5172d727c533a492aec665`.

## Review focus

1. A Debug View whose inputs change underneath it (MetalFX chosen under Rejection, occlusion off
   under HZB) falls back to Final with a notice, never a stale or invalid view (Task 4).
2. After Clear-while-frozen the Console chip counts arrivals from the Clear (Task 10).
3. A schema 3 `imgui.ini` with open detached Performance/Render Graph windows keeps their bounds
   and visibility after migration; docks are rebuilt once, not on every launch (Task 13).
4. Without `codicon.ttf` (setup not rerun) the build still succeeds and the editor runs with
   labelled buttons and exactly one warning (Task 2).
5. F, Home and C do nothing while a text field has focus, a popup is open or during RMB look (Task 5).

## Roles, waves and paths

The main thread coordinates, reviews every result and runs the owner-facing gates. Implementers are
Sonnet 5, reviewed by Opus 5; Tasks 4 and 13 are Opus 5, reviewed by a Fable 5 subagent. Worktree
`../Luminex-ux2` (sibling, not nested) after `git submodule update --init`, with `ThirdParty/*` and
`Assets/Fetched` symlinked from the main checkout and `-P .` on every xmake call. Parent build for
identity checks: `../Luminex-ux2-parent`. Parallel tasks prepare in separate worktrees; the
controller commits them serially in task order.

| Wave | Tasks | Blocks on |
|---|---|---|
| W0 | 1 | — |
| W1 (UX2.1) | 2, 3, 4 in parallel; then 5; then 6 | W0 |
| W2 (UX2.2) | 7, then 8 and 9 in parallel | W1 owner check |
| W3 (UX2.3) | 10, 11, 12 in parallel | W1 |
| W4 (UX2.4) | 13, then 14, then 15 | W2, W3 |

### Task 1: Start execution (main thread)

- [x] Rename the branch, create both worktrees. Record `Accepted` (implementation authorized under
  this plan), this plan `In progress`, `docs/roadmap.md` and `AGENTS.md` (net zero lines) name UX2
  in progress. Policy passes. Commit `docs: start UX2 execution`.

### Task 2: Codicons font and icon table (UX2.1)

**Files:** `xmake/setup.lua`, `Source/App/xmake.lua`, `Source/App/Shell/EditorFont.{h,cpp}`,
`THIRD_PARTY_NOTICES.md`; create `Source/App/Model/Workspace/EditorIcon.{h,cpp}` and
`Tests/App/Model/Workspace/AppEditorIconTests.cpp`.
**Produces:** `enum class EditorIcon` with Codicons code points Play EAD3 (debug-start), Pause
EAD1, Stop EAD7, Step EAD6, Reset EAE2 (discard), Close EA76, More EA7C (ellipsis), Details EB14
(link-external), NewBelow EA9A, FitGraph EB4C (screen-full), FitSelection EBF8 (target), Lock
EA75, Unlock EB74, Rail EADA (device-camera); `struct EditorIconInfo { char32_t codepoint;
std::string_view label; }`; `EditorIconInfo editorIconInfo(EditorIcon)`; `std::string
encodeUtf8(char32_t)`; `bool configureEditorFont()`, true when icons loaded.
- [x] Tests first: the table above; labels non-empty; code points distinct and within
  U+EA60–U+EC40; `encodeUtf8(0xEAD3) == "\xEE\xAB\x93"`. Run, see fail; implement; pass.
- [x] Setup, after the Inter block: download the pinned tarball to a `.download` file, verify it,
  extract `package/dist/codicon.ttf` and `package/LICENSE` into `ThirdParty/Codicons`, verify both,
  write `SOURCE.txt` (package, version, URL, "License: CC BY 4.0; see LICENSE"); skip when the
  verified files exist. `Source/App/xmake.lua` stages them into `Fonts/` as `codicon.ttf`,
  `Codicons-LICENSE.txt`, `Codicons-SOURCE.txt` when present, else one build warning, never a
  failure (Inter keeps its assert). `THIRD_PARTY_NOTICES.md` gains `## Codicons` in Inter's shape.
- [x] `configureEditorFont` merges `Fonts/codicon.ttf` into Inter (`MergeMode`, `GlyphMinAdvanceX =
  kReferenceSize`, range U+EA60–U+EC40); when absent it returns false and logs one `LMX_LOG_WARN`
  naming `xmake setup`. With the staged font moved aside, `LMX_MAX_FRAMES=30` logs it once.
  Commit `build: fetch and stage the Codicons icon font (UX2)`.

### Task 3: Shared primitives and notices (UX2.1)

**Files:** `Source/App/Panels/Shared/EditorStyle.h` (create `EditorStyle.cpp` for non-trivial
bodies), `ActionFeedback.{h,cpp}`, `docs/architecture/app.md`; create
`Source/App/Model/Capture/NoticeQueue.{h,cpp}` and `Tests/App/Model/Capture/AppNoticeQueueTests.cpp`.
**Consumes:** `EditorIcon`. **Produces:** `class NoticeQueue { void post(ActionResult, double
nowSeconds); const ActionResult* current(double nowSeconds) const; void dismiss(); static
constexpr double kSuccessSeconds = 6.0; }`; in `editor_style`: `iconButton(const char* id,
EditorIcon, bool enabled, const char* tooltip) -> bool`, `setIconFontAvailable(bool)`,
`beginHeaderRow()`/`endHeaderRow()`, `overflowMenu(const char* id) -> bool` (a More icon opening a
popup; caller ends it), `beginPropertyGrid(const char* id)` with one `kPropertyGridMinWidth =
260.0f`, `beginDiagnostics() -> bool` (collapsed by default), `nextInRow(float width)`,
`drawNotice(NoticeQueue&, double now)`.
- [ ] Tests first: a success expires after 6 s; Failed, Pending and Unavailable stay until dismissed
  or replaced; a newer post replaces the current one; Ready or an empty result yields null.
- [ ] `iconButton` is square at the frame height, draws the glyph when the font is available and
  the label otherwise, with `editorTooltip`. `nextInRow` replaces the per-file `nextControl` and
  `nextToolbarItem`. `drawNotice` is a small borderless window at the main viewport's bottom-right
  work area with the `drawActionFeedback` body and a Close icon; `drawActionFeedback` prints
  nothing for Ready.
- [ ] Record the six surface conventions in `docs/architecture/app.md`, trimming superseded panel
  prose to stay in budget. Commit `editor: add shared surface primitives (UX2)`.

### Task 4: Debug View selector model (UX2.1, Opus)

**Files:** create `Source/App/Model/Rendering/Settings/DebugView.{h,cpp}` and
`Tests/App/Model/Rendering/Settings/AppDebugViewTests.cpp`.
**Produces:** `enum class DebugViewTopic { Temporal, Lighting, Occlusion }`; `struct DebugView {
DebugViewTopic topic; uint8_t value; }` (`render::TemporalDebugView`, `engine::LightDebugView` or
HZB level); `struct DebugViewEntry { DebugView view; std::string label; bool available;
std::string reason; }`; `debugViewEntries(const EditorRenderSettings&, uint32_t hzbLevels) ->
std::vector<DebugViewEntry>`; `activeDebugView(const EditorRenderSettings&) ->
std::optional<DebugView>`; `selectDebugView(EditorRenderSettings&, std::optional<DebugView>)`,
which clears the other two; `reconcileDebugView(EditorRenderSettings&) ->
std::optional<std::string>`, which returns to Final and returns notice text when invalid.
- [ ] Tests first. Oracle: for every view × temporal {off, raw, taa, metalfx} × local lights {off,
  direct, clustered} × occlusion {off, on} × classify {cpu, gpu}, `available` equals
  `parseAppOptions` accepting `--scene sponza --screenshot o.png --temporal <m> --local-lights <l>
  --occlusion <o> --classify <c>` plus that view's own flag. Also: `selectDebugView` leaves one
  field non-Final; `reason` is non-empty exactly when unavailable; Review focus 1 both ways, and
  `reconcileDebugView` returns nothing for a valid view.
- [ ] Implement from the rules in `AppOptions.cpp` (light views need Clustered; HZB needs occlusion,
  which needs GPU classify; temporal views need temporal on; Rejection, Weight and Age conflict
  with MetalFX); pass. Commit `app: model one Debug View selector (UX2)`.

### Task 5: Menus, viewport and window title (UX2.1)

**Files:** `Source/App/Shell/{EditorShell.h,EditorShell.cpp,EditorWorkspace.cpp,EditorInput.cpp,main.cpp}`,
`Source/App/Panels/Viewport/ViewportPanel.{h,cpp}`; create
`Source/App/Model/Capture/EditorShortcuts.{h,cpp}` and its `AppEditorShortcutsTests.cpp`.
**Consumes:** Tasks 3, 4. **Produces:** `enum class EditorShortcut { FrameSelected, ResetCamera,
Capture }`; `struct ShortcutContext { bool textInput, cameraLook, popupOpen, captureAvailable,
hasSelection; }`; `bool shortcutAllowed(EditorShortcut, const ShortcutContext&)`.
- [ ] Tests first (Review focus 5): every shortcut is refused under text input, camera look or a
  popup; FrameSelected needs a selection; Capture needs availability. Implement; pass.
- [ ] Menus: File (scene catalog, Quit); View (Reset Camera `Home`, Frame Selected `F`, Selection
  Outline, Editor Camera, Debug View grouped by topic with each disabled entry's `reason` as its
  tooltip, UI Scale); Window (panel toggles, Reset Default Layout); Debug (Capture Next GPU Frame
  `C`); Help (Controls, the former Camera help). Reset camera and frame selected move from
  `ViewportPanel.cpp` into shell actions shared by menu, shortcut and Hierarchy; `C`, `F` and
  `Home` go through `shortcutAllowed`.
- [ ] Viewport: `drawToolbar` goes; `drawLegend` becomes a chip over the image whose title is a
  combo of the active topic's entries, with the HZB level stepper and Close to Final; nothing in
  Final. Capture results post notices. `reconcileDebugView` runs each frame before declaration.
  `SDL_SetWindowTitle` shows `<scene> — Luminex`. Commit `editor: move viewport commands into
  menus (UX2)`.

### Task 6: Transport in the menu bar (UX2.1)

**Files:** `Source/App/Panels/Scene/PlaybackToolbar.{h,cpp}`,
`Source/App/Shell/{EditorTransport.cpp,EditorMeasurement.cpp}`, `MeasurementPanel.cpp`; create
`Source/App/Model/Workspace/MenuBarFit.{h,cpp}` and `AppMenuBarFitTests.cpp`.
**Produces:** `struct MenuBarWidths { float menus, buttons, readout, zoom, spacing; }`; `struct
MenuBarFit { bool showReadout; bool showZoom; float transportX; }`; `MenuBarFit fitMenuBar(float
available, const MenuBarWidths&)`.
- [ ] Tests first: with room, the transport is centred between menus and zoom with both shown;
  shrinking drops the readout, then zoom; buttons never drop and `transportX >= menus + spacing`;
  1280 pt at 150% (menus 435, buttons 150, readout 180, zoom 70) shows everything.
- [ ] The main menu bar draws Play/Pause, Stop, Step and Rail (scenes with a rail only) as
  `iconButton`s and a readout: the time, or `Measuring n / N` during a run, when only Stop is
  enabled. The side bar, Scene/Measure combo, options chevron and `measureOnPlay` go; clicking the
  zoom percentage resets it to 100%. The Measure tab gets Start and Stop and loses its instruction
  text. Commit `editor: put the transport in the menu bar (UX2)`.
- [ ] **Owner check (UX2.1):** walk the placement map, reaching each removed command by its named
  route; menus, chip and transport at 1280 × 720 and maximized, at 100% and 150%.

### Task 7: Scene-only Hierarchy (UX2.2)

**Files:** `Source/App/Model/Scene/EditorSelection.{h,cpp}`, `Source/App/Panels/Scene/ScenePanel.cpp`,
`Tests/App/Model/Scene/AppEditorSelectionTests.cpp`.
**Produces:** `buildSceneSelectionRows` emits DirectionalLight, LocalLight and Object rows only;
`struct HierarchyCount { size_t shown, total; }`; `hierarchyCount(const Scene&, std::string_view
filter) -> HierarchyCount`, whose `total` counts the same rows under an empty filter.
- [ ] Tests first: in LightLab and Sponza `total` equals the unfiltered rows including local
  lights, and `shown <= total` for any filter. Cases that asserted Camera or Rendering rows move to
  the scene-only contract; the commit body names each one.
- [ ] Panel: no Workspace group or local-light checkboxes; the search field gets an inline Close
  icon when non-empty; the context menu adds Frame Selected; dimmed rows keep their tooltip.
  Commit `editor: keep the Hierarchy to scene content (UX2)`.

### Task 8: Rendering panel (UX2.2)

**Files:** create `Source/App/Panels/Rendering/RenderingPanel.{h,cpp}`; move the topic bodies out
of `InspectorRendering.cpp` and `drawLightingSection`; `EditorShell.cpp`; `WorkspaceModel.h`
(`EditorPanel::Rendering`, visible by default, not persisted until Task 13).
**Produces:** `kRenderingPanelWindowName = "Rendering"`; `drawRenderingPanel(bool& open, const
InspectorPanelContext&)`.
- [ ] One collapsing header per topic in today's order, Reconstruction open; each header carries a
  Reset icon calling that topic's `resetRenderingGroup` scope, and topics without one show none.
  Controls, then readings, then `beginDiagnostics`; per-pass timings become a Details icon opening
  Performance. `EditorSubject::Rendering` goes. Check every scoped reset against the parent in
  Sponza. Commit `editor: add the Rendering panel (UX2)`.

### Task 9: Inspector pages (UX2.2)

**Files:** `Source/App/Panels/Inspector/*`, `EditorStyle.h`.
- [ ] Every page opens with `beginHeaderRow`: subject name, kind and a Reset icon whose tooltip
  names what it restores; the local-light page adds the enable checkbox calling `editLocalLight`.
  Every page uses `beginPropertyGrid`; identifiers, bounds and frame numbers move under
  `beginDiagnostics`; footnotes become tooltips. View > Editor Camera selects the Camera subject.
  Commit `editor: give Inspector pages one header and grid (UX2)`.
- [ ] **Owner check (UX2.2):** the completion gate's search/select/edit/restore task, every scoped
  reset as before, and no page stacking labels at the default layout width.

### Task 10: Console (UX2.3)

**Files:** `Source/App/Model/Console/ConsoleModel.{h,cpp}`, `ConsoleLog.h` (a `nextSequence()`
accessor), `Source/App/Panels/Console/ConsolePanel.cpp`, `Tests/App/Model/Console/AppConsoleTests.cpp`.
**Produces:** `ConsoleModel::setScrolledToEnd(bool)`, `newSinceFreeze() const -> uint64_t`,
`resumeAtEnd()`; `consoleSeverityCounts(const ConsoleSnapshot&) -> ConsoleSeverityCounts`;
`autoScroll` removed.
- [ ] Tests first: leaving the end freezes; returning resumes; `newSinceFreeze` counts appends
  since the freeze, or since a Clear while frozen (Review focus 2); `resumeAtEnd` resumes and zeroes
  it; Copy visible copies the frozen view; per-level counts. Existing cases stay unchanged.
- [ ] One row: search with inline Close, severity chips with counts, a More menu with Clear and Copy
  visible; a `↓ N new` chip while frozen; statistics print only when eviction or truncation is
  non-zero, else they sit in the search tooltip. Commit `editor: freeze the Console on scroll (UX2)`.

### Task 11: Performance tab and window (UX2.3)

**Files:** `Source/App/Panels/Performance/*`, `EditorShell.cpp`, `WorkspaceModel.h`
(`EditorPanel::PerformanceSummary`, visible by default, not persisted until Task 13); create
`Source/App/Model/Performance/PassStages.{h,cpp}` and `AppPassStagesTests.cpp`.
**Produces:** `passStage(std::string_view label) -> std::string_view`; `struct StageTimingRow {
std::string stage; double averageMs, latestMs; uint32_t firstSchedule; std::vector<size_t>
members; }`; `groupPassStages(std::span<const PassTimingSummary>) -> std::vector<StageTimingRow>`.
- [ ] Tests first: `lmx.pass.bloom.downsample2` → `bloom`, `lmx.pass.scene` → `scene`, other labels
  are their own stage; bloom rows fold into one stage summing average and latest, ordered by first
  schedule index; costliest-first ordering is stable.
- [ ] Docked tab `Performance##Summary` (no window class): frame and GPU time, a sparkline of
  `frameIntervalsMs`, the three costliest stages, freshness only when stale, and Details opening
  the detached window. That window keeps the name `Performance`, so schema 3 bounds carry over, and
  gains Live and Measure tabs, stage-grouped expandable rows and a sortable `#` column replacing the
  Schedule order checkbox. Both read one `PerformanceModel` snapshot. Commit `editor: add the
  compact Performance tab (UX2)`.

### Task 12: Render Graph header and scene pin bundle (UX2.3)

**Files:** `Source/App/Panels/Graph/{RenderGraphPanel.cpp,RenderGraphCanvas.cpp}`,
`Source/App/Model/Graph/GraphLayout.{h,cpp}`, `AppGraphLayoutTests.cpp`.
**Produces:** `GraphLayoutOptions::expandedPinBundles` (node keys); a collapsed pin `N scene
imports` standing for a node's version-0 inputs whose `resourceName` starts with `lmx.scene.`,
when there are at least two.
- [ ] Tests first: six `lmx.scene.*` imports show one bundle pin and six when expanded; one import
  stays a plain pin; other imports never bundle; `signature` follows expansion, not measurements.
- [ ] One header row: Freeze/Resume (Lock/Unlock) with status only when frozen or stale, FitGraph,
  FitSelection, `1:1`, and More (Reset layout, Columns, Dump). Dump posts a notice; the pooling
  line and explanation become tooltips; memory stays in Details. Commit `editor: compress the
  Render Graph header (UX2)`.
- [ ] **Owner check (UX2.3):** find the costliest pass, freeze, clear, resume; hold graph selection
  and navigation ten seconds under Native TAA, freeze, dump, resume; Console filter, copy, clear.

### Task 13: Workspace schema 4 (UX2.4, Opus)

**Files:** `Source/App/Model/Workspace/WorkspaceModel.{h,cpp}`,
`Source/App/Shell/{EditorWorkspace.cpp,EditorShell.cpp}`, `AppWorkspaceModelTests.cpp`.
- [ ] Tests first: schema 4 round-trips eight visibilities and the scale; schema 3 migrates keeping
  its six visibilities and scale, shows Rendering and the Performance tab, and rebuilds docks once;
  schema 2 and unknown versions keep today's outcomes; Reset Default Layout restores schema 4
  defaults and keeps the scale.
- [ ] Default docking: Rendering as a tab beside the Inspector and the Performance tab beside the
  Console, each neighbour focused. Check Review focus 3 with a saved schema 3 `imgui.ini`. Commit
  `editor: migrate the workspace to schema 4 (UX2)`.

### Task 14: Operator documents (UX2.4)

- [ ] `docs/architecture/app.md` (new units), `docs/guides/gpu-debugging.md`, the editor lines of
  `AGENTS.md` (Debug View, transport, Console, Performance, schema 4, the Codicons setup line) and
  README only where it names removed controls, drafted with the `humanizer` skill loaded. Policy
  passes. Commit `docs: describe the UX2 editor surfaces`.

### Task 15: Acceptance and closure (main thread)

- [ ] Identity: for each installed catalog scene, `--scene <s> --temporal off --frames 4
  --screenshot <s>.png` from parent and head has equal SHA-256. `MTL_DEBUG_LAYER=1 xmake test
  Tests/gpu` passes. Evidence in `../Luminex-evidence/ux2/`.
- [ ] The owner runs the completion gate at both window sizes on restored and reset layouts; record
  every result and every unverified gesture in `docs/milestones/ux/ux2-validation.md`.
- [ ] Record `Implemented` with dated behaviour and limits, this plan removed, roadmap and
  `AGENTS.md` updated; open the pull request.
