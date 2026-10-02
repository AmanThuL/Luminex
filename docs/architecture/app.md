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
dynamic-resolution policy, temporal/exposure state, theme/menu models, provenance/activity and bounded
Console storage and presentation,
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
`Workspace/`, `Session/` and `Rendering/{Settings,Temporal,Lighting,Visibility}/`. App sources use `Shell/`,
`Headless/` and feature folders under `Panels/`, with shared controls in `Panels/Shared/`. These
folders add no contract units. Shell/panel headers, including `InspectorInternal.h`, stay private;
AppModel model headers are public. App's root holds its build file.

Session models own proposals, protocol/queries, tiers/plans, listener/mailbox, attribution and Export.
The shell polls files and dispatches at the frame safe point; the listener owns no editor state.
[App Session](app-session.md) describes these units and [Agent Session](../guides/agent-session.md) gives client examples.

## AppModel feature folders

### Options

`AppOptions` parses the command line and its defaults for the editor and headless runners.
`--scene` accepts a catalog ID or a `.scene.gltf` path; asset URIs remain relative to `Assets/`.
`--appearance auto|light|dark` is a session override for the windowed editor; screenshot, sequence
and measurement modes reject it. See [appearance and persistence](app-design-system.md#appearance-and-persistence).

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
and say “not saved”. The tree is cached and rows are clipped, so large labs scale; tooltips build on hover. Filtering
preserves selection, and Inspector tests visibility against the same filtered document rows. Environment opens the scene look in Inspector: Exposure, Bloom and
Shadows edit `Scene::look`, with resets to the loaded or saved document; Sky and IBL are read-only.

`DocumentWorkflow` sequences Open, catalog switches, Save, Save As, Revert and Quit independently
of windowing. Dirty state derives from canonical export when the session edit generation changes;
a failed export stays dirty and surfaces its error. Save, Save As and Revert require Stopped and
no active Measure. Open, switch, Quit and close ask Save, Discard or Cancel before discarding; Revert asks Discard or Cancel.
For Open, Save finishes before the chooser opens, so canceling the chooser cannot cancel that
save. A mutex-protected dialog mailbox keeps the response until the main-thread pump consumes it
before drawable acquisition; pending Quit is reconsidered after that response.

`SceneSession` prepares replacement scenes before invalidating the old one. Save adoption follows
successful export, write, canonical reread/equality and hash. It updates path/library identity and
reset-camera baselines together while retaining live IDs and generated defaults. Save As rejects
aliases of either active file, including the decoded companion buffer. Ordinary reported write
failures roll back the pair; crash atomicity is not provided, and a post-write verification error
can leave new disk bytes without adopting them in memory. Save refuses a companion `.bin` the
target does not name, and Save As drops stale path metadata when it rekeys. See the [operator guide](../guides/scene-documents.md).

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
with a 16 KiB per-message limit, and tags entries with `log::Level` and System/Operator/Agent actor. `ConsoleModel` owns the filtered display and
freezes its snapshot when the user scrolls away from the end. `setScrolledToEnd`, `resumeAtEnd` and `newSinceFreeze`
govern resumption and arrival counts. The snapshot's sequence cursor makes Clear-while-frozen count arrivals from the
atomic Clear boundary, including messages later evicted. `consoleSeverityCounts` counts each displayed severity before
filtering; the chips retain inclusive minimum-severity filtering. Search and actor chips select rows.
Session Export uses this held snapshot and search/severity with Operator and Agent selected, preserving multiline entries.

### Capture

`CaptureMetadata` and `LightCheckCapture` record capture-time state. `EditorActions`
retains capture capability and results for menu- and shortcut-raised action intents; `ActionResult`
carries their outcome. `NoticeQueue` keeps the newest result: success expires after six seconds;
pending, failed and unavailable results stay until dismissed or replaced. Ready and empty results
produce no notice.

### Workspace

`WorkspaceModel` owns nine panel visibilities, UI scale, appearance and density in schema 6.
`EditorTheme` owns pure appearance resolution, transitions and shared metric contracts;
`EditorThemeTokens` holds generated colors and slot mappings. `MenuModel` supplies command state;
`Provenance` and `ActivityModel` describe existing editor state. `GalleryCatalog` names 21 specimens.
`MenuBarFit` drops the activity verb, then time, then zoom while retaining actions and the activity
mark. `EditorIcon` names Codicons glyphs and fallback labels. The
[design-system companion](app-design-system.md) describes these models and their shell consumers.

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
Inspector, with Rendering in the Inspector's tab group; Console shares the bottom dock with Performance summary and Session.
Inspector and Console are selected on default layout construction. Detailed Performance and Render Graph each own a
detached native window; their `ImGuiWindowClass` rejects unclassed docking and disables auto-merge. Both start closed.

The ImGui settings handler persists schema 6's nine visibilities, UI scale, appearance and density
alongside docking and viewport data. Schema 5 restores unchanged with Session hidden; schema 4 restores without rebuilding, using Auto and
Comfortable; schema 3 preserves six visibilities, scale and detached bounds, enables the new tabs
and rebuilds main docks once. Schema 2 keeps valid scale and rebuilds defaults; unknown schemas use
defaults. Reset Default Layout preserves scale, appearance and density, closes detached windows
and resets Performance's next-open bounds. Hierarchy keeps its `Scene` window ID. Explicit
Performance focus resolves SDL3 `PlatformHandle` as an `SDL_WindowID` and restores a minimized
window. The shell consumes layout-reset intent at the next frame's start.

[Native menus](app-design-system.md#menus-and-frame-boundaries) share `MenuModel` and
`runMenuCommand` with the ImGui menu renderer. CPU UI and keyboard ownership finish before drawable
acquisition. A skipped drawable still calls `UpdatePlatformWindows` and applies appearance, with
no platform GPU render. Presented frames render platform windows after the main `endFrame`.
The vendored Metal 4 ImGui backend uses one command buffer and per-slot event per detached window.
Its maintained patch quarantines each slot's uploaded vertex/index buffers in `usedBuffers` until
that slot is revisited; platform events and main-frame pacing precede reuse or eviction.

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

The [surface conventions](app-design-system.md#editor-surface-conventions) describe control
placement, responsive property grids, diagnostics, shared notices and status by exception.
`Panels/Shared/EditorStyle` implements these primitives alongside semantic colors, type scopes,
actor/provenance marks and the activity strip. Native menus own commands; the toolbar owns
transport, activity and zoom. Scene-only captures omit those cues.

App declares the editor-only [selection outline](render-passes.md#selection-outline) after scene
display, using one encoded `#4CABFD` constant in both themes. Console remains bounded and read-only;
its held-view and copy/clear behavior is covered by the
[operator guide](../guides/gpu-debugging.md#console-and-editor-selection-diagnostics).

## Capture and measurement

`ActionFeedback` shows a capture's recovery reasons and Copy path/Reveal; panels never execute capture themselves.
`--capture-sequence <directory> --frames N --warmup W` writes `N` numbered PNGs (or `--capture-format bmp`) after `W`
unsaved frames at 60 Hz into a new or empty directory, recording actual camera, settings and temporal status; a vendor
reconstruction fallback fails the sequence outright. Renderer exposes its display-domain contract to capture metadata
and to the read-only Rendering > Display diagnostics
([render-passes.md#exposure-bloom-and-display](render-passes.md#exposure-bloom-and-display)). Asset's `PngImage` writes
deterministic color-tagged PNGs. Manifest v3 retains display/container/UI fields and adds
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
icon after window creation. Before SDL video initialization it also sets the process name to
"Luminex", which titles the application menu of the unbundled `App` binary; About Luminex passes
the same name, the applied icon and the README's opening description to the standard panel, with
no version. Only `runWindowed` calls either; headless paths skip them. A missing PNG logs
one warning and keeps the system icon. The supplied FACET B2.2 artwork remains provisional, with
owner approval pending; [validation](../milestones/ux/ux3-editor-validation.md#application-icon)
records the unverified Dock/switcher appearance. The product remains a bare executable.

`EditorFont` loads Geist Sans Regular/Medium and Geist Mono Regular; setup pins Geist 1.7.2
with SIL Open Font License 1.1 and provenance. Sans/Medium digits use 0.6 em (9.6 at body 16);
Mono keeps native advances. Codicons stays on a 16-point grid. Missing Medium or Mono falls back
to Regular with one warning per face; missing Regular uses the embedded fallback. Missing Codicons
uses labeled buttons and one warning. App stages resources in `Fonts/` beside the executable.
The [type contract](app-design-system.md#type-shape-and-density) lists roles and limits.

The shell reapplies UI scale from an unscaled base before `NewFrame`; View > UI Scale offers
75/80/90/100/110/125/150%. The toolbar percentage or Cmd+0 resets to 100%; Cmd+-/Cmd++ exclude
editing, active widgets, popups and camera look. Missing scale is 100%. Migration and layout reset
preserve valid scale. Graph cards remeasure on scale changes, retaining selection, frozen data and
independent navigation. See [editor UI scale](../guides/gpu-debugging.md#editor-ui-scale).

## Tests

`Tests/App/Model/` mirrors each model folder. Capture tests cover metadata/actions/light-check
output; Console covers bounded storage and held views; Graph covers records, topology, layout,
publication and one GPU case. Options tests exercise catalog/path parsing. Performance tests
check retirement joins and frozen measurement populations. Rendering tests cover lighting,
visibility, reset scopes, diagnostics, dynamic resolution and temporal state. Scene tests cover
workflow/mailbox ordering, save adoption, tree/search, enabled edits and playback dirtiness.
Workspace tests cover schema migration, appearance/contrast, type/density, provenance/activity,
menu state and Gallery inventory. Shell, Headless and Panels have no direct unit tests.
