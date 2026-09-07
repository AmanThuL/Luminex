# Architecture Overview

**Status**: Implemented

Luminex is a Metal 4-first rendering playground organized as a one-way dependency stack. The RHI
is a repository-root component; the other runtime layers remain under `Source/`:

`Core → RHI → Render → Engine → App`

- **Core** owns logging, assertions, and dependency-free utilities.
- **RHI** is built from `RHI/xmake.lua`. Its self-contained core public headers live under
  `RHI/Include/RHI/`, split by owner concept — `GpuAddress.h`, `Format.h`, `Buffer.h`, `Texture.h`,
  `Heap.h`, `Sampler.h`, `ShaderLibrary.h`, `GraphicsPipeline.h`, `ComputePipeline.h`, `Indirect.h`,
  `RenderPass.h`, `CommandList.h`, `Swapchain.h`, and `Device.h`, plus the focused `Result.h`,
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
  which is what motion-vector and reactive-weight targets need.
- **RHI/Backends/Metal4** implements the current backend with private metal-cpp headers, three
  frames in flight, argument tables, a per-frame-slot growable frame-data page arena (256 KiB
  normal pages backing `bindFrameData`, oversize requests rounded up to that page quantum, pages
  retained mapped and resident until device destruction so a slot's high water becomes its reused
  capacity rather than being released), residency, shared-event pacing, render, compute, and copy
  pass encoders, indirect draws and dispatches, untracked placement heaps with resources created at
  explicit offsets, per-pass GPU timing, and capture support. The optional `RHIMetal4ImGui` target
  owns the adapter, its ImGui-dependent public extension header, and the dependency on Dear ImGui;
  the core RHI does not inherit any of them.
- **Render** owns camera, mesh, the validating render graph (`RenderGraph`), the shadow/scene/sky/
  display passes it declares, and the plain per-frame `SceneView` it consumes. The graph is
  declared fresh every frame and validates its declarations before any of them reach the GPU. It
  declares raster, compute, and copy passes with per-subresource uses over resources it either
  imports from a caller or creates as one-frame transients, culls every pass no declared sink
  reaches, places lifetime-disjoint transients in the shared bytes of a `TransientPool` placement
  heap, and answers with a `CompiledFrameRecord` describing the frame it encoded — schedule,
  barriers, transient lifetimes and assignments, and memory totals; `GraphDump.h` renders that
  record as deterministic text. Render also owns camera temporal history and the GPU-resident
  motion/history contract (`Temporal.h`, `TemporalHistory.h`, `Shaders/Motion.slang`): the previous
  `CameraFrameState`, the Halton jitter sequence, the derived `HistoryResetReason`, and the
  `Renderer`-created `lmx.render.motion`/`lmx.render.reactive` textures the temporal passes declare
  when `SceneView::temporal.enabled` is set. The `TemporalResolve` reconstruction stage (ADR 0014)
  owns two ping-ponged colour/depth slot pairs (`lmx.render.historyColor0/1`,
  `lmx.render.sceneDepth0/1`) and the pipelines that reproject, reject, clip and blend a native
  `NativeTaa` frame or commit a raw copy under `Raw`; every temporal frame's colour slot holds that
  frame's output in either mode, which is what makes a mode switch not a reset. Colour imports
  record their final consumer's access: `ShaderRead` after NativeTaa or Raw's HistoryAge view,
  otherwise `CopyDestination` for a Raw commit; the previous slot changes only when read
  ([ADR 0015](../decisions/0015-temporal-slot-terminal-access.md)). These targets are
  allocated with the scene targets and recreated by `resize()` alongside them, so the allocation is
  permanent rather than made on
  first enable.
- **Engine** owns scenes, procedural geometry, color conversion, DDS/glTF/Radiance HDR decoding,
  deterministic equirectangular environment conversion and image-based-lighting generation
  (`HdrEnvironment.h`, `Ibl.h`), including filtered cubemap sampling and a higher-resolution
  MaterialLab studio reflection source with a separate bounded diffuse source, and deterministic
  offline texture mip baking (`TextureBake.h`).
  It also owns object identity and previous transforms (`SceneObject::previousModel`/`motionClass`,
  `Scene::resetMotion`/`commitFrame`) and rigid animation (`SceneAnimation`, glTF-baked
  `RigidTrack`s, the shared `SceneEnvironment.h` sky/light rig, and the `temporal-lab`/`milk-truck`
  catalog entries).
- **App** owns SDL3, the editor shell, and the frame loop. `Source/App/Panels/` holds the five
  panel drawing functions (Scene, Viewport, Inspector, Performance, Render Graph); `EditorShell`
  coordinates them and the process-global ImGui context. Scene, Viewport, Inspector, and
  Performance dock together as in M5.3; Render Graph is submitted with its own `ImGuiWindowClass`
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
  compile into the Tests target alongside the rest of App's plain logic. The Render Graph panel
  draws a `GraphLayout` on a vendored `ImGuiNodeEditor` canvas (ADR 0011) with compact pins (full
  label on hover or selection), a selection-scoped details pane, and a `columns` control; dragged
  node positions are session state, and a changed layout signature (shape, expanded-group set, or
  column count) reapplies the deterministic positions. A registered ImGui settings handler persists
  the workspace schema and panel visibility as Luminex's own section of `imgui.ini`, alongside Dear
  ImGui's own docking and viewport data. Menu drawing and keyboard shortcuts only raise action
  intents; the frame loop consumes quit and capture at the boundary that already owns each
  operation, calls `ImGui::UpdatePlatformWindows()`/`RenderPlatformWindowsDefault()` after each
  presented frame so the vendored Metal 4 ImGui backend renders any detached window with its own
  command buffer and per-slot event, and the shell consumes a layout-reset intent at the start of
  the next frame. `EditorRenderSettings` carries the temporal toggles (enable, jitter, debug view,
  animation play, camera-track follow); the pure `TemporalEditorState` tracks the scene-generation
  counter and camera-cut latch, and the frame loop calls `advanceFrameAnimation()`/`commitFrame()`
  around `declarePasses` so a declared frame — and only a declared frame — advances Engine's
  animation clock and Render's history.

Shaders are authored in Slang and compiled to readable MSL, then to a metallib when the offline Metal
toolchain is present. The runtime MSL path remains a supported fallback. The live frame sequence and
resource transitions are documented in `docs/frame-pipeline.md`.

The root component is a physical and build boundary, not yet a separately published library: it
still participates in this repository's Core contracts and validation. The RHI grows only when a
rendering feature supplies a real portability requirement. Metal is the first implementation, not
the public vocabulary: accepted contracts do not leak native handles upward. ADR 0010 selected an
address-first per-frame data path while retaining the object-shaped resource, pass, pipeline,
residency, and barrier model and the render graph's logical ownership; M5.2 shipped that path —
`bindFrameData` over per-slot growable page arenas is the current runtime's per-frame data-delivery
contract. D3D12 is the intended second production
backend; Vulkan remains research evidence rather than a planned target.
