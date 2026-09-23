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

`Source/App/Model` owns its feature directories directly: `Options/`, `Scene/`, `Graph/`,
`Performance/`, `Console/`, `Capture/`, `Workspace/` and `Rendering/{Settings,Temporal,Lighting,
Visibility}/`. The remaining App sources live in `Shell/` (`main.cpp` and `EditorShell` partials,
including workspace persistence/docking and camera input), `Headless/` (`Screenshot`, `Measurement`
and `OcclusionValidation`) and `Panels/` (`Scene/`, `Inspector/`, `Viewport/`, `Graph/`,
`Performance/` and `Console/`, with shared controls in `Shared/`); these folders add no contract
units of their own, and App's root holds only its build file. Inspector's camera, rendering,
directional-light and object units share private `Panels/Inspector/InspectorInternal.h`, while
dispatch and row helpers stay in `InspectorPanel` itself; every shell and panel header is private
to App, while AppModel's shared model headers stay public.

## AppModel feature folders

### Options

`AppOptions` parses the command line and its defaults for the editor and headless runners.

### Scene

The shared `SceneSession` borrows library-owned scenes, owns the camera, prepares
playback and borrowed views, forwards paced scene-table preparation, and resets or commits motion.
`EditorPlayback` owns the Stopped/Playing/Paused state and, on the first Play, captures camera and
time plus animation-owned object poses, emissive strength and tracked light positions; Stop restores
them and resets motion, and the shell separately resets temporal and exposure state. Activation
starts Stopped after restoring the previous run. The preview shares the scene and does not restore
rendering settings or unrelated edits. `SceneSession` captures each scene's authored transform and
light defaults once, on first activation, by full `LightId` for lights; returning to a cached scene
never replaces those defaults with edited values, and an animated object's default samples only that
object's rigid track at the current playback time. Resetting a light samples its current orbit
position while restoring its other authored fields, and `SceneSession` separately retains the
actual LightLab pile IDs for bounded runtime Apply/Clear without touching grid lights or their
tracks. Editing or resetting one transform collapses only its own previous transform; the editor
separately raises its temporal discontinuity latch. Activation resets editor motion but preserves
headless loader state, so each path keeps its own first-frame contract. `SceneTableDisplay` formats
live scene counts and capacities, geometry bytes, writes, slot and growth events, and pending
release buffers for Inspector's read-only Scene tables topic. `SelectionBounds` uses Core's shared
AABB corner transform on mesh bounds to frame a selection's reliable world bounds; a rejected
selection produces no outline. `EditorSelection` holds
the current selection, and `SceneLoadState` tracks catalog loading; `SceneDefaults` holds the shared
display-space clear value both interactive and headless rendering use.

### Graph

`FrameRecordRing` holds each retained `CompiledFrameRecord`, never the graph builder, together with
its declaration-time counts, logical image size, render and output extents and context epoch.
`GraphSnapshot` shares the same 0.25-second publication interval as Performance but owns one complete
retained record and its exact matched timing set, never averaged; labels, details and dumps read that
publication. First data and Resume publish immediately; later changes, including topology, wait for
the next publication boundary, and Freeze keeps the displayed publication through ring eviction and
scene changes. `GraphNodeModel` derives nodes, edges, a culled band and alias links from a
`CompiledFrameRecord`; `GraphLayout` groups and places stages, collapsing a shared-label-prefix set
of at least two same-culled-status passes into one group node, deduplicating edges and pins that
cross a collapsed boundary, and wrapping long chains into rows under a caller-chosen column count.
`GraphInspectorModel` shapes the Render Graph panel's selection details pane.

### Performance

`PerformanceModel` joins each retained frame's declaration-time counts, extents and
context epoch with that frame's retired timing, then publishes or freezes the result as one
snapshot. Its 60-frame timing window refreshes at 4 Hz; the interval plot measures wall-clock
intervals and labels the 60 Hz reference, and sorting preserves pass identity while schedule order
remains available. `PassTimingHistory` keeps per-pass samples in the same `RingBuffer`.
`MeasurementRun` joins declared CPU samples to retired GPU timings by frame ID,
validates the pass inventory, and completes only once every sample has retired; editor and headless
measurement both serialize GPU retirement after each submitted frame, since the RHI publishes only
the newest retired timing set. Reports disclose this pacing, separate the `beginFrame` wait from
encoding, and exclude the post-submit wait, so they do not measure realtime throughput. Editor runs
stay interactive and unscored; headless scored runs refuse validation and capture flags. The
top-toolbar Measure Play button starts this fixed plan; Stop cancels it, Pause stays disabled, and
completion or cancellation restores the preview state while retaining results. Performance owns the
plan, results and export in its detached window; selecting Measure mode has no window side effect,
but Play opens or focuses that window once. Closing the window leaves the run active, and completion
or cancellation never reopens it; the toolbar's explicit Show measurement option opens or focuses it
at any time. `MeasurementReport` and `MetricsContextRevision` support the same model, the latter
tracking the context epoch.

### Console

`ConsoleLog` serializes ingestion, snapshotting and clearing, capping storage at 2,000 messages and
2 MiB of payload with a 16 KiB per-message limit, and tags entries with `log::Level`. `ConsoleModel`
owns an independent filtered and frozen display with its own loss counters.

### Capture

`CaptureMetadata` and `LightCheckCapture` record capture-time state. `EditorActions`
retains capture capability and results for menu- and shortcut-raised action intents; `ActionResult`
carries their outcome.

### Workspace

`WorkspaceModel` owns panel visibility and the workspace persistence schema that the
shell's ImGui settings handler writes into `imgui.ini` (below).

### Rendering

`Rendering/{Settings,Temporal,Lighting,Visibility}` holds rendering-state models.
`EditorRenderSettings` carries the temporal toggles: enable, jitter, debug view, animation play and
camera-track follow. `EditorRenderDefaults` and `ExposureReset` hold scoped reset defaults;
`DiagnosticRefresh` shares the 0.25-second (4 Hz) editor diagnostic publication interval Performance
and the Rendering topics use. `TemporalEditorState` tracks scene generation and the camera-cut
latch, retains the last non-`None` reset reason together with its original declared-frame
count, and observes compatible live retired timing independently of metric freeze; it separates the
current request, the declared execution and device availability, so temporal off reports Off/N/A at
scale 1 and a vendor fallback retains its original request and reason, and a waiting state differs
from a measured zero. `Renderer`'s own per-frame `TemporalStatus::lastReset` is unaffected.
`DynamicResolution.h` drives the shell-owned resolution controller only while temporal and dynamic
resolution are both active; its publication cursor and its last actual measurement carry separate
frame IDs, so an inactive status never pairs an idle frame with an older measurement. Native TAA
stays the default reconstruction. Under device reconstruction, Inspector disables the diagnostic
views that need native accumulation and explains why.
`DiagnosticLegend` describes shader-derived view encodings and Raw-mode placeholders beside the
active mode and a Return-to-Final action. `LightingDisplay` publishes one retired counter and timing
frame every 250 ms, with immediate overflow and check warnings; `LightingHistory`,
`LightingDiagnostics` and `DirectionalLightRole` support the same Lighting topic. `VisibilityDisplay`
retains full object identity and frame-scoped classifications for Inspector diagnostics and
Hierarchy badges, and `VisibilityDiagnostics` supports it.

## Editor shell and windows

`EditorShell` coordinates the panels and the one process-global ImGui context; `EditorWorkspace`
owns settings callbacks, default docking and UI-scale controls; `EditorInput` owns camera input.
Hierarchy, Viewport and Inspector dock together, with Console alone below them; Performance and the
Render Graph panel each own a detached native window. Their
`ImGuiWindowClass` disallows unclassed docking and turns off auto-merge under Dear ImGui's platform
viewports (`ImGuiConfigFlags_ViewportsEnable`). The dock builder places neither detached window; both
start closed, and the Window menu's toggles remain explicit actions.

A registered ImGui settings handler persists the workspace schema and panel visibility as Luminex's
own section of `imgui.ini`, beside Dear ImGui's own docking and viewport data. Schema 3 restores
panel visibility and detached-window geometry; schema 2 migrates to the default topology while
preserving a valid UI-scale value. Reset Default Layout closes Performance and the Render Graph
window and resets Performance's next-open bounds; Hierarchy keeps its `Scene` window ID across a
reset. Explicit Performance focus resolves ImGui's SDL3 `PlatformHandle` as an `SDL_WindowID` and
restores the window only if it was minimized. The shell consumes a pending layout-reset intent at
the start of the next frame.

Menu drawing and keyboard shortcuts only raise action intents; the frame loop consumes quit and
capture at the boundary that already owns each operation, and calls
`ImGui::UpdatePlatformWindows()`/`RenderPlatformWindowsDefault()` after every presented frame so the
vendored Metal 4 ImGui backend renders any detached window with its own command buffer and per-slot
event. That backend's maintained patch quarantines each slot's uploaded vertex and index buffers in a
per-slot `usedBuffers` set until the slot is revisited; platform events and App's main-frame pacing
run before reuse or eviction, which keeps a same-frame window upload from overwriting a GPU read
still in flight.

The Render Graph panel coordinates its detached window with separate units for canvas ownership,
selection details and the dump. Its canvas draws a `GraphLayout` on a vendored `ImGuiNodeEditor`
canvas ([ADR 0011](../decisions/0011-vendored-imgui-node-editor.md)) with compact pins that show a
full label on hover or selection, a selection-scoped details pane, and a `columns` control; dragged
node positions are session state. Canvas identity stays stable across the alternating physical
temporal resource instances a frame can use, while the details pane still shows the displayed
frame's exact physical names and ranges. Unchanged topology preserves selection, groups and pan/zoom;
a real topology change updates the model and explicitly invalidates any selection that disappeared.
Fit graph, Fit selection, 100% and Reset layout are explicit actions; a narrow graph window stacks
the canvas and the details pane instead of placing them side by side.

`EditorStyle.h` shares responsive layout fields and delayed contextual tooltips across panels;
Inspector keeps a selected-subject heading above its scrolling fields. Rendering expands into
category subjects, each pairing controls with compact live readings and no nested detail toggles;
category selection also participates in filtering and keyboard navigation, and changing pages resets
scroll. Editable reset groups keep their scoped defaults. File > Open Scene owns catalog
availability, loading and retry. Hierarchy offers compact search, collapsible subject groups and
keyboard navigation; local lights use row clipping with complete generational selection. Lighting
owns its mode, check and view controls plus pile controls and reset; Hierarchy's per-light checkboxes
preserve every light's identity and edits, including disabled lights, and Inspector's per-light edits
decode an authored sRGB colour once into linear storage. Source names use scene-local
disambiguation; a filtered-out selection stays explicit in Inspector and can clear its own filter
there. Viewport owns camera help and Frame selected. The top Scene/Measure toolbar owns one
state-switching Play/Pause button plus separate Stop/Step and camera-rail follow options.

App alone declares `Render/Passes/SelectionOutline/SelectionOutline`, opting into it after scene
display; see [render-passes.md#selection-outline](render-passes.md#selection-outline) for the pass
itself.

`ConsoleLogSink` subscribes after logger setup and before option parsing or device initialization,
and its RAII subscription lasts through application shutdown; its callbacks only append to the
thread-safe Console store. Console displays UTC timestamps and six severity levels, with
minimum-severity and case-insensitive message filters. Freeze latches the display and its counters
while ingestion continues; Resume refreshes the display; Clear empties history and counters while
keeping filters and freeze state; Copy visible exports exactly the messages currently displayed.
Console is read-only, and follow-newest applies only when the view is already at the end. See
[gpu-debugging.md](../guides/gpu-debugging.md#console-and-editor-selection-diagnostics) for the
operator workflow.

## Capture and measurement

`ActionFeedback` shows a capture's recovery reasons and Copy path/Reveal; panels never execute
capture themselves. `--capture-sequence <directory> --frames N --warmup W` writes `N`
numbered PNGs (or `--capture-format bmp`) after `W` unsaved frames at 60 Hz into a new or empty
directory, recording actual camera, settings and temporal status; a vendor reconstruction fallback
fails the sequence outright. Renderer exposes its display-domain contract to capture metadata and
to the read-only Inspector Display details
([render-passes.md#exposure-bloom-and-display](render-passes.md#exposure-bloom-and-display)).
Asset's `PngImage` writes deterministic colour-tagged PNGs, and manifest v2 records the display
domain, container and UI absence. The offline
[temporal comparison workflow](../guides/temporal-comparison.md) synchronizes Raw/Native/MetalFX
reports and an optional CPU LDR-FLIP map over final sRGB output, against Native TAA as the
comparison baseline rather than ground truth; neither FLIP nor its Python dependencies enter App.
`Headless/Screenshot` runs offscreen screenshots and sequence capture, `Headless/Measurement` runs
headless measurement, and `Headless/OcclusionValidation` runs an offscreen active-extent step for
unscored occlusion recovery evidence. Operator procedures for capture, frame dumps and measurement
live in
[gpu-debugging.md](../guides/gpu-debugging.md#capture-and-inspect-a-frame) and
[gpu-debugging.md#measure-visibility-and-submission](../guides/gpu-debugging.md#measure-visibility-and-submission);
frozen comparison evidence is covered in
[screenshot-comparison.md](../guides/screenshot-comparison.md).

## Fonts and UI scale

`EditorFont` loads the bundled Inter Regular font before the first frame, with fixed-width digits and
an embedded fallback; App stages the pinned font and its license beside its executable during the
build. The shell applies the persisted UI zoom before `ImGui::NewFrame`, deriving font and control
sizes from an unscaled base style. The optional `UiScalePercent` preference defaults to 100%; the
schema-2 migration described above keeps a valid value. Top-bar controls and Layout presets cover
75–150%, and Cmd zoom shortcuts account for ImGui's macOS modifier
mapping while excluding text editing, active widgets, popups and camera look. Shared panel
measurements scale with the preference, and Render Graph cards remeasure once when it changes,
preserving selected identity, frozen data and the canvas's own separate navigation state. See
[gpu-debugging.md#editor-ui-scale](../guides/gpu-debugging.md#editor-ui-scale) for the operator
control surface.

## Tests

- `Tests/App/Model/Capture/`: capture metadata, editor action intents and the light-check capture
  path.
- `Tests/App/Model/Console/`: Console's bounded storage, filtering and freeze semantics.
- `Tests/App/Model/Graph/`: the frame-record ring, graph layout and node model, graph snapshot
  publication, and the Render Graph inspector model, including one GPU case.
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
