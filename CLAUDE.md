# Luminex

Modern rendering playground / portfolio piece. Metal 4-first (macOS 26+, Apple Silicon), thin RHI,
future Vulkan/D3D12 backends. Successor to college project "lumine" (DX12).

## Golden sources
- Spec: `docs/specs/2026-08-07-luminex-upgrade-design.md` (decisions D1–D10 are binding)
- ADRs: `docs/decisions/` · Conventions: `docs/conventions/` (C++ style, shader style, commits)
- Current plan: `docs/plans/2026-08-07-m1-foundation-triangle.md`

## Commands
- Setup (once): `brew install xmake`, `xmake setup`. Optional: `xcodebuild -downloadComponent
  MetalToolchain` enables offline shader precompile (Apple catalog was refusing it 2026-08-07 —
  retry occasionally; the runtime-MSL-compile fallback works without it).
- Editor setup (once, for clangd): `xmake project -k compile_commands` writes
  `compile_commands.json` (gitignored) — without it clangd cannot resolve the xmake-managed
  include paths and reports spurious "file not found" diagnostics.
- Build: `xmake` · Run: `xmake run App` · Tests: `xmake test` (CPU-only: `xmake test Tests/unit`)
- Format: `xmake format` (check: `xmake format --check`)
- Debug: Metal validation `MTL_DEBUG_LAYER=1 xmake run App`; GPU capture: press `c` in-app
  (needs `MTL_CAPTURE_ENABLED=1`), then open the .gputrace in Xcode. Offscreen screenshot:
  `xmake run App --screenshot <out.bmp>`. Automated runs: `LMX_MAX_FRAMES=N` exits after N
  frames (0 or unset = unlimited); `LMX_CAPTURE_AT_FRAME=N` captures frame N without a keypress.

## Architecture
`Source/Core` (lmx:: log/assert) → `Source/RHI` (lmx::rhi interfaces; **no Metal types in public
headers**) → `Source/RHI/Metal4` (the only backend: metal-cpp, 3 frames in flight, argument tables,
residency set, shared-event pacing) → `Source/App` (SDL3 window + frame loop). `Source/Render` is an
intentionally empty placeholder until M2. Shaders: `Shaders/*.slang` (see shader-style.md).

## Hard rules
- C++23. No Metal 3 fallback (`MTLGPUFamilyMetal4` required). 3 frames in flight.
- Creation returns `Result<T>`; misuse is `LMX_ASSERT`. GPU objects always get labels.
- xrepo deps only as needed (M1: libsdl3, glm, spdlog, catch2). ThirdParty/ is fetched via
  `xmake setup`, pinned in xmake.lua, never committed.
- Commits per `docs/conventions/commits.md`. Every commit compiles + passes `xmake test`.

## Subagent & model policy (Rudy's standing instruction)
**Fable 5** runs the main thread — the brain that moderates everything, writes specs/plans, and
reviews all subagent output. Dispatch subagents with model tiers: **Opus 5** for design-sensitive/
thin-doc work (RHI surface, Metal 4 backend, sync, shader toolchain), **Sonnet 5** for standard
implementation (scaffolding, docs, CI), **Haiku 4.5** only for trivial mechanical ops (bulk format,
checkbox updates).

## Update policy
Refresh this file at every milestone boundary (M1 → M2 → …) and whenever a command or hard rule changes.
