# Luminex

Modern rendering playground / portfolio piece. Metal 4-first (macOS 26+, Apple Silicon) with a
thin RHI and one implemented backend.
The long-term direction is a graph-scheduled, GPU-driven hybrid renderer sharing scene, material,
light and temporal semantics; visible rendering quality and reproducible engineering evidence both
serve the portfolio. Future scope and prerequisites live only in `docs/roadmap.md`.

## Golden sources
- Spec: `docs/specs/2026-08-07-luminex-upgrade-design.md` (decisions D1–D10 are binding)
- Current architecture: `docs/architecture/overview.md` · Frame walkthrough: `docs/frame-pipeline.md`
- GPU debugging: `docs/guides/gpu-debugging.md`
- ADRs: `docs/decisions/` · Conventions: `docs/conventions/` · Roadmap: `docs/roadmap.md`
- Roadmap entry: M6 has five temporal/display slices; M7 ends after four scene/visibility/lighting
  slices. Basic transparency belongs to M8, ordinary LOD to M9, and area lights to a
  separate extension. These are planned boundaries, not current renderer capabilities.
- Current baseline: `docs/milestones/m5.5.md` (Render Graph legibility and detached window) over
  `docs/milestones/m5.4.md` (Render Graph node view, ADR 0011) over
  `docs/milestones/m5.3.md` (editor workspace and selection) over `docs/milestones/m5.2.md`
  (frame-data path, ADR 0010) over `docs/milestones/m5.1.md` over `docs/milestones/m5.md`
- M5.6 closed as reliability failure / DEFER, with no accepted performance conclusion (ADR 0012).
  Evidence and experiment source are frozen at `m5.6-gpu-submission-evidence`; see
  `docs/milestones/m5.6.md`. The shipped rendering baseline remains M5.5; M6 is unblocked.

## Commands
- Setup (once): `brew install xmake`, `xmake setup` — fetches pinned ThirdParty deps (metal-cpp,
  slang, Dear ImGui docking-branch commit, imgui-node-editor), Damaged Helmet, the CC0 Studio
  Small 09 HDRI, and the official ~78 MB Crytek Sponza OBJ+PNG archive into gitignored
  `Assets/Fetched/`, with upstream provenance/license metadata.
  Setup deterministically converts Sponza to uncompressed core glTF, then bakes every base-color
  and normal image referenced by Sponza and Damaged Helmet into a deterministic offline mip chain
  (`Tools/TextureBake`, DDS + manifest) that scene loading prefers over its in-process fallback;
  pins and hashes are in `xmake.lua`. Setup re-applies the maintained ThirdParty patches
  idempotently: a tree already carrying the current patch is left alone, and one carrying an older
  revision of it is restored to the pin before the patch is applied. Optional:
  `xcodebuild -downloadComponent MetalToolchain` enables offline shader precompile (runtime-MSL
  fallback works without it).
- Editor setup (once, for clangd): `xmake project -k compile_commands` writes
  `compile_commands.json` (gitignored) — without it clangd reports spurious diagnostics.
- Build: `xmake` · Run: `xmake run App` · Tests: `xmake test` (CPU-only: `xmake test Tests/unit`)
- **Gotcha**: the Tests target has `set_default(false)` — a plain `xmake` does NOT relink the
  test binary after `Source/` changes. `xmake test` rebuilds it; when running the Tests binary
  directly, `xmake build Tests` first or risk a false pass against a stale binary.
- Frozen portability-checkpoint-A subset (ADR 0009): `xmake build Tests && cd
  build/macosx/arm64/release/test && MTL_DEBUG_LAYER=1 ./Tests "[checkpoint-a]"` — a future backend
  must pass this filter unchanged; the working directory must be the Tests build directory (shaders
  resolve relative to CWD).
- Format: `xmake format` (check: `xmake format --check`) · Policy: `xmake policy`
- **Gotcha**: `xmake policy` run from inside a nested git worktree silently validates the *outer*
  checkout, not the worktree — xmake resolves its project root to the outermost ancestor directory
  holding an `xmake.lua`. In a worktree, run the checkers directly from its root instead: `python3
  Tools/check_project_policy.py`; `python3 Tools/check_cpp_comments.py --public-api-docs error`
  (first regenerate that worktree's `compile_commands.json` with `xmake project -k
  compile_commands -P .` — a prerequisite the comment checker reads, not a checker itself);
  `python3 Tools/check_rhi_headers.py`; `python3 Tools/check_cpp_layout.py`.
- Frame-data benchmark: `xmake build FrameDataBench` then `python3
  Tools/Bench/frame_data_paired.py` for paired CPU-encoding measurements against a frozen baseline
  build; both the bench binary and the driver support `--selftest`.
- Scenes: `xmake run App` opens the editor maximized to the display's usable bounds, with Sponza
  selected by default (catalog selector in the Scene panel); `--windowed` keeps the fixed 1280×720
  default size instead. Offscreen: `xmake run App --screenshot <out.bmp>` or `--scene
  <sponza|damaged-helmet|milk-truck|material-lab|temporal-lab> --screenshot <out.bmp>`. `--frames N`
  (default 1) renders N frames before writing the last, advancing the scene's animation by 1/60 s
  and following its camera track (if any) between them; `--temporal` enables the temporal path for
  the capture and `--temporal-view off|motion|reprojection` selects a diagnostic overlay (and
  implies `--temporal`) — e.g. `xmake run App --scene temporal-lab --frames 4 --temporal-view
  motion --screenshot out.bmp`. Running the binary directly requires CWD = its build dir (shaders
  resolve relative to CWD). Sponza's first load decodes its referenced textures — expect several
  seconds in a debug build.
- Debug: Metal validation `MTL_DEBUG_LAYER=1 xmake run App`; GPU capture: press `c` in-app, or use
  Debug > Capture Next GPU Frame in the main menu (shown with its `C` shortcut) — both need
  `MTL_CAPTURE_ENABLED=1` — then open the .gputrace in Xcode. Automated runs: `LMX_MAX_FRAMES=N`
  exits after N frames; `LMX_CAPTURE_AT_FRAME=N` captures without a keypress. The Performance panel
  shows a pausable 60-frame rolling Pass/Average/Latest/Min–Max/Samples table per render-graph
  pass, refreshed four times per second, with Pause and Clear History. The Render Graph panel opens
  in its own real OS window (Dear ImGui platform viewports; a macOS title bar with close/minimise/
  zoom, and it never docks) and draws the exact newest-retired-frame's compiled record as a node
  canvas of Falcor-style cards — a title band in the pass kind's colour, inputs and outputs as dots
  centred on the card's edges, links coloured per resource — placed left to right from the cards'
  measured sizes and opened with the leading columns filling the view, pan and zoom for the rest. Stage groups (a shared label prefix, at least two members, scheduled and culled
  kept separate) are collapsed by default and opened by double-click or the details pane's Expand
  button; pins are compact and show their full label on hover or selection; the `columns` control
  defaults to 0, no wrap, and a positive value wraps long chains into rows. A details pane is
  scoped to the selected item, Reset Layout re-measures and re-places, and dragged node positions
  are session state only.
- GitHub-hosted macOS exposes a paravirtual GPU without Metal 4. Hosted CI compiles and inventories
  GPU cases; renderer/RHI/shader PRs still require `MTL_DEBUG_LAYER=1 xmake test Tests/gpu` on
  Metal 4 Apple Silicon before merge.
- Controls: fly camera — hold RMB in the Viewport panel + WASD (move) / QE (down/up) while held.
  The main menu (File/Window/Layout/Debug) exposes quit, per-panel visibility, Reset Default
  Layout, and GPU capture. Dock layout and Luminex's own versioned workspace metadata (schema
  version, per-panel visibility) persist together in `imgui.ini` next to the built binary (build
  dir, gitignored): a clean or pre-M5.5 ini rebuilds the default four-panel dock layout once, a
  matching schema restores it unchanged, and Reset Default Layout rebuilds it on demand without
  touching unrelated ini entries. Render Graph is never part of that dock layout — its window class
  forbids docking into an unclassed node, so it always opens as its own OS window, positioned
  centred over the main window's work area on first use and remembered by `imgui.ini` afterwards
  like any other window.
- GPU debug: capture+dump via `MTL_CAPTURE_ENABLED=1 LMX_CAPTURE_AT_FRAME=N LMX_MAX_FRAMES=N+10
  LMX_CAPTURE_PATH=/tmp/out.gputrace xmake run App` (path must be absolute) then `python3
  Tools/GpuDebug/gputrace_dump.py /tmp/out.gputrace`; timings via `python3
  Tools/GpuDebug/profile.py`. What the frame *declared*: `LMX_GRAPH_DUMP=/tmp/out.txt xmake run
  App` writes the first compiled frame's passes, sinks, culled passes, and derived barriers
  (absolute path, written once). The editor's read-only Render Graph panel, in its own detached OS
  window, shows the same compiled record live as a grouped node canvas with a
  selection-scoped details pane (uses, schedule, culling, transitions, transient lifetimes and
  memory) and a button to dump the displayed frame on demand.
  Guide: `docs/guides/gpu-debugging.md`, whose Parity checks section documents the exact procedure
  and commands for verifying auto-exposure/bloom toggles leave pre-M5 output unchanged.

## Architecture
`Source/Core` (lmx:: log/assert) → root `RHI/` component (`RHI/Include/RHI`: public `lmx::rhi`
interfaces with **no Metal or ImGui types**; `RHI/Source`: shared implementation;
`RHI/Backends/Metal4/Source`: the only backend, with metal-cpp, 3 frames in flight, argument tables
+ a per-frame-slot growable frame-data page arena with a checked recycle invariant, residency set,
shared-event pacing, per-pass GPU timing for every pass kind, samplers, sRGB/BC1/cubemap/RGBA16Float
formats, depth-only passes, compute passes with storage bindings, subresource views, and explicit
texture and buffer barriers, copy passes with general copies and fills (the path to any subresource
but level zero), indirect draws and dispatches over RHI-owned argument layouts, and untracked
placement heaps whose resources are created at explicit offsets;
`RHIMetal4ImGui`: optional ImGui glue target) → `Source/Render` (lmx::render: `Camera`, `Mesh`, the
validating `RenderGraph` — raster/compute/copy passes with per-subresource uses over imported
resources and over one-frame transients the graph creates, dead-pass culling from declared sinks
only, conservative aliasing of lifetime-disjoint transients into `TransientPool`'s per-frame-slot
placement heaps, and a `CompiledFrameRecord` per frame — schedule, barriers, transient lifetimes and
assignments, memory totals — that `GraphDump.h` renders as deterministic text; `Renderer` — declares
shadow, scene+sky, histogram exposure (clear/accumulate/resolve, GPU-resident feedback into the next
frame), bloom (threshold/downsample/upsample), and display-transform passes into a graph consuming a
plain `SceneView`; `fitShadowOrtho` and friends are free functions) →
`Source/Engine` (lmx::engine: `Scene`/`SceneLibrary`, GeometryGenerator, DDS/glTF/Radiance HDR
loaders, sRGB color utilities, deterministic environment conversion and CPU-side image-based-lighting
generation (`HdrEnvironment.h`, `Ibl.h`), deterministic offline texture mip baking
(`TextureBake.h`)) → `Source/App` (SDL3 window, a five-panel editor shell — Scene / Viewport /
Inspector / Performance docked together, Render Graph always detached into its own OS window via
Dear ImGui platform viewports — drawn from `Source/App/Panels/` — with a main menu
(File/Window/Layout/Debug, GPU capture with a `C` shortcut), versioned `imgui.ini` workspace
persistence with legacy migration and Reset Default Layout, and a single selection resolved
against the Scene panel's filterable, grouped subject list that drives the Inspector's
subject-scoped editing (camera, rendering, one of three directional lights, or one object); the
Render Graph panel shapes the retained compiled frame into the ImGui-free `GraphNodeModel`, groups
it into a `GraphLayout` of layers, ranks, rows and columns (collapsible stage groups, compact pins,
an optional columns-per-row wrap, no pixels), and draws it on a vendored `ImGuiNodeEditor` canvas as
cards the panel places from their own measured sizes, with a selection-scoped details pane,
deterministic layout stable across unchanged frames, and session-only dragged positions; frame
loop, joins its own UI pass to the graph plus the platform-window render after present,
`--screenshot` path).
Shaders: `Shaders/*.slang` — Encode, Lighting, Shadow (shared modules), ScenePass/ScenePassAuto,
ShadowPass, Sky/SkyAuto, HistogramAccumulate, ExposureResolve, BloomThreshold/BloomDownsample/
BloomUpsample, DisplayTransform (+ Triangle/SamplerSmoke/CubeSmoke/ShadowSmoke/FullscreenSample as
test oracles).
One frame end-to-end: `docs/frame-pipeline.md`.

## Hard rules
- C++23. No Metal 3 fallback (`MTLGPUFamilyMetal4` required). 3 frames in flight.
- Creation returns `Result<T>`; misuse is `LMX_ASSERT`. GPU objects always get labels.
- xrepo deps only as needed (current: libsdl3, glm, spdlog, catch2, cgltf, stb). ThirdParty/ and
  Assets/Fetched/ are fetched via `xmake setup`, pinned in xmake.lua, never committed.
- Engineering and documentation follow `docs/conventions/`. Every commit compiles, passes the
  relevant tests, and passes `xmake policy`.
- Experimental source does not live on `main`. Preserve accepted evidence with an immutable tag
  and develop a new experiment on a short-lived `exp/<topic>` branch; only conclusions, ADRs, and
  adopted production code return to `main`.
- Lighting math runs in scene-linear space and is pre-exposed before the scene target sees it;
  authored color constants, including the editor's clear color, decode via `engine::srgbToLinear`
  (or `Render/ColorTransfer.h`'s copy, below Engine in the dependency chain) once at scene build or
  pass declaration. Nothing upstream of `Shaders/DisplayTransform.slang` encodes sRGB.
- Public-facing copy (README, GitHub About, release text, gallery captions) leads with shipped
  rendering behavior and uses plain feature themes for future work. It never exposes milestone
  numbers, task/plan status, or an unimplemented backend as a current capability. `CLAUDE.md`
  imports this file, so the same rule applies to Codex and Claude Code.

## Update policy
Refresh this file whenever a command, architecture contract, or hard rule changes.
