# Architecture Overview

**Status**: Implemented

Luminex is a Metal 4-first rendering playground organized as a one-way dependency stack. The RHI is a repository-root component; the other runtime layers remain under `Source/`:

`Core → Asset`, `Core → Render` and, independently, `RHI → Render`, joined by `Scene → AppModel → App`.
The RHI has no Core dependency; Asset uses only its format/descriptor headers and links no GPU
target. Core owns shared colour transfer and contract-preserving primitives; `Render/SceneView.h`
holds the borrowed frame input independently of the renderer.

- **Core** owns logging, assertions, two alignment contracts, shared colour transfer, whole-file reads,
  JSON escaping, complete numeric parsing and dispatch division; spdlog and glm are public packages.
- **RHI** builds and tests from its own root (`xmake -P RHI`): `RHI/xmake.lua` includes `RHI/xmake/targets.lua`, `setup.lua` and `shaders.lua`; Luminex's root includes `targets.lua` alone. It has no Core
  dependency — a private `RHI/Source/Base` supplies assert/log/align/JSON, and the one public addition
  is `RHI/Include/RHI/Message.h`'s severity/text callback (unset: stderr), which `Render/RhiLog`
  forwards into spdlog/Console for App and Luminex's `Tests`. `RHI/Tests`/`RHI/Shaders/Tests` hold its
  own contract/GPU suite (`RHITests`, linking only `RHI`); `RHI/Tools` holds its header check, ImGui
  patch and buffer probe. Its self-contained core public headers live under
  `RHI/Include/RHI/`, split by owner concept — `GpuAddress.h`, `Format.h`, `Buffer.h`, `Texture.h`,
  `Heap.h`, `Sampler.h`, `ShaderLibrary.h`, `GraphicsPipeline.h`, `ComputePipeline.h`, `Indirect.h`,
  `RenderPass.h`, `CommandList.h`, `TemporalScaler.h`, `Swapchain.h`, and `Device.h`, plus the focused `Result.h`,
  `Validate.h`, and `CaptureSchema.h` — behind an includes-only `RHI.h` umbrella that declares no
  parallel surface. Every leaf compiles alone and the set exposes API-neutral resource, pipeline,
  command, synchronization, capture, and domain-owned error contracts without Metal or ImGui dependencies. Transient CPU-to-GPU parameter delivery is one typed operation,
  `CommandList::bindFrameData(slot, value)`: it allocates, copies, and binds the caller's block in
  one call and returns a `GpuAddress` — a standard-layout, arithmetic-free value naming the block's
  GPU location for the open frame. Data that survives the frame stays on the unchanged `bindBuffer`
  path instead of being copied through the frame-data arena. `BufferDesc::cpuWrite` enables
  checked `Buffer::write(offset, data, size)` uploads into host-visible memory. Writes require
  non-null data and a nonempty in-bounds range; the caller proves all GPU readers/writers of that
  range retired. Metal4 copies into Shared storage; device-private placed buffers reject this flag.
  Paced scene tables are the first consumer. `RenderPassDesc` and `GraphicsPipelineDesc` support up to `kMaxExtraColorTargets` (3) additional colour attachments
  beyond the primary (`ExtraColorTarget`/`extraColorFormats`), validated for colour-renderable
  formats and matching extent; `RG16Float` and `R8Unorm` are colour-renderable and CPU-readable,
  which is what motion-vector and reactive-weight targets need. `RenderPassDesc` also carries an
  origin-anchored `renderAreaWidth`/`Height` (default 0/0, the whole attachment), validated against
  every attachment and encoded as an explicit Metal 4 viewport/scissor when non-zero.
  `DeviceCapabilities::temporalScaler` reports an optional algorithm and its input/output scale
  interval; `TemporalScaler` owns private reconstruction history, and the between-pass
  `CommandList::temporalScale` consumes neutral frame parameters. `R16Float` is sampled and storage-writable for the exposure texel; no MetalFX types enter public headers.
- **RHI/Backends/Metal4** implements the current backend with private metal-cpp headers, three
  frames in flight, argument tables (texture slots cleared before each render/compute pass), a per-frame-slot growable frame-data page arena (256 KiB
  normal pages backing `bindFrameData`, oversize requests rounded up to that page quantum, pages
  retained mapped and resident until device destruction so a slot's high water becomes its reused
  capacity rather than being released), residency, shared-event pacing, render, compute, and copy
  pass encoders, indirect draws and dispatches, untracked placement heaps with resources created at
  explicit offsets, per-pass GPU timing, and capture support. Its MetalFX temporal scaler translates reciprocal scale units, uses a public fence to hand work across opaque encoders, and retains state in every encoded frame slot until retirement. CPU-readable outputs use a creation-time private scratch and a copy inside the same timed call. The optional `RHIMetal4ImGui` target
  owns the adapter (sources under `RHI/Backends/Metal4/ImGui/Source/`), its ImGui-dependent public
  extension header, and the dependency on Dear ImGui; the core RHI does not inherit any of them.
- **Render** owns camera, CPU geometry vocabulary (`Vertex`/`MeshData`), shared scene-table rows,
  the validating render graph (`RenderGraph`), the shadow/scene/sky/display passes it declares, and the plain per-frame `SceneView` it consumes. The graph is
  declared fresh every frame and validates its declarations before any of them reach the GPU. It declares raster, compute, copy and external passes with per-subresource uses over resources it either
  imports from a caller or creates as one-frame transients, culls every pass no declared sink
  reaches, places lifetime-disjoint transients in the shared bytes of a `TransientPool` placement
  heap, and answers with a `CompiledFrameRecord` describing the frame it encoded — schedule,
  barriers, transient lifetimes and assignments, and memory totals; `GraphDump.h` renders that
  record as deterministic text. `CompiledFrameRecord.h` owns this value-only observer contract, independently of the builder. Declaration/execution, compile/lifetime assignment, transitions and
  validation have separate implementation units with private shared range helpers. `Renderer`
  composes `ShadowStage` and `SceneStage`, which own opaque/masked pipelines and direct, indirect or instanced batched submission. `Bounds.h` owns finite AABBs and the eight-corner transform. `SceneTables.h` and its Slang module mirror the
  240-byte instance, 112-byte material, 48-byte mesh and 64-byte local-light rows, including bounds.
  `Visibility.h` classifies canonical uploaded CPU rows against five normalized planes from the
  jittered raster view-projection, with a 1e-3 world-unit guard and no far plane. Rejected rows keep
  their identity and motion; disabled, shadow-view, unreliable-bounds and nonfinite-transform bypasses remain inspectable. Shadow candidates are unculled.
  Renderer owns `DrawSubmission`: three paced, growable row/argument buffer pairs, retained on
  replacement until the last prepared frame + 3. Scene entries precede shadow entries. The default
  indirect mode issues one command per retained object; batched mode stably sorts pipeline,
  material and mesh keys and issues one instanced command per run. Direct remains selectable.
  The 16-byte b1 `DrawUniforms.firstEntry` and shader instance index select a b4 visible-list entry,
  then instance/material rows at b5/b6; vertices at b0 and the rebased index pool are shared per pass.
  Five read-only `lmx.scene.*` imports expose vertices, indices, meshes, instances and materials;
  `lmx.draw.rows` and `lmx.draw.args` declare list and indirect-argument reads. CPU-only production
  leaves both buffers without GPU writers. Opt-in `GpuVisibility` adds reset/classify/scan/emit,
  deterministic fixed slots and retired diagnostics. Opt-in `HzbStage` owns two output-capacity
  R32Float pyramids: half-resolution minima, one pass per mip, then a publication read of all mips.
  GPU `Occlusion` tests use the preceding declared frame's matrix, jitter and active extent;
  `OcclusionHistory` globally retains on invalid source/camera/coverage evidence or wireframe.
  `OcclusionReference` draws every candidate directly into private ID/depth targets and joins
  generational missing-frame streaks at retirement; [contracts](../guides/gpu-visibility.md).
  `LocalLightMode` selects Off, Direct (reference) or Clustered (default) in one shared punctual-light loop; the [default decision](../milestones/m7.5-validation.md#default-decision) follows passed lossless-list/scoped exact-image gates, independent of cost; owner acceptance is recorded separately. Shared punctual normal-footprint filtering broadens only its specular lobe; directional/IBL terms stay unchanged. Current diagnostics and limits are in the [follow-up](../milestones/m7.5-followup.md).
  Point/spot terms follow the directional sum before ambient/emissive and the single pre-exposure
  multiply. Local lights affect opaque/masked surfaces without shadows; `PassUniforms` stays 400 B.
  A sixth `lmx.scene.lights` import exists only with live lights. Clustered adds a public
  `LightClusterStage` with three paced slots and reset/count/scan/fill passes before scene shading.
  Its 16×9×24 pixel-aligned froxels use the jittered projection and uploaded reversed-Z slices;
  ordered fp32 CPU/GPU predicates, ascending row lists, a 128-light froxel cap and 65,536-index cap
  make truncation deterministic. `LightClusters` owns the CPU mirror; `LightClusterCheck` compares
  declaration-time mirror results with retired GPU lists/counters. `LightingStatus` retains frame,
  scene and requested/effective mode; zero-live frames use Off and declare no light-list/debug pass.
  Lazy `LightDebugStage` reads actual scene depth/lists after display and renders count, overflow or
  missed-light views to the display target from a dedicated SDR source transient; scene/history stay unchanged.
  [ADR 0023](../decisions/0023-local-light-and-cluster-contract.md) owns the accepted contract; milestone records retain historical image failures and follow-up evidence limits.
  SceneStage draws sky last from the shared geometry pool. Private `ExposureStage`, `BloomStage`
  and `DisplayStage` own their pipelines/resources; Renderer retains frame ordering and targets.
  `Render/FrameDeclaration` shares graph construction/execution across application loops and returns the accepted record for App-side retention. Render also owns
  camera temporal history and the GPU-resident motion/history contract (`Temporal.h`, `TemporalHistory.h`, `Shaders/Modules/Motion.slang`): the previous
  `CameraFrameState`, the Halton jitter sequence, the derived `HistoryResetReason`, and the
  `Renderer`-created `lmx.render.motion`/`lmx.render.reactive` textures the temporal passes declare
  when `SceneView::temporal.enabled` is set. The `TemporalResolve` reconstruction stage (ADR 0014)
  keeps native, upscale, vendor and diagnostic declaration units behind one history owner. It owns two ping-ponged colour/depth slot pairs (`lmx.render.historyColor0/1`,
  `lmx.render.sceneDepth0/1`) and the pipelines that reproject, reject, clip and blend a native
  `NativeTaa` frame or commit a raw copy under `Raw`; every temporal frame's colour slot holds that
  frame's output in every mode, which is what makes a mode switch not a reset. Colour imports
  record their final consumer's access: `ShaderRead` after NativeTaa or Raw's HistoryAge view,
  otherwise `CopyDestination` for a Raw commit; the previous slot changes only when read ([ADR 0015](../decisions/0015-temporal-slot-terminal-access.md)). These targets are
  allocated with the scene targets and recreated by `resize()` alongside them, so the allocation is
  permanent rather than made on first enable. Since M6.3 (ADR 0016), every one of these targets
  allocates at the *output* extent regardless of `SceneView::temporal.renderScale`; `Temporal.h`'s
  `renderExtentsForScale`/`jitterTexelOffset`/`renderSamplePosition` derive the active *render*
  rectangle a scale below 1.0 confines the scene pass and reconstruction to, and `TemporalHistory`'s
  `ExtentChanged` now fires on an output-extent change alone (superseding ADR 0013's clause), so a
  render-scale change alone derives `None` and reuses history. `TemporalResolve` selects between the
  native `NativeTaa` kernel (`Shaders/TemporalResolve.slang`, unchanged since M6.2) and the upscale
  kernel (`Shaders/TemporalUpscale.slang`) by whether render equals output extent and history was not just accumulated at another one; `Raw` gets the matching split against
  `Shaders/SpatialUpscale.slang`. Shared reason codes, constants and colour-space helpers live in
  `Shaders/Modules/TemporalCommon.slang`, imported by both. `Source/Render/ResolutionController` is a pure
  class with no device, graph or App dependency that proposes the next render scale from a retired
  frame's summed GPU pass time against a budget, with hysteresis. `VendorTemporal` selects a composed `VendorTemporalScaler` inside the existing resolve stage
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
  `ExternalRead`. Native terminal-use rows remain unchanged. `AlphaMode::Mask` selects dedicated `ScenePassMask`/`ScenePassAutoMask` and `ShadowPassMask`
  pipelines ([ADR 0018](../decisions/0018-masked-material-coverage.md)). Shared `AlphaMask.slang`
  discards when base-color texture alpha times factor alpha is below the material cutoff; color,
  depth, motion and reactive coverage share one scene invocation. Masked shadows use the same UV
  transform and cutoff. One- or two-sided variants support foliage and reverse back-face shading
  normals; cutoff and flags come from the shared material row. `AlphaMask.slang` is a pure
  coverage function; the separate alpha-mask frame-data block is retired. Pipeline variants remain.
- **Asset** owns procedural geometry, DDS/glTF/Radiance HDR and PNG/BMP image handling,
  deterministic equirectangular environment conversion and image-based-lighting generation
  (`HdrEnvironment.h`, `Ibl.h`), including filtered cubemap sampling and a higher-resolution
  MaterialLab studio reflection source with a separate bounded diffuse source, and deterministic
  offline texture mip baking (`TextureBake.h`), clip data and sampling, shared transform
  decomposition, repository discovery and the asset error domain. The glTF loader carries its own MASK cutoff/double-sided vocabulary and rejects referenced BLEND materials.
- **Scene** owns distinct generational `InstanceId`/`MeshId`/`MaterialId`/`TextureId`/`LightId` handles,
  the immutable shared vertex/index pool, paced instance/material/mesh buffers, texture and IBL uploads, the scene catalog, initial camera mapping,
  source-derived object names, mesh-local bounds computed by `addMesh`, and previous transforms (`SceneObject::previousModel`/`motionClass`,
  `Scene::resetMotion`/`commitFrame`), playback of Asset's rigid tracks, camera-track following,
  the shared `SceneEnvironment.h` sky/light rig and `temporal-lab`/`milk-truck` catalog entries. The eight-scene catalog also includes optional `san-miguel`, imported at authored
  metre scale with a deterministic 12-second camera rail. `xmake setup --san-miguel` fetches its pinned official archive, converts the realtime OBJ with diffuse alpha and `N_` tangent normals,
  preserves both upstream metadata and bundled license in provenance, and bakes referenced images.
  Always-available VisibilityLab adds a seeded cube/icosphere grid, four materials, five initial
  camera boundary probes and a 12-second rail. Its configurable total N includes the probes.
  LightLab adds a deterministic point/spot grid, a 12-second rail, position-only orbit tracks and
  an optional overflow pile; authored orbits clear material rows by 0.25 m. Sponza authors 16 static lights and a 120-second two-level corridor/atrium tour. Disabled lights retain IDs/rows/edits/orbits; localLights() includes them, while enabledLightCount() feeds rendering liveLightCount. Explicit CLI rig off disables the group without removing allocation.
  `addLight`/`updateLight`/`removeLight` use the sixth paced table; `localLights()` lists live IDs.
  Authored pre-finalize light indices bind orbit tracks; runtime lights remain static and edits do not advance occlusion coverage. All mutations precede `prepareFrame`.
  `addMesh`/`addTexture`/`addMaterial`/`addObject` build a scene, and `finalize` merges geometry
  including sky with rebased indices and allocates three table slots. Slot indices remain stable
  across draw-list removal/reorder; stale generations and foreign stores fail checked queries.
  `prepareFrame(frameNumber)` follows `Device::beginFrame`, updating only rows dirty for that retired slot and recomputing world bounds with the shared corner transform. It advances `coverageEpoch` for geometry/mask edits, excluding previous-pose and emissive-only changes. `Scene::meshBounds`
  also supplies selection framing; no per-object local bounds remain. Borrowed CPU instance rows
  match the prepared GPU slot. Changes mark all slots dirty; static scenes converge to zero writes.
  Growth doubles capacity and retains old buffers until `lastFrame + 3`; texture removal invalidates
  the handle immediately and defers allocation release the same way. Scene destruction requires retired GPU reads. New instances seed their own previous
  pose; `commitFrame` promotes transforms only when the caller accepts the rendered frame.
  `MaterialRecord` owns texture handles; views resolve per-draw pointers for existing fallbacks.
- **AppModel** is the static library under `Source/App/Model`, linked by App and Tests. It owns
  options, capture metadata, selection, workspace schema, actions, performance/graph models,
  timing history, frame-record retention, dynamic-resolution policy, temporal/exposure state and
  bounded Console storage/presentation, visibility formatting and the `MeasurementRun` state machine.
  The module checker keeps it free of ImGui, SDL, Metal and the graph builder. Tests compiles
  its own C++ sources only. The shared scene session borrows library-owned scenes, owns its camera,
  prepares playback and borrowed views, forwards paced table preparation, and resets/commits motion.
  `EditorPlayback` owns Stopped/Playing/Paused and captures camera/time plus animation-owned object poses/emissive strength and tracked light positions on first Play. Stop restores these and resets motion; the shell resets temporal/exposure. Activation starts Stopped after restoring the old run.
  This preview shares the scene; it does not restore rendering settings or unrelated edits. Authored transform and light defaults are captured once per scene on first activation; returning to a cached scene never replaces
  those defaults with edited values. An animated object's default samples only that object's rigid
  track at the current playback time. Editing/resetting one transform collapses only its previous
  transform; the editor separately raises its temporal discontinuity latch. Activation resets
  editor motion but preserves headless loader state, matching each path's first-frame contract.
  Graph models and FrameRecordRing include the compiled record rather than the builder. Each application loop uses
  Render's shared frame declaration, retains its accepted record, appends its own output sink and owns scheduling and GPU waits.
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
  `SceneTableDisplay` formats live scene counts/capacities, geometry bytes, writes, slot, growth
  events and pending release buffers for Inspector's read-only Scene tables topic. `SceneSession` captures local-light defaults by full `LightId`; reset samples current orbit
  position while restoring other authored fields. It retains actual LightLab pile IDs for bounded
  runtime Apply/Clear without touching grid lights or tracks. `LightingDisplay` publishes one retired counter/timing frame every 250 ms, with immediate overflow/check warnings.
  `VisibilityDisplay` retains full object identity and frame-scoped classifications for Inspector
  diagnostics and Hierarchy badges. `MeasurementRun` joins declared CPU samples to retired GPU
  timings by frame ID, validates the pass inventory and completes only after every sample retires.
  Editor and headless measurement serialize GPU retirement after each submitted frame because RHI
  publishes only the newest retired timing set. Reports disclose this pacing, separate beginFrame
  wait from encoding, and exclude the post-submit wait; they do not measure realtime throughput.
  Editor runs remain interactive/unscored. Toolbar Measure Play starts the fixed plan; Stop cancels, Pause is disabled, and completion/cancellation restores preview state while retaining results. Performance owns plan/results/export in a detached native window. Measure mode selection has no window side effect; Play opens/focuses Measure once, while closing the window leaves the run active. Completion/cancellation never reopens it; explicit Show measurement opens/focuses it anytime. Headless scored runs refuse validation/capture flags.
  The [debugging guide](../guides/gpu-debugging.md#measure-visibility-and-submission) owns commands.
- **App** owns SDL3, the editor shell, and the frame loops. `Source/App/Panels/` holds the six
  panel drawing functions (Hierarchy, Viewport, Inspector, Performance, Console, Render Graph);
  `EditorShell` coordinates them and the process-global ImGui context. Hierarchy, Viewport and
  Inspector dock together, with Console alone below. Performance and Render Graph each own a
  detached native window with an `ImGuiWindowClass` that disallows unclassed docking and overrides
  auto-merge off under Dear ImGui platform viewports (`ImGuiConfigFlags_ViewportsEnable`).
  The dock builder places neither; both start closed, and Window menu toggles remain explicit.
  Selection (`EditorSelection.h`), panel visibility and the workspace persistence schema (`WorkspaceModel.h`), menu- and shortcut-raised action intents
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
  Reset layout remain explicit actions; narrow graph windows stack canvas and details. A registered ImGui settings handler persists
  the workspace schema and panel visibility as Luminex's own section of `imgui.ini`, alongside Dear
  ImGui's own docking and viewport data. Schema 3 restores panel visibility and detached geometry;
  schema 2 migrates to default topology while preserving valid UI scale. Reset Default Layout
  closes Performance/Graph and resets Performance's next-open bounds. Hierarchy retains its `Scene` window ID. Menu drawing and keyboard shortcuts only raise action
  intents; the frame loop consumes quit and capture at the boundary that already owns each
  operation, calls `ImGui::UpdatePlatformWindows()`/`RenderPlatformWindowsDefault()` after each
  presented frame so the vendored Metal 4 ImGui backend renders any detached window with its own
  command buffer and per-slot event. The maintained backend patch quarantines uploaded vertex/index
  buffers in per-slot `usedBuffers` until slot revisit; platform events and App's main-frame pacing precede reuse/eviction, preventing same-frame window uploads from overwriting GPU reads.
  Explicit Performance focus resolves ImGui's SDL3 `PlatformHandle` as an `SDL_WindowID` and restores only minimized windows. The shell consumes a layout-reset intent at the start of
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
  idle frame with an older measurement. Native TAA remains the default; device reconstruction explains native-only diagnostic availability.
  `EditorStyle.h` shares responsive fields and delayed contextual tooltips; Inspector keeps a selected-subject heading above its
  scrolling fields. Rendering expands into category subjects; each page pairs controls with compact live readings, without nested detail toggles. Category selection participates in filtering and
  keyboard navigation; changing pages resets scroll. Editable reset groups keep scoped defaults. File > Open Scene owns
  catalog availability, loading and retry. Hierarchy uses compact search, collapsible subject
  groups and keyboard navigation; Local lights uses clipping and complete generational selection.
  Lighting owns mode/check/view and pile controls/reset; Hierarchy checkboxes preserve per-light identities and edits, including disabled lights. Per-light Inspector edits decode sRGB
  colour once into linear storage. Source names use scene-local disambiguation. A filtered-out
  selection remains explicit and can clear its filter in Inspector. Viewport owns camera help and Frame selected.
  The top Scene/Measure toolbar owns one state-switching Play/Pause button, separate Stop/Step and camera-rail follow options. `SelectionBounds`
  frames shared reliable world bounds; a rejected selection produces no outline. `Render/SelectionOutline` supplies an editor-only utility that
  App opts into after scene display: full-resolution unjittered selected-only coverage/depth and scene visibility
  preserve the true silhouette, reading the same instance/material tables and masked alpha. A soft 1.5-logical-point border is
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
  counters while preserving filters/freeze, and Copy visible exports exactly matching displayed messages. It is read-only; follow-newest applies only when already at the end.
  `--capture-sequence <directory> --frames N --warmup W` writes N numbered PNGs (or `--capture-format bmp`) after W unsaved
  frames at 60 Hz into a new or empty directory, with actual camera, settings and temporal status.
  Vendor fallback fails a sequence. `Render/DisplayDomain.h` owns the opaque 8-bit SDR BT.709/sRGB/PBR Neutral output contract; Renderer exposes it to the Inspector and capture
  metadata. Asset `PngImage` writes deterministic colour-tagged PNGs; manifest v2 records the
  display domain, container and UI absence. The offline [comparison workflow](../guides/temporal-comparison.md)
  synchronizes Raw/Native/MetalFX reports and optional CPU LDR-FLIP on final sRGB images; Native TAA
  is the comparison baseline, not ground truth. Neither FLIP nor its Python dependencies enter App.

Shaders are authored in Slang and compiled to readable MSL, then to a metallib when the offline Metal
toolchain is present. Shared modules live in `Shaders/Modules/`, test oracles in `Shaders/Tests/`;
entry points and modules import only modules, enforced by policy. Runtime basenames stay unchanged.
Root xmake includes unit-local targets and `xmake/` setup/rules/tasks. The runtime MSL fallback and live frame/resource sequence are documented in `docs/frame-pipeline.md`.

The root component is a physical and build boundary, not yet a separately published library: it has
no Core dependency but still builds, tests and passes policy inside this repository. The RHI grows
only when a rendering feature supplies a real portability requirement. Metal is the first implementation, not
the public vocabulary: accepted contracts do not leak native handles upward. ADR 0010 selected an
address-first per-frame data path while retaining the object-shaped resource, pass, pipeline,
residency, and barrier model and the render graph's logical ownership; M5.2 shipped that path —
`bindFrameData` over per-slot growable page arenas delivers transient parameters; persistent scene
tables use explicit paced `Buffer::write` uploads under the same retirement proof. D3D12 is the intended second production
backend; Vulkan remains research evidence rather than a planned target.

`EditorFont` loads bundled Inter Regular before the first frame, with fixed-width digits and an
embedded fallback. App stages the pinned font/license beside its executable during build.
The shell applies persisted UI zoom before ImGui NewFrame, deriving font/control sizes from an
unscaled base style. Optional `UiScalePercent` defaults to 100%; schema-2 migration preserves valid
values while rebuilding topology. Top-bar controls and Layout presets cover 75–150%; Cmd zoom shortcuts account for
ImGui's macOS modifier mapping and exclude text editing, active widgets, popups and camera look.
Shared panel measurements scale with the preference. Graph cards remeasure once when it changes,
preserving selected identity, frozen data and the canvas's separate navigation state.
