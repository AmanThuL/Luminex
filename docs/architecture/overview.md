# Architecture Overview

**Status**: Implemented

Luminex is a Metal 4-first rendering playground organized as a one-way dependency stack. The RHI
is a repository-root component; the other runtime layers remain under `Source/`:

`Core → Asset` and `Core → RHI → Render`, joined by `Scene → AppModel → App`. Asset uses only
RHI format/descriptor headers and links no GPU target. Core owns shared colour transfer and contract-preserving primitives;
`Render/SceneView.h` holds the borrowed frame input independently of the renderer.

- **Core** owns logging, assertions, two alignment contracts, shared colour transfer, whole-file reads,
  JSON escaping, complete numeric parsing and dispatch division; spdlog and glm are
  public packages.
- **RHI** is built from `RHI/xmake.lua`. Its self-contained core public headers live under
  `RHI/Include/RHI/`, split by owner concept — `GpuAddress.h`, `Format.h`, `Buffer.h`, `Texture.h`,
  `Heap.h`, `Sampler.h`, `ShaderLibrary.h`, `GraphicsPipeline.h`, `ComputePipeline.h`, `Indirect.h`,
  `RenderPass.h`, `CommandList.h`, `TemporalScaler.h`, `Swapchain.h`, and `Device.h`, plus the focused `Result.h`,
  `Validate.h`, and `CaptureSchema.h` — behind an includes-only `RHI.h` umbrella that declares no
  parallel surface. Every leaf compiles alone and the set exposes API-neutral resource, pipeline,
  command, synchronization, capture, and domain-owned error contracts without Metal or ImGui
  dependencies. Per-frame CPU-to-GPU data delivery is one typed operation,
  `CommandList::bindFrameData(slot, value)`: it allocates, copies, and binds the caller's block in
  one call and returns a `GpuAddress` — a standard-layout, arithmetic-free value naming the block's
  GPU location for the open frame. Data that survives the frame stays on the unchanged `bindBuffer`
  path instead of being copied through the frame-data arena. `RenderPassDesc` and
  `GraphicsPipelineDesc` support up to `kMaxExtraColorTargets` (3) additional colour attachments
  beyond the primary (`ExtraColorTarget`/`extraColorFormats`), validated for colour-renderable
  formats and matching extent; `RG16Float` and `R8Unorm` are colour-renderable and CPU-readable,
  which is what motion-vector and reactive-weight targets need. `RenderPassDesc` also carries an
  origin-anchored `renderAreaWidth`/`Height` (default 0/0, the whole attachment), validated against
  every attachment and encoded as an explicit Metal 4 viewport/scissor when non-zero.
  `DeviceCapabilities::temporalScaler` reports an optional algorithm and its input/output scale
  interval; `TemporalScaler` owns private reconstruction history, and the between-pass
  `CommandList::temporalScale` consumes neutral frame parameters. `R16Float` is sampled and
  storage-writable for the exposure texel; no MetalFX types enter public headers.
- **RHI/Backends/Metal4** implements the current backend with private metal-cpp headers, three
  frames in flight, argument tables, a per-frame-slot growable frame-data page arena (256 KiB
  normal pages backing `bindFrameData`, oversize requests rounded up to that page quantum, pages
  retained mapped and resident until device destruction so a slot's high water becomes its reused
  capacity rather than being released), residency, shared-event pacing, render, compute, and copy
  pass encoders, indirect draws and dispatches, untracked placement heaps with resources created at
  explicit offsets, per-pass GPU timing, and capture support. Its MetalFX temporal scaler translates
  reciprocal scale units, uses a public fence to hand work across opaque encoders, and retains
  state in every encoded frame slot until retirement. CPU-readable outputs use a creation-time
  private scratch and a copy inside the same timed call. The optional `RHIMetal4ImGui` target
  owns the adapter (sources under `RHI/Backends/Metal4/ImGui/Source/`), its ImGui-dependent public
  extension header, and the dependency on Dear ImGui; the core RHI does not inherit any of them.
- **Render** owns camera, mesh, the validating render graph (`RenderGraph`), the shadow/scene/sky/
  display passes it declares, and the plain per-frame `SceneView` it consumes. The graph is
  declared fresh every frame and validates its declarations before any of them reach the GPU. It
  declares raster, compute, copy and external passes with per-subresource uses over resources it either
  imports from a caller or creates as one-frame transients, culls every pass no declared sink
  reaches, places lifetime-disjoint transients in the shared bytes of a `TransientPool` placement
  heap, and answers with a `CompiledFrameRecord` describing the frame it encoded — schedule,
  barriers, transient lifetimes and assignments, and memory totals; `GraphDump.h` renders that
  record as deterministic text. `CompiledFrameRecord.h` owns this value-only observer contract,
  independently of the builder. Declaration/execution, compile/lifetime assignment, transitions and
  validation have separate implementation units with private shared range helpers. `Renderer`
  composes `ShadowStage` and `SceneStage`, which own
  opaque/masked pipelines, per-object bindings and draw encoding; SceneStage draws sky last in
  the same scene pass. Private `ExposureStage`, `BloomStage` and `DisplayStage` owners hold
  their pipelines/resources and declare their passes; Renderer keeps frame ordering and targets.
  `Render/FrameDeclaration` shares graph construction and execution across
  application loops and returns the accepted record for App-side retention. Render also owns
  camera temporal history and the GPU-resident motion/history contract (`Temporal.h`, `TemporalHistory.h`, `Shaders/Modules/Motion.slang`): the previous
  `CameraFrameState`, the Halton jitter sequence, the derived `HistoryResetReason`, and the
  `Renderer`-created `lmx.render.motion`/`lmx.render.reactive` textures the temporal passes declare
  when `SceneView::temporal.enabled` is set. The `TemporalResolve` reconstruction stage (ADR 0014)
  keeps native, upscale, vendor and diagnostic declaration units behind one history owner. It
  owns two ping-ponged colour/depth slot pairs (`lmx.render.historyColor0/1`,
  `lmx.render.sceneDepth0/1`) and the pipelines that reproject, reject, clip and blend a native
  `NativeTaa` frame or commit a raw copy under `Raw`; every temporal frame's colour slot holds that
  frame's output in every mode, which is what makes a mode switch not a reset. Colour imports
  record their final consumer's access: `ShaderRead` after NativeTaa or Raw's HistoryAge view,
  otherwise `CopyDestination` for a Raw commit; the previous slot changes only when read
  ([ADR 0015](../decisions/0015-temporal-slot-terminal-access.md)). These targets are
  allocated with the scene targets and recreated by `resize()` alongside them, so the allocation is
  permanent rather than made on first enable. Since M6.3 (ADR 0016), every one of these targets
  allocates at the *output* extent regardless of `SceneView::temporal.renderScale`; `Temporal.h`'s
  `renderExtentsForScale`/`jitterTexelOffset`/`renderSamplePosition` derive the active *render*
  rectangle a scale below 1.0 confines the scene pass and reconstruction to, and `TemporalHistory`'s
  `ExtentChanged` now fires on an output-extent change alone (superseding ADR 0013's clause), so a
  render-scale change alone derives `None` and reuses history. `TemporalResolve` selects between the
  native `NativeTaa` kernel (`Shaders/TemporalResolve.slang`, unchanged since M6.2) and the upscale
  kernel (`Shaders/TemporalUpscale.slang`) by whether render equals output extent and history was
  not just accumulated at another one; `Raw` gets the matching split against
  `Shaders/SpatialUpscale.slang`. Shared reason codes, constants and colour-space helpers live in
  `Shaders/Modules/TemporalCommon.slang`, imported by both. `Source/Render/ResolutionController` is a pure
  class with no device, graph or App dependency that proposes the next render scale from a retired
  frame's summed GPU pass time against a budget, with hysteresis.
  `VendorTemporal` selects a composed `VendorTemporalScaler` inside the existing resolve stage
  ([ADR 0017](../decisions/0017-vendor-reconstruction-capability.md)). It lazily creates the device
  scaler and replaces only the reconstruction kernel: a packing pass translates invalid motion,
  reactive weight and reciprocal applied exposure, then an external pass writes the current colour
  slot. The graph records `ExternalRead`/`ExternalWrite`, opens no scope around the external
  callback, and retains the usual dependency, culling and transient rules. Output resize recreates
  the scaler; scale changes only move the content rectangle. Vendor entry or engine resets discard
  the vendor's private history without resetting engine history on a mode switch. Unsupported or
  failed creation falls back to Native TAA with an explicit status reason. Engine reprojection
  diagnostics stay available; `VendorTemporalHistory.slang` supplies corrected reprojected history
  only for that selected view, while rejection, blend-weight and per-pixel age remain native-only.
  Vendor colour imports retain conservative `ExternalWrite`; current depth and scene colour record
  `ExternalRead`. Native terminal-use rows remain unchanged.
  `AlphaMode::Mask` selects dedicated `ScenePassMask`/`ScenePassAutoMask` and `ShadowPassMask`
  pipelines ([ADR 0018](../decisions/0018-masked-material-coverage.md)). Shared `AlphaMask.slang`
  discards when base-color texture alpha times factor alpha is below the material cutoff; color,
  depth, motion and reactive coverage share one scene invocation. Masked shadows use the same UV
  transform and cutoff. One- or two-sided variants support foliage and reverse back-face shading
  normals; the cutoff has its own frame-data block. Opaque shaders and uniform layouts stay separate.
- **Asset** owns procedural geometry, DDS/glTF/Radiance HDR and PNG/BMP image handling,
  deterministic equirectangular environment conversion and image-based-lighting generation
  (`HdrEnvironment.h`, `Ibl.h`), including filtered cubemap sampling and a higher-resolution
  MaterialLab studio reflection source with a separate bounded diffuse source, and deterministic
  offline texture mip baking (`TextureBake.h`), clip data and sampling, shared transform
  decomposition, repository discovery and the asset error domain. The glTF loader carries its own
  MASK cutoff/double-sided vocabulary and rejects referenced BLEND materials.
- **Scene** owns GPU texture and IBL uploads, the scene catalog, initial camera mapping,
  source-derived object names, optional local geometry bounds, and previous transforms
  (`SceneObject::previousModel`/`motionClass`,
  `Scene::resetMotion`/`commitFrame`), playback of Asset's rigid tracks, camera-track following,
  the shared `SceneEnvironment.h` sky/light rig and `temporal-lab`/`milk-truck` catalog entries. The six-scene catalog also includes optional `san-miguel`, imported at authored
  metre scale with a deterministic 12-second camera rail. `xmake setup --san-miguel` fetches its
  pinned official archive, converts the realtime OBJ with diffuse alpha and `N_` tangent normals,
  preserves both upstream metadata and bundled license in provenance, and bakes referenced images.
- **AppModel** is the static library under `Source/App/Model`, linked by App and Tests. It owns
  options, capture metadata, selection, workspace schema, actions, performance/graph models,
  timing history, frame-record retention, dynamic-resolution policy, temporal/exposure state and
  bounded Console storage/presentation.
  The module checker keeps it free of ImGui, SDL, Metal and the graph builder. Tests compiles
  its own C++ sources only. The shared scene session borrows library-owned scenes, owns its camera,
  prepares playback and borrowed views, and resets/commits motion. It captures authored transform
  and light defaults once per scene on first activation; returning to a cached scene never replaces
  those defaults with edited values. An animated object's default samples only that object's rigid
  track at the current playback time. Editing/resetting one transform collapses only its previous
  transform; the editor separately raises its temporal discontinuity latch. Activation resets
  editor motion but preserves headless loader state, matching each path's first-frame contract.
  Graph models and
  FrameRecordRing include the compiled record rather than the builder. Each application loop uses
  Render's shared frame declaration, retains its accepted record, appends its own output sink and
  owns scheduling and GPU waits.
  Declaration-time counts, logical image size, render/output extents and context epoch travel with
  each retained frame. Performance joins those values with that frame's retired timing and
  publishes/freezes them as one snapshot. Its 60-frame timing window refreshes at 4 Hz; the interval
  plot measures wall-clock intervals and labels the 60 Hz reference. Sorting preserves pass
  identity and schedule order remains available. `GraphSnapshot` uses the same shared 0.25-second
  publication interval but owns one complete retained record and its exact matched timing set,
  never averaged timings. Labels, details and dumps read that publication. First data and Resume
  publish immediately; later changes, including topology, wait for the next boundary. Freeze owns
  the displayed publication through ring eviction and scene changes. `ConsoleLog` serializes
  ingestion/snapshot/clear, limiting storage to 2,000 messages and 2 MiB of payload with a 16 KiB
  per-message cap. `ConsoleModel` owns independent filtered/frozen display and loss counters.
- **App** owns SDL3, the editor shell, and the frame loops. `Source/App/Panels/` holds the six
  panel drawing functions (Hierarchy, Viewport, Inspector, Performance, Console, Render Graph);
  `EditorShell` coordinates them and the process-global ImGui context. Hierarchy, Viewport,
  Inspector, Performance and Console dock together, with Console beside Performance; Render Graph
  is submitted with its own `ImGuiWindowClass`
  (docking with unclassed windows disallowed, auto-merge overridden off) under Dear ImGui platform
  viewports (`ImGuiConfigFlags_ViewportsEnable`), so it always owns a separate OS window and the
  dock builder never places it. Selection (`EditorSelection.h`), panel visibility and the workspace
  persistence schema (`WorkspaceModel.h`), menu- and shortcut-raised action intents
  (`EditorActions.h`), the Performance panel's coherent snapshot (`PerformanceModel.h`), the Render
  Graph panel's node shaping (`GraphNodeModel.h`, deriving nodes, edges, a culled band, and alias
  links from a `CompiledFrameRecord`), and its stage grouping and placement (`GraphLayout.h`,
  collapsing a shared-label-prefix set of at least two same-culled-status passes into one group
  node, deduplicating the edges and pins that cross a collapsed boundary, and wrapping long chains
  into rows under a caller-chosen column count) are ImGui/SDL/Metal-backend-free models that
  belong to AppModel alongside the rest of App's plain logic. The Render Graph panel coordinates its detached window, with separate canvas ownership,
  selection details and dump implementation units. Its canvas draws a `GraphLayout` on a vendored `ImGuiNodeEditor` canvas (ADR 0011) with compact pins (full
  label on hover or selection), a selection-scoped details pane, and a `columns` control; dragged
  node positions are session state. Stable canvas identity excludes alternating physical temporal
  resource instances while details retain the displayed frame's exact physical names and ranges.
  Unchanged topology preserves selection, groups and pan/zoom. Real topology changes still update
  the model and explicitly invalidate disappeared selections. Fit graph, Fit selection, 100% and
  Reset layout remain explicit actions; narrow graph windows stack canvas and details. A registered
  ImGui settings handler persists
  the workspace schema and panel visibility as Luminex's own section of `imgui.ini`, alongside Dear
  ImGui's own docking and viewport data. Schema 2 remains current: Console visibility is additive,
  absent keys default visible, and its first-use tab joins Performance without rebuilding existing
  dock nodes. Hierarchy keeps the original `Scene` window ID for saved layouts. Menu drawing and keyboard shortcuts only raise action
  intents; the frame loop consumes quit and capture at the boundary that already owns each
  operation, calls `ImGui::UpdatePlatformWindows()`/`RenderPlatformWindowsDefault()` after each
  presented frame so the vendored Metal 4 ImGui backend renders any detached window with its own
  command buffer and per-slot event, and the shell consumes a layout-reset intent at the start of
  the next frame. `EditorRenderSettings` carries the temporal toggles (enable, jitter, debug view,
  animation play, camera-track follow). `TemporalEditorState` tracks scene generation and the
  camera-cut latch, retains the last non-None reset reason with its original declared-frame count,
  and observes compatible live retired timing independently of metric freeze. It separates current
  request, declared execution and device availability: temporal off is Off/N/A at scale 1; vendor
  fallback retains the request and reason. Waiting differs from a measured zero. Renderer’s
  per-frame `TemporalStatus::lastReset` remains unchanged. The frame loop advances and commits
  animation through the shared session around frame declaration. `DynamicResolution.h` drives the
  shell-owned controller only while temporal and dynamic resolution are active; its publication
  cursor and last actual measurement have separate frame IDs, so inactive status never pairs an
  idle frame with an older measurement. Native TAA remains the default; device reconstruction
  explains native-only diagnostic availability.
  `EditorStyle.h` shares responsive fields and delayed contextual tooltips; Inspector keeps a selected-subject heading above its
  scrolling fields. Exposure/Bloom/Shadows start collapsed, Reconstruction/Resolution expanded;
  each editable group has scoped Reset and changed-from-default state. File > Open Scene owns
  catalog availability, loading and retry. Hierarchy uses compact search, collapsible subject
  groups and keyboard navigation; source names use scene-local disambiguation. A filtered-out
  selection remains explicit and can clear its filter in Inspector. Viewport owns
  camera help, scene playback/step/reset, camera-rail following and Frame selected. `SelectionBounds`
  frames reliable world bounds. `Render/SelectionOutline` supplies an editor-only utility that
  App opts into after scene display: full-resolution unjittered selected-only coverage/depth and scene visibility
  preserve the true silhouette, respecting masked alpha. A soft 1.5-logical-point border is
  depth-tested at source and destination before compositing into its own SDR target, so foreground
  occlusion cuts do not become edges. `lmx.pass.selection.coverage`, `lmx.pass.selection.visibility` and
  `lmx.pass.selection.outline` appear in graph costs. Ordinary Renderer/offscreen paths never
  declare these passes; scene targets, temporal history and scene-only captures remain unchanged. `DiagnosticLegend` describes shader encodings and Raw-mode placeholders beside the
  active mode and Return to Final. `EditorActions` retains capture capability and results;
  `ActionFeedback` shows recovery reasons and Copy path/Reveal without executing capture in panels.
  `ConsoleLogSink` subscribes after logger setup and before option parsing/device initialization;
  its RAII subscription lasts through application shutdown. Callbacks only append to the thread-safe store. Console displays UTC
  timestamps, six severity levels, minimum-severity and case-insensitive message filters. Freeze
  latches display/counters while ingestion continues, Resume refreshes, Clear empties history and
  counters while preserving filters/freeze, and Copy visible exports exactly matching displayed
  messages. It is read-only; follow-newest applies only when already at the end.
  `--capture-sequence <directory> --frames N --warmup W` writes N numbered PNGs (or `--capture-format bmp`) after W unsaved
  frames at 60 Hz into a new or empty directory, with actual camera, settings and temporal status.
  Vendor fallback fails a sequence. `Render/DisplayDomain.h` owns the opaque 8-bit SDR
  BT.709/sRGB/PBR Neutral output contract; Renderer exposes it to the Inspector and capture
  metadata. Asset `PngImage` writes deterministic colour-tagged PNGs; manifest v2 records the
  display domain, container and UI absence. The offline [comparison workflow](../guides/temporal-comparison.md)
  synchronizes Raw/Native/MetalFX reports and optional CPU LDR-FLIP on final sRGB images; Native TAA
  is the comparison baseline, not ground truth. Neither FLIP nor its Python dependencies enter App.

Shaders are authored in Slang and compiled to readable MSL, then to a metallib when the offline Metal
toolchain is present. Shared modules live in `Shaders/Modules/`, test oracles in `Shaders/Tests/`;
entry points and modules import only modules, enforced by policy. Runtime basenames stay unchanged.
Root xmake includes unit-local targets and `xmake/` setup/rules/tasks. The runtime MSL fallback
and live frame/resource sequence are documented in `docs/frame-pipeline.md`.

The root component is a physical and build boundary, not yet a separately published library: it
still participates in this repository's Core contracts and validation. The RHI grows only when a
rendering feature supplies a real portability requirement. Metal is the first implementation, not
the public vocabulary: accepted contracts do not leak native handles upward. ADR 0010 selected an
address-first per-frame data path while retaining the object-shaped resource, pass, pipeline,
residency, and barrier model and the render graph's logical ownership; M5.2 shipped that path —
`bindFrameData` over per-slot growable page arenas is the current runtime's per-frame data-delivery
contract. D3D12 is the intended second production
backend; Vulkan remains research evidence rather than a planned target.

`EditorFont` loads bundled Inter Regular before the first frame, with fixed-width digits and an
embedded fallback. App stages the pinned font/license beside its executable during build.
The shell applies persisted UI zoom before ImGui NewFrame, deriving font/control sizes from an
unscaled base style. Schema 2's optional `UiScalePercent` defaults to 100% without changing dock
restoration. Top-bar controls and Layout presets cover 75–150%; Cmd zoom shortcuts account for
ImGui's macOS modifier mapping and exclude text editing, active widgets, popups and camera look.
Shared panel measurements scale with the preference. Graph cards remeasure once when it changes,
preserving selected identity, frozen data and the canvas's separate navigation state.
