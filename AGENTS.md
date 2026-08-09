# Luminex

Modern rendering playground / portfolio piece. Metal 4-first (macOS 26+, Apple Silicon) with a
thin RHI and one implemented backend.

## Golden sources
- Spec: `docs/specs/2026-08-07-luminex-upgrade-design.md` (decisions D1–D10 are binding)
- Current architecture: `docs/architecture/overview.md` · Frame walkthrough: `docs/frame-pipeline.md`
- GPU debugging: `docs/guides/gpu-debugging.md`
- ADRs: `docs/decisions/` · Conventions: `docs/conventions/` · Roadmap: `docs/roadmap.md`
- Current baseline: `docs/milestones/m3.1.md` · Active plan:
  `docs/plans/2026-08-10-m4-correct-image-formation.md`

## Commands
- Setup (once): `brew install xmake`, `xmake setup` — fetches pinned ThirdParty deps (metal-cpp,
  slang, Dear ImGui docking-branch commit), Damaged Helmet, and the official ~78 MB Crytek Sponza
  OBJ+PNG archive into gitignored `Assets/Fetched/`, with upstream provenance/license metadata.
  Setup deterministically converts Sponza to uncompressed core glTF; pins and hashes are in
  `xmake.lua`. Optional:
  `xcodebuild -downloadComponent MetalToolchain` enables offline shader precompile (runtime-MSL
  fallback works without it).
- Editor setup (once, for clangd): `xmake project -k compile_commands` writes
  `compile_commands.json` (gitignored) — without it clangd reports spurious diagnostics.
- Build: `xmake` · Run: `xmake run App` · Tests: `xmake test` (CPU-only: `xmake test Tests/unit`)
- **Gotcha**: the Tests target has `set_default(false)` — a plain `xmake` does NOT relink the
  test binary after `Source/` changes. `xmake test` rebuilds it; when running the Tests binary
  directly, `xmake build Tests` first or risk a false pass against a stale binary.
- Format: `xmake format` (check: `xmake format --check`) · Policy: `xmake policy`
- Scenes: `xmake run App` opens the editor with Sponza selected by default (scene dropdown in the
  Inspector). Offscreen: `xmake run App --screenshot <out.bmp>` or `--scene
  <sponza|damaged-helmet> --screenshot <out.bmp>`. Running the binary directly
  requires CWD = its build dir (shaders resolve relative to CWD). Sponza's first load decodes its
  referenced textures — expect several seconds in a debug build.
- Debug: Metal validation `MTL_DEBUG_LAYER=1 xmake run App`; GPU capture: press `c` in-app
  (needs `MTL_CAPTURE_ENABLED=1`), then open the .gputrace in Xcode. Automated runs:
  `LMX_MAX_FRAMES=N` exits after N frames; `LMX_CAPTURE_AT_FRAME=N` captures without a keypress.
- GitHub-hosted macOS exposes a paravirtual GPU without Metal 4. Hosted CI compiles and inventories
  GPU cases; renderer/RHI/shader PRs still require `MTL_DEBUG_LAYER=1 xmake test Tests/gpu` on
  Metal 4 Apple Silicon before merge.
- Controls: fly camera — hold RMB in the Viewport panel + WASD (move) / QE (down/up) while held.
  Dock layout persists via `imgui.ini` next to the built binary (build dir, gitignored).
- GPU debug: capture+dump via `MTL_CAPTURE_ENABLED=1 LMX_CAPTURE_AT_FRAME=N LMX_MAX_FRAMES=N+10
  LMX_CAPTURE_PATH=/tmp/out.gputrace xmake run App` (path must be absolute) then `python3
  Tools/GpuDebug/gputrace_dump.py /tmp/out.gputrace`; timings via `python3
  Tools/GpuDebug/profile.py`. Guide: `docs/guides/gpu-debugging.md`.

## Architecture
`Source/Core` (lmx:: log/assert) → `Source/RHI` (lmx::rhi interfaces; **no Metal types in public
headers**) → `Source/RHI/Metal4` (the only backend: metal-cpp, 3 frames in flight, argument tables
+ per-frame uniform rings with a checked recycle invariant, residency set, shared-event pacing,
samplers, sRGB/BC1/cubemap formats, depth-only passes, `Metal4ImGui` glue) → `Source/Render`
(lmx::render: `Camera`, `Mesh`, `Renderer` — shadow pass → scene+sky pass → barrier, consuming a
plain `SceneView`; `fitShadowOrtho` and friends are free functions) → `Source/Engine` (lmx::engine:
`Scene`/`SceneLibrary`, GeometryGenerator, DDS/glTF loaders, sRGB color utilities) →
`Source/App` (SDL3 window, docked ImGui editor shell — scene dropdown, light editor, render
settings — frame loop, `--screenshot` path). Shaders: `Shaders/*.slang` — six files: Encode,
Lighting, Shadow (shared modules), ScenePass, ShadowPass, Sky (+ Triangle/SamplerSmoke/CubeSmoke/
ShadowSmoke/FullscreenSample as test oracles). One frame end-to-end: `docs/frame-pipeline.md`.

## Hard rules
- C++23. No Metal 3 fallback (`MTLGPUFamilyMetal4` required). 3 frames in flight.
- Creation returns `Result<T>`; misuse is `LMX_ASSERT`. GPU objects always get labels.
- xrepo deps only as needed (current: libsdl3, glm, spdlog, catch2, cgltf, stb). ThirdParty/ and
  Assets/Fetched/ are fetched via `xmake setup`, pinned in xmake.lua, never committed.
- Engineering and documentation follow `docs/conventions/`. Every commit compiles, passes the
  relevant tests, and passes `xmake policy`.
- Lighting math runs in linear space; authored color constants decode via
  `engine::srgbToLinear` at scene build. The single exception is the clear color (written raw —
  the hardware clear bypasses the shader-side sRGB encode).
- Public-facing copy (README, GitHub About, release text, gallery captions) leads with shipped
  rendering behavior and uses plain feature themes for future work. It never exposes milestone
  numbers, task/plan status, or an unimplemented backend as a current capability. `CLAUDE.md`
  imports this file, so the same rule applies to Codex and Claude Code.

## Update policy
Refresh this file whenever a command, architecture contract, or hard rule changes.
