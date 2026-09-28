# App

**Status**: Implemented

App is the editor and headless-runner layer above the renderer. It splits into two units: `AppModel`,
a static library of ImGui/SDL/Metal-free editor logic, and `App`, the SDL3 shell that hosts ImGui,
drives the frame loops, and links AppModel. Render depends on neither; the dependency runs
`Render → AppModel → App`. AppModel and App also link Scenes, which sits above Engine
([Engine, Asset and Scenes](engine.md#scene-documents-and-catalog)).

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

`Source/App/Model` owns `Options/`, `Scene/`, `Graph/`, `Performance/`, `Console/`, `Capture/`,
`Workspace/` and `Rendering/{Settings,Temporal,Lighting,Visibility}/`. App sources use `Shell/`,
`Headless/` and feature folders under `Panels/`, with shared controls in `Panels/Shared/`. These
folders add no contract units. Shell/panel headers, including `InspectorInternal.h`, stay private;
AppModel model headers are public. App's root holds its build file.

## AppModel feature folders

### Options

`AppOptions` parses the command line and its defaults for the editor and headless runners.
`--scene` accepts a catalog ID or a `.scene.gltf` path; asset URIs remain relative to `Assets/`.

### Scene

`SceneSession` borrows library-owned scenes and owns the editor camera, playback, view preparation
and motion commit/reset. First Play captures camera/time, animation-owned poses/emissive strength
and tracked light positions; Stop restores that preview. Unrelated edits and rendering settings
stay outside restoration. Activation starts Stopped and keeps editor and headless first-frame
contracts separate. Authored defaults are captured once per loaded scene; an animated object's
Reset samples its own track at the current time, while light Reset restores authored fields and
its current orbit position. Full `LightId`s keep edits and LightLab pile removal scoped correctly.
`SceneTableDisplay` supplies read-only table diagnostics. `SelectionBounds` transforms reliable
mesh bounds for framing; a rejected selection produces no outline. `EditorSelection` retains
selection through filtering; `SceneLoadState` tracks loading and `SceneDefaults` supplies the
shared display-space clear value.

`SceneTree` builds the document-order hierarchy, retaining ancestors of search matches and
source-node identity across multi-primitive assets. Group, object and light headers edit their own
enabled flags; muted off rows remain distinct from culled rows. Generated children name their lab
and say “not saved”. Filtering preserves selection, and Inspector tests visibility against the
same filtered document rows. Environment opens the scene look in Inspector: Exposure, Bloom and
Shadows edit `Scene::look`, with resets to the loaded or saved document; Sky and IBL are read-only.

`DocumentWorkflow` sequences Open, catalog switches, Save, Save As, Revert and Quit independently
of windowing. Dirty state derives from canonical export when the session edit generation changes;
a failed export stays dirty and surfaces its error. Save, Save As and Revert require Stopped and
no active Measure. Open/switch/Revert/Quit/close ask Save, Discard or Cancel before discarding.
For Open, Save finishes before the chooser opens, so cancelling the chooser cannot cancel that
save. A mutex-protected dialog mailbox keeps the response until the main-thread pump consumes it
before drawable acquisition; pending Quit is reconsidered after that response.

`SceneSession` prepares replacement scenes before invalidating the old one. Save adoption follows
successful export, write, canonical reread/equality and hash. It updates path/library identity and
reset-camera baselines together while retaining live IDs and generated defaults. Save As rejects
aliases of either active file, including the decoded companion buffer. Ordinary reported write
failures roll back the pair; crash atomicity is not provided, and a post-write verification error
can leave new disk bytes without adopting them in memory. See the [operator guide](../guides/scene-documents.md).

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
temporal toggles: enable, jitter, debug view, animation play and camera-track follow. `EditorRenderDefaults` holds renderer reset defaults; `ExposureReset` responds to changes in the
active scene look; `DiagnosticRefresh` shares the 0.25-second (4 Hz) editor diagnostic
publication interval Performance and the Rendering topics use. `TemporalEditorState` tracks scene generation and the
camera-cut latch, retains the last non-`None` reset reason together with its original declared-frame count, and observes
compatible live retired timing independently of metric freeze; it separates the current request, the declared execution
and device availability, so temporal off reports Off/N/A at scale 1 and a vendor fallback retains its original request
and reason, and a waiting state differs from a measured zero. `Renderer`'s own per-frame `TemporalStatus::lastReset` is
unaffected. `DynamicResolution.h` drives the shell-owned resolution controller only while temporal and dynamic
resolution are both active; its publication cursor and its last actual measurement carry separate frame IDs, so an
inactive status never pairs an idle frame with an older measurement. Native TAA stays the default reconstruction.
`DebugView` provides one grouped selector for temporal, lighting and HZB diagnostics, checks availability against the
CLI rules for the effective reconstruction, so a MetalFX fallback keeps native views, and clears conflicts. If a settings edit invalidates the active view, reconciliation returns to Final
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
   Inspector headers name the subject and kind; Reset, enabled only after a change, names its scope.
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
each scene; filtering keeps selection. Hierarchy follows the document and imported source-node tree, with Environment and generated children. View > Editor
Camera selects the camera in Inspector; View also owns Reset Camera (Home), Frame Selected (F), Selection Outline, Debug
View and UI Scale. Frame Selected is also a Hierarchy context action. `EditorShortcuts` suppresses F, Home and C during text
entry, popups, RMB look or Render Graph/Performance focus; C without capture explains why. Help > Controls explains movement. `EditorMenus` and `EditorTransport` share the menu row;
the latter owns Play/Pause, Stop, Step, time and rail follow. The tree root and window title show `*`
when dirty. Stop restores the preview without adding document edits. View > Set Scene Camera from View
is the explicit way to save the editor camera.
`RenderingPanel`, `RenderingTopics` and `RenderingLighting` draw eight collapsing topics, with Reconstruction initially
open. Topic headers have scoped resets that keep the Debug View; controls precede readings and Diagnostics, and Details opens Performance.
Inspector pages share subject/kind/reset headers. Enabled controls on groups, objects and lights preserve
identities and descendant own flags; Measure locks edits. Exposure, Bloom and Shadows live under Environment.

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
deterministic colour-tagged PNGs. Manifest v3 retains display/container/UI fields and adds
`sceneDocument: {path, sha256}`; measurement schema 5 carries the same provenance and a frozen enabled
population. The hash covers loaded JSON then buffer bytes, excluding unsaved edits; PNG `lmx:frame` stays unchanged. The offline
[temporal comparison workflow](../guides/temporal-comparison.md) synchronizes Raw/Native/MetalFX reports and an optional
CPU LDR-FLIP map over final sRGB output, against Native TAA as the comparison baseline rather than ground truth; neither
FLIP nor its Python dependencies enter App. `Headless/Screenshot` runs offscreen screenshots and sequence capture,
`Headless/Measurement` runs headless measurement, and `Headless/OcclusionValidation` runs an offscreen active-extent
step for unscored occlusion recovery evidence. Operator procedures for capture, frame dumps and measurement live in
[gpu-debugging.md](../guides/gpu-debugging.md#capture-and-inspect-a-frame) and
[gpu-debugging.md#measure-visibility-and-submission](../guides/gpu-debugging.md#measure-visibility-and-submission);
frozen comparison evidence is covered in [screenshot-comparison.md](../guides/screenshot-comparison.md).

## Application icon, fonts and UI scale

`Shell/AppIcon.mm` loads the staged `Icons/luminex-icon-1024.png` and sets the AppKit application
icon after window creation. Only `runWindowed` calls it; headless paths skip it. A missing PNG logs
one warning and keeps the system icon. The supplied FACET B2.2 artwork remains provisional, with
owner approval pending; [validation](../milestones/ux/ux3-editor-validation.md#application-icon)
records the unverified Dock/switcher appearance. The product remains a bare executable.

`EditorFont` loads Inter Regular with fixed-width digits and an embedded fallback, then merges the Codicons glyph range
on a 16 px grid. Setup pins Inter 4.1 and Codicons 0.0.46-24; App stages their fonts, licenses and Codicons provenance
in `Fonts/`. Without Codicons, the build still succeeds, buttons show labels and the editor logs one setup warning.
`iconButton` centers visible glyph ink. The shell applies persisted UI scale before `ImGui::NewFrame` from an unscaled
base style. View > UI Scale covers 75–150%; the menu-bar percentage resets to 100%, as does Cmd+0. Cmd+-/Cmd++ exclude
text editing, active widgets, popups and camera look. Missing scale is 100%. Schema 2/3 migration and Reset Default
Layout preserve valid scale. Graph cards remeasure once when scale changes, retaining selection, frozen data and the
canvas's independent navigation. See [gpu-debugging.md#editor-ui-scale](../guides/gpu-debugging.md#editor-ui-scale).

## Tests

`Tests/App/Model/` mirrors each model folder. Capture tests cover metadata/actions/light-check
output; Console covers bounded storage and held views; Graph covers records, topology, layout,
publication and one GPU case. Options tests exercise catalog/path parsing. Performance tests
check retirement joins and frozen measurement populations. Rendering tests cover lighting,
visibility, reset scopes, diagnostics, dynamic resolution and temporal state. Scene tests cover
workflow/mailbox ordering, save adoption, tree/search, enabled edits and playback dirtiness.
Workspace tests cover schema persistence. Shell, Headless and Panels have no direct unit tests.
