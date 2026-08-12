# Luminex

Modern rendering playground / portfolio piece. Metal 4-first (macOS 26+, Apple Silicon) with a
thin RHI and one implemented backend.

## Golden sources
- Spec: `docs/specs/2026-08-07-luminex-upgrade-design.md` (decisions D1–D10 are binding)
- Current architecture: `docs/architecture/overview.md` · Frame walkthrough: `docs/frame-pipeline.md`
- GPU debugging: `docs/guides/gpu-debugging.md`
- ADRs: `docs/decisions/` · Conventions: `docs/conventions/` · Roadmap: `docs/roadmap.md`
- Current baseline: `docs/milestones/m5.2.md` (frame-data path, ADR 0010) over
  `docs/milestones/m5.1.md` over `docs/milestones/m5.md`

## Commands
- Setup (once): `brew install xmake`, `xmake setup` — fetches pinned ThirdParty deps (metal-cpp,
  slang, Dear ImGui docking-branch commit), Damaged Helmet, the CC0 Studio Small 09 HDRI, and the
  official ~78 MB Crytek Sponza OBJ+PNG archive into gitignored `Assets/Fetched/`, with upstream
  provenance/license metadata.
  Setup deterministically converts Sponza to uncompressed core glTF, then bakes every base-color
  and normal image referenced by Sponza and Damaged Helmet into a deterministic offline mip chain
  (`Tools/TextureBake`, DDS + manifest) that scene loading prefers over its in-process fallback;
  pins and hashes are in `xmake.lua`. Optional:
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
- Scenes: `xmake run App` opens the editor with Sponza selected by default (scene dropdown in the
  Inspector). Offscreen: `xmake run App --screenshot <out.bmp>` or `--scene
  <sponza|damaged-helmet|material-lab> --screenshot <out.bmp>`. Running the binary directly
  requires CWD = its build dir (shaders resolve relative to CWD). Sponza's first load decodes its
  referenced textures — expect several seconds in a debug build.
- Debug: Metal validation `MTL_DEBUG_LAYER=1 xmake run App`; GPU capture: press `c` in-app
  (needs `MTL_CAPTURE_ENABLED=1`), then open the .gputrace in Xcode. Automated runs:
  `LMX_MAX_FRAMES=N` exits after N frames; `LMX_CAPTURE_AT_FRAME=N` captures without a keypress.
  The Inspector's Stats panel shows a pausable 60-frame rolling average for each render-graph pass,
  refreshed four times per second; hover shows latest/range details. The Render Graph panel keeps
  the exact newest-retired-frame timings.
- GitHub-hosted macOS exposes a paravirtual GPU without Metal 4. Hosted CI compiles and inventories
  GPU cases; renderer/RHI/shader PRs still require `MTL_DEBUG_LAYER=1 xmake test Tests/gpu` on
  Metal 4 Apple Silicon before merge.
- Controls: fly camera — hold RMB in the Viewport panel + WASD (move) / QE (down/up) while held.
  Dock layout persists via `imgui.ini` next to the built binary (build dir, gitignored).
- GPU debug: capture+dump via `MTL_CAPTURE_ENABLED=1 LMX_CAPTURE_AT_FRAME=N LMX_MAX_FRAMES=N+10
  LMX_CAPTURE_PATH=/tmp/out.gputrace xmake run App` (path must be absolute) then `python3
  Tools/GpuDebug/gputrace_dump.py /tmp/out.gputrace`; timings via `python3
  Tools/GpuDebug/profile.py`. What the frame *declared*: `LMX_GRAPH_DUMP=/tmp/out.txt xmake run
  App` writes the first compiled frame's passes, sinks, culled passes, and derived barriers
  (absolute path, written once). The editor's read-only Render Graph inspector panel shows the same
  compiled record live (uses, schedule, culling, transitions, transient lifetimes and memory) with a
  button to dump the displayed frame on demand. Guide: `docs/guides/gpu-debugging.md`, whose Parity
  checks section documents the exact procedure and commands for verifying auto-exposure/bloom
  toggles leave pre-M5 output unchanged.

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
(`TextureBake.h`)) → `Source/App` (SDL3 window, docked ImGui editor shell — scene dropdown, light
editor, render settings including histogram auto-exposure and bloom toggles, a read-only Render
Graph inspector panel — frame loop, joins its own UI pass to the graph, `--screenshot` path).
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
- Repository-root experiments use `lmx::experimental::<name>` namespaces; for example,
  `Experiments/NoApi/` is `lmx::experimental::noapi`. Never put experiment-owned APIs directly
  under `lmx` or use a one-off namespace marker.
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
