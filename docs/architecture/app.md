# App

**Status**: Implemented

App is the editor and headless-runner layer above the renderer. It splits into two units: `AppModel`,
a static library of ImGui/SDL/Metal-free editor logic, and `App`, the SDL3 shell that hosts ImGui,
drives the frame loops, and links AppModel. Render depends on neither; the dependency runs
`Render → AppModel → App`. AppModel and App also link Scenes, which sits above Engine
([Engine, Asset and Scenes](engine.md#scenes-catalog)).

## AppModel and App as units

`AppModel` lives at `Source/App/Model` (target `AppModel`, namespace `lmx::app`) and may depend on
`core`, `asset`, `engine`, `render` and `scenes`, plus glm
([module contract](../conventions/modules.md)). It owns options, capture metadata, selection,
workspace schema, actions, performance and graph models, timing history, frame-record retention,
dynamic-resolution policy, temporal and exposure state, bounded Console storage and presentation,
visibility formatting, and the `MeasurementRun` state machine. Core's `RingBuffer` stores its frame,
timing and Console history. The module checker keeps it free of
ImGui, SDL, Metal and the graph builder: it must not include `RenderGraph.h` directly or
transitively, so its graph observers work only through `CompiledFrameRecord.h`
([render-graph.md](render-graph.md)). App and `Tests` both link `AppModel`; `Tests` compiles only
its own C++ sources.

`App` (`Source/App` outside `Model`, target `App`, namespace `lmx::app`) may depend on `core`,
`asset`, `render`, `engine`, `app-model` and `scenes`, plus glm, imgui, imgui-node-editor and
libsdl3. It owns SDL3, Dear ImGui, the panels, the editor shell, the frame loops and `main`. Both
application loops call Render's shared `FrameDeclaration`
([render-graph.md#framedeclaration](render-graph.md#framedeclaration)); each loop retains the
accepted `CompiledFrameRecord` it returns, appends its own output sink, and owns its own scheduling
and GPU waits. The frame loop advances and commits animation through the shared scene session around
frame declaration.

`Source/App/Model` owns its feature directories directly: `Options/`, `Scene/`, `Graph/`, `Performance/`, `Console/`,
`Capture/`, `Workspace/` and `Rendering/{Settings,Temporal,Lighting,Visibility}/`. The remaining App sources live in
`Shell/` (`main.cpp` and `EditorShell` partials, including workspace persistence/docking and camera input), `Headless/`
(`Screenshot`, `Measurement` and `OcclusionValidation`) and `Panels/` (`Scene/`, `Inspector/`, `Viewport/`, `Graph/`,
`Rendering/`, `Performance/` and `Console/`, with shared controls in `Shared/`); these folders add no contract units of
their own, and App's root holds only its build file. Inspector's camera, local-light, directional-light and object units
share private `Panels/Inspector/InspectorInternal.h`, while dispatch and row helpers stay in `InspectorPanel` itself;
every shell and panel header is private to App, while AppModel's shared model headers stay public.

## AppModel feature folders

### Options

`AppOptions` parses the command line and its defaults for the editor and headless runners.

### Scene

The shared `SceneSession` borrows library-owned scenes, owns the camera, prepares playback and borrowed views, forwards
paced scene-table preparation, and resets or commits motion. `EditorPlayback` owns the Stopped/Playing/Paused state and,
on the first Play, captures camera and time plus animation-owned object poses, emissive strength and tracked light
positions; Stop restores them and resets motion, and the shell separately resets temporal and exposure state. Activation
starts Stopped after restoring the previous run. The preview shares the scene and does not restore rendering settings or
unrelated edits. `SceneSession` captures each scene's authored transform and light defaults once, on first activation,
by full `LightId` for lights; returning to a cached scene never replaces those defaults with edited values, and an
animated object's default samples only that object's rigid track at the current playback time. Resetting a light samples
its current orbit position while restoring its other authored fields, and `SceneSession` separately retains the actual
LightLab pile IDs for bounded runtime Apply/Clear without touching grid lights or their tracks. Editing or resetting one
transform collapses only its own previous transform; the editor separately raises its temporal discontinuity latch.
Activation resets editor motion but preserves headless loader state, so each path keeps its own first-frame contract.
`SceneTableDisplay` formats live scene counts and capacities, geometry bytes, writes, slot and growth events, and
pending release buffers for Rendering's read-only Scene tables topic. `SelectionBounds` uses Core's shared AABB corner
transform on mesh bounds to frame a selection's reliable world bounds; a rejected selection produces no outline.
`EditorSelection` holds the current selection and counts the same scene-only rows under search, including disabled
lights; `SceneLoadState` tracks catalog loading; `SceneDefaults` holds the shared display-space clear value both
interactive and headless rendering use.

### Graph

`FrameRecordRing` holds each retained `CompiledFrameRecord`, never the graph builder, together with its declaration-time
counts, logical image size, render and output extents and context epoch. `GraphSnapshot` shares the same 0.25-second
publication interval as Performance but owns one complete retained record and its exact matched timing set, never
averaged; labels, details and dumps read that publication. First data and Resume publish immediately; later changes,
including topology, wait for the next publication boundary, and Freeze keeps the displayed publication through ring
eviction and scene changes. Live data is stale after one second without a new observed frame, independently of
publication. `GraphNodeModel` derives nodes, edges, a culled band and alias links from a `CompiledFrameRecord`;
`GraphLayout` groups and places stages, collapsing a shared-label-prefix set of at least two same-culled-status passes
into one group node, deduplicating edges and pins that cross a collapsed boundary, and wrapping long chains into rows
under a caller-chosen column count. `GraphLayoutOptions::expandedPinBundles` uses logical node keys: two or more
version-zero `lmx.scene.*` inputs collapse into one pin with all original identities retained. Expansion changes the
layout signature; timing samples and physical temporal slot alternation do not. `GraphInspectorModel` shapes the Render
Graph panel's selection details pane.

### Performance

`PerformanceModel` joins each retained frame's declaration-time counts, extents and context epoch with that frame's
retired timing, then publishes or freezes the result as one snapshot. Its 60-frame timing window refreshes at 4 Hz; the
interval plot measures wall-clock intervals and labels the 60 Hz reference, and sorting preserves pass identity while
schedule order remains available. Freshness follows accepted retirement arrival, independently of publication;
one second without a new accepted frame marks live data stale. `PassTimingHistory` keeps per-pass samples in the same `RingBuffer`. `MeasurementRun`
joins declared CPU samples to retired GPU timings by frame ID, validates the pass inventory, and completes only once
every sample has retired; editor and headless measurement both serialize GPU retirement after each submitted frame,
since the RHI publishes only the newest retired timing set. Reports disclose this pacing, separate the `beginFrame` wait
from encoding, and exclude the post-submit wait, so they do not measure realtime throughput. Editor runs stay
interactive and unscored; headless scored runs refuse validation and capture flags. The Performance > Measure tab starts
the fixed plan; Stop cancels it, Pause stays disabled, and completion or cancellation restores the preview while
retaining results. Closing Performance leaves an active run running. The docked `Performance##Summary` and detached
`Performance` window read one snapshot; Details opens/focuses the detached Live tab. `PassStages` groups by the first
family after `lmx.pass.`, adds Average/Latest costs and preserves member identities and stable sorting. Live has
expandable stage rows and sortable schedule (`#`), Average and Latest columns; More > Individual pass rows also exposes
Min, Max and Samples sorting. Measure retains the plan, results and export. `MeasurementReport` and
`MetricsContextRevision` support the same model, the latter tracking the context epoch.

### Console

`ConsoleLog` serializes ingestion, snapshotting and clearing, capping storage at 2,000 messages and 2 MiB of payload
with a 16 KiB per-message limit, and tags entries with `log::Level`. `ConsoleModel` owns the filtered display and
freezes its snapshot when the user scrolls away from the end. `setScrolledToEnd`, `resumeAtEnd` and `newSinceFreeze`
govern resumption and arrival counts. The snapshot's sequence cursor makes Clear-while-frozen count arrivals from the
atomic Clear boundary, including messages later evicted. `consoleSeverityCounts` counts each displayed severity before
filtering; the chips retain inclusive minimum-severity filtering.

### Capture

`CaptureMetadata` and `LightCheckCapture` record capture-time state. `EditorActions`
retains capture capability and results for menu- and shortcut-raised action intents; `ActionResult`
carries their outcome. `NoticeQueue` keeps the newest result: success expires after six seconds;
pending, failed and unavailable results stay until dismissed or replaced. Ready and empty results
produce no notice.

### Workspace

`WorkspaceModel` owns panel visibility and the workspace persistence schema that the shell's ImGui settings handler
writes into `imgui.ini` (below). `MenuBarFit` keeps one transport row, dropping the time readout, then the zoom
percentage when space is tight; action buttons remain. `EditorIcon` names Codicons glyphs and their readable fallback
labels.

### Rendering

`Rendering/{Settings,Temporal,Lighting,Visibility}` holds rendering-state models. `EditorRenderSettings` carries the
temporal toggles: enable, jitter, debug view, animation play and camera-track follow. `EditorRenderDefaults` and
`ExposureReset` hold scoped reset defaults; `DiagnosticRefresh` shares the 0.25-second (4 Hz) editor diagnostic
publication interval Performance and the Rendering topics use. `TemporalEditorState` tracks scene generation and the
camera-cut latch, retains the last non-`None` reset reason together with its original declared-frame count, and observes
compatible live retired timing independently of metric freeze; it separates the current request, the declared execution
and device availability, so temporal off reports Off/N/A at scale 1 and a vendor fallback retains its original request
and reason, and a waiting state differs from a measured zero. `Renderer`'s own per-frame `TemporalStatus::lastReset` is
unaffected. `DynamicResolution.h` drives the shell-owned resolution controller only while temporal and dynamic
resolution are both active; its publication cursor and its last actual measurement carry separate frame IDs, so an
inactive status never pairs an idle frame with an older measurement. Native TAA stays the default reconstruction.
`DebugView` provides one grouped selector for temporal, lighting and HZB diagnostics, checks availability against the
CLI rules and clears conflicting views. If a settings edit invalidates the active view, reconciliation returns to Final
with a notice. `DiagnosticLegend` describes shader-derived view encodings and Raw-mode placeholders beside the active
mode in the viewport chip, whose title selects views and Close returns to Final. `LightingDisplay` publishes one retired
counter and timing frame every 250 ms, with immediate overflow and check warnings; `LightingHistory`,
`LightingDiagnostics` and `DirectionalLightRole` support the same Lighting topic. `VisibilityDisplay` retains full
object identity and frame-scoped classifications for Inspector diagnostics and Hierarchy badges, and
`VisibilityDiagnostics` supports it.

## Editor shell and windows

`EditorShell` coordinates the panels and the one process-global ImGui context; `EditorWorkspace` owns settings
callbacks, default docking and UI-scale controls; `EditorInput` owns camera input. Hierarchy and Viewport dock beside
Inspector, with Rendering in the Inspector's tab group; Console shares the bottom dock with the compact Performance tab.
Inspector and Console are selected on default layout construction. Detailed Performance and Render Graph each own a
detached native window; their `ImGuiWindowClass` rejects unclassed docking and disables auto-merge. Both start closed.

The ImGui settings handler persists schema 4's eight panel visibilities and UI scale alongside ImGui's docking and
viewport data. Schema 3 migration keeps its six visibilities, valid scale and both detached windows' bounds, shows the
two new panels and rebuilds the main docks once. Saving schema 4 makes later launches restore that layout. Schema 2
rebuilds defaults while keeping valid scale; unknown schemas use defaults. Window > Reset Default Layout preserves
scale, closes the detached windows and resets Performance's next-open bounds. Hierarchy keeps its `Scene` window ID.
Explicit Performance focus resolves ImGui's SDL3 `PlatformHandle` as an `SDL_WindowID` and restores only a minimized
window. The shell consumes a layout-reset intent at the next frame's start.

Menu drawing and keyboard shortcuts only raise action intents; the frame loop consumes quit and
capture at the boundary that already owns each operation, and calls
`ImGui::UpdatePlatformWindows()`/`RenderPlatformWindowsDefault()` after every presented frame so the
vendored Metal 4 ImGui backend renders any detached window with its own command buffer and per-slot
event. That backend's maintained patch quarantines each slot's uploaded vertex and index buffers in a
per-slot `usedBuffers` set until the slot is revisited; platform events and App's main-frame pacing
run before reuse or eviction, which keeps a same-frame window upload from overwriting a GPU read
still in flight.

The Render Graph panel coordinates its detached window with separate units for canvas ownership, selection details and
the dump. Its canvas draws a `GraphLayout` on a vendored `ImGuiNodeEditor` canvas ([ADR
0011](../decisions/0011-vendored-imgui-node-editor.md)) with compact pins that show a full label on hover or selection,
a selection-scoped details pane, and a `columns` control; dragged node positions are session state. Canvas identity
stays stable across the alternating physical temporal resource instances a frame can use, while the details pane still
shows the displayed frame's exact physical names and ranges. Unchanged topology preserves selection, groups and
pan/zoom; a real topology change updates the model and explicitly invalidates any selection that disappeared. The header
has Freeze/Resume, Fit graph, Fit selection and 1:1; More holds Reset layout, Columns and Dump. Frozen/stale status
stays visible, and dump results use shared notices. Double-click a scene import bundle to expand it, or an expanded
import to collapse it. A narrow graph window stacks the canvas and the details pane instead of placing them side by
side.

## Editor surface conventions

The [UX2 placement map](../milestones/ux/ux2.md#placement-map) fixes each control's destination.
Shared primitives in `Panels/Shared/EditorStyle` implement these conventions:

1. **One home per function.** Commands have one menu route, plus a shortcut or frequent context
   action. Panels do not repeat global commands.
2. **Header row.** Frequent panel actions use icon buttons with tooltips; rare actions use More.
   Inspector headers name the subject and kind, with a Reset icon explaining what it restores.
3. **Property grid.** Inspector pages use label | value grids; they reflow to one column below
   `kPropertyGridMinWidth` (260 base UI points, adjusted by UI scale).
4. **Controls, readings, diagnostics.** Actionable readings stay visible. Identifiers, capacities,
   bounds and frame numbers use a collapsed Diagnostics section. Timings belong to Performance;
   other surfaces link there.
5. **Status by exception.** Normal states such as Stopped, Ready and zero evictions stay quiet;
   warnings, failures and pending work remain visible. Explanations use tooltips.
6. **The viewport is the image.** Only pixel-related overlays belong there, including the Debug
   View legend chip with its selector and Close action, and pending capture.

`iconButton` uses square Codicons buttons and readable text labels when the font is unavailable. `nextInRow` shares
width-aware wrapping; `beginHeaderRow` groups panel actions, `overflowMenu` opens their popup, `beginPropertyGrid`
shares the reflow threshold, and `beginDiagnostics` starts collapsed. `drawNotice` displays the retained result and Copy
path/Reveal actions in a dismissible, borderless window at the main viewport's bottom-right work area. `ActionFeedback`
suppresses Ready. File > Open Scene owns catalog availability, loading and retry. Source names are disambiguated within
each scene; filtering keeps selection. Hierarchy contains directional lights, local lights and objects. View > Editor
Camera selects the camera in Inspector; View also owns Reset Camera (Home), Frame Selected (F), Selection Outline, Debug
View and UI Scale. Frame Selected is also a Hierarchy context action. `EditorShortcuts` suppresses F, Home and C during text
entry, popups or RMB look. Help > Controls explains movement. `EditorMenus` and `EditorTransport` share the menu row;
the latter owns Play/Pause, Stop, Step, time and rail follow. The main window title carries the scene name.
`RenderingPanel`, `RenderingTopics` and `RenderingLighting` draw eleven collapsing topics, with Reconstruction initially
open. Seven topic headers have scoped resets; controls precede readings and Diagnostics, and Details opens Performance.
Inspector pages share subject/kind/reset headers, with a local-light enable checkbox that preserves light identity,
edits and orbit tracks.

App alone declares `Render/Passes/SelectionOutline/SelectionOutline`, opting into it after scene
display; see [render-passes.md#selection-outline](render-passes.md#selection-outline) for the pass
itself.

`ConsoleLogSink` subscribes after logger setup and before option parsing or device initialization, and its RAII
subscription lasts through application shutdown; its callbacks only append to the thread-safe Console store. Console
displays UTC timestamps and six severity levels, with minimum-severity chips and case-insensitive search. Scroll-up
holds the displayed rows while logging continues; returning to the bottom or clicking `↓ N new` resumes. The chip
remains visible at zero arrivals. More owns Clear and Copy visible: Clear preserves filters/freeze, and Copy exports the
matching held view. Loss counts print only when nonzero and otherwise remain in search help. Console is read-only.
Header counts can trail the messages by one frame because scroll state is resolved before refreshing the displayed
snapshot. See [gpu-debugging.md](../guides/gpu-debugging.md#console-and-editor-selection-diagnostics) for the operator
workflow.

## Capture and measurement

`ActionFeedback` shows a capture's recovery reasons and Copy path/Reveal; panels never execute capture themselves.
`--capture-sequence <directory> --frames N --warmup W` writes `N` numbered PNGs (or `--capture-format bmp`) after `W`
unsaved frames at 60 Hz into a new or empty directory, recording actual camera, settings and temporal status; a vendor
reconstruction fallback fails the sequence outright. Renderer exposes its display-domain contract to capture metadata
and to the read-only Rendering > Display diagnostics
([render-passes.md#exposure-bloom-and-display](render-passes.md#exposure-bloom-and-display)). Asset's `PngImage` writes
deterministic colour-tagged PNGs, and manifest v2 records the display domain, container and UI absence. The offline
[temporal comparison workflow](../guides/temporal-comparison.md) synchronizes Raw/Native/MetalFX reports and an optional
CPU LDR-FLIP map over final sRGB output, against Native TAA as the comparison baseline rather than ground truth; neither
FLIP nor its Python dependencies enter App. `Headless/Screenshot` runs offscreen screenshots and sequence capture,
`Headless/Measurement` runs headless measurement, and `Headless/OcclusionValidation` runs an offscreen active-extent
step for unscored occlusion recovery evidence. Operator procedures for capture, frame dumps and measurement live in
[gpu-debugging.md](../guides/gpu-debugging.md#capture-and-inspect-a-frame) and
[gpu-debugging.md#measure-visibility-and-submission](../guides/gpu-debugging.md#measure-visibility-and-submission);
frozen comparison evidence is covered in [screenshot-comparison.md](../guides/screenshot-comparison.md).

## Fonts and UI scale

`EditorFont` loads Inter Regular with fixed-width digits and an embedded fallback, then merges the Codicons glyph range
on a 16 px grid. Setup pins Inter 4.1 and Codicons 0.0.46-24; App stages their fonts, licenses and Codicons provenance
in `Fonts/`. Without Codicons, the build still succeeds, buttons show labels and the editor logs one setup warning.
`iconButton` centers visible glyph ink. The shell applies persisted UI scale before `ImGui::NewFrame` from an unscaled
base style. View > UI Scale covers 75–150%; the menu-bar percentage resets to 100%, as does Cmd+0. Cmd+-/Cmd++ exclude
text editing, active widgets, popups and camera look. Missing scale is 100%. Schema 2/3 migration and Reset Default
Layout preserve valid scale. Graph cards remeasure once when scale changes, retaining selection, frozen data and the
canvas's independent navigation. See [gpu-debugging.md#editor-ui-scale](../guides/gpu-debugging.md#editor-ui-scale).

## Tests

- `Tests/App/Model/Capture/`: capture metadata, editor action intents and the light-check capture
  path.
- `Tests/App/Model/Console/`: Console's bounded storage, filtering, scroll freeze, arrivals and Clear/Copy semantics.
- `Tests/App/Model/Graph/`: the frame-record ring, graph layout and node model, graph snapshot
  publication/freshness, scene pin bundles, and the Render Graph inspector model, including one GPU case.
- `Tests/App/Model/Options/`: editor option parsing and defaults.
- `Tests/App/Model/Performance/`: pass timing history, the performance model's snapshot join,
  `MeasurementRun`'s CPU/GPU join, and the lighting join in measurements.
- `Tests/App/Model/Rendering/Lighting/`: directional-light role, lighting display and history, and
  lighting diagnostics.
- `Tests/App/Model/Rendering/Settings/`: editor render defaults and exposure reset scoping.
- `Tests/App/Model/Rendering/Temporal/`: the diagnostic legend, dynamic resolution and temporal
  editor state.
- `Tests/App/Model/Rendering/Visibility/`: GPU visibility, occlusion and visibility display models.
- `Tests/App/Model/Scene/`: editor playback, selection, scene load state, scene session, selection
  bounds and the scene table display.
- `Tests/App/Model/Workspace/`: the workspace persistence schema.

No test covers a `Shell/`, `Headless/` or `Panels/` unit.
