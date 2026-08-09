# Luminex

A Metal 4-native rendering playground and portfolio piece, built around a thin, honest RHI
(`lmx::rhi`) that grows real backends — Vulkan and D3D12 — one feature at a time. Successor to
"lumine", a college-era DX12 renderer.

Metal 4 · C++23 · Slang · xmake

**Status**: M3.1 is the maintained engineering baseline. Two selectable scenes render through a
shadow-mapped, normal-mapped, gamma-correct Blinn-Phong forward pipeline.

| Sponza | Damaged Helmet |
|---|---|
| ![Crytek Sponza atrium rendered by Luminex](docs/media/sponza.png) | ![Khronos Damaged Helmet](docs/media/damaged-helmet.png) |

*Offscreen renders via `App --scene <scene-id> --screenshot`. A capture of the docked editor shell
itself (Viewport + Inspector) is pending a windowed session.*

## Requirements

- macOS 26+, Apple Silicon
- Xcode 26 (Metal Toolchain component optional — enables offline shader precompile; a
  runtime-shader-compile fallback works without it)
- [Homebrew](https://brew.sh)
- [xmake](https://xmake.io) 3.x

## Quick start

```bash
brew install xmake
xmake setup
xmake
xmake run App
xmake test
xmake format
xmake policy
```

`xmake setup` also fetches Damaged Helmet from a pinned upstream commit and the official 78 MB
Crytek Sponza OBJ+PNG archive from Morgan McGuire's Computer Graphics Archive. Setup verifies the
downloads and deterministically converts Sponza to the core glTF subset consumed by Luminex; all
generated files stay under gitignored `Assets/Fetched/`. These assets are not covered by this
repository's Apache-2.0 license; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
`xmake run App --screenshot out.bmp` renders one frame of Sponza offscreen — no window, no
swapchain — and `--scene damaged-helmet` selects the other scene (the
gallery above was produced this way, converted with `sips -s format png … --out docs/media/…` —
no automated conversion step exists; re-run manually when refreshing). The windowed run opens the
docked editor: a central Viewport, and a right-side Inspector with the scene dropdown, light
editing, wireframe/shadow-filter toggles, and per-object transforms. Hold right-mouse in the
Viewport to fly the camera (WASD + Q/E while held). Pressing `c` writes a one-frame
`luminex-frame.gputrace` next to the binary for Xcode's GPU debugger, which requires launching
with `MTL_CAPTURE_ENABLED=1` in the environment.

## Architecture

Luminex is organized as `Source/Core` (logging, assertions) → `Source/RHI` (`lmx::rhi`, the
API-agnostic interface layer — no Metal types leak through public headers) → `Source/RHI/Metal4`
(currently the only backend, built on Apple's official metal-cpp: 3 frames in flight, MTL4 argument
tables, a per-frame transient uniform ring, a single residency set, `MTLSharedEvent` pacing, and the
native Metal 4 ImGui backend glue) → `Source/Render` (`lmx::render`: `Camera`, `Mesh`, and
`Renderer` — the shadow, scene, and sky passes, consuming a plain per-frame `SceneView`) →
`Source/Engine` (`lmx::engine`: the scene system, DDS/glTF import, and procedural geometry) →
`Source/App` (SDL3 window, docked ImGui editor shell, frame loop).
Each frame renders a depth-only shadow pass, then the scene+sky into an offscreen render target,
encodes explicit barriers, then renders the UI pass onto the swapchain — the Viewport window
samples the scene target as an image; [docs/frame-pipeline.md](docs/frame-pipeline.md) walks the
whole frame. The RHI itself
is deliberately **thin, explicit, and honest**: it models the shared conceptual core of Metal 4,
Vulkan, and D3D12, exposes only what the renderer needs, and grows per real feature demand
rather than speculatively. Shaders are authored once in Slang (`Shaders/*.slang`) and compiled to
MSL today, keeping the door open for SPIR-V/DXIL once Vulkan and D3D12 backends return.

## Features

- Two scenes behind an Inspector dropdown: **Sponza** (the default world scene) and **Damaged
  Helmet** (a focused material sample), fetched and checksum-verified by `xmake setup`. Sponza is
  converted locally from its upstream OBJ+PNG distribution to core glTF.
- Shadow mapping: depth-only pass into a 2048² map, 25-tap Poisson-disk PCF or PCSS at runtime,
  ported constant-for-constant from lumine and proven by derivation-based GPU tests.
- Gamma-correct forward Blinn-Phong in linear space (lumine's classic missing-sRGB error, fixed),
  with normal mapping, a cubemap reflection term, and a far-plane sky pass.
- Asset pipeline: DDS (BC1 + cubemaps) and glTF via cgltf + stb; loaders return Engine-owned asset
  errors and are covered by fixture unit tests.
- Docked ImGui editor shell: Viewport, scene dropdown, per-light editing, wireframe and
  shadow-filter toggles, live per-object transform edits; fly camera on right-mouse-drag.
- Metal 4 backend: argument tables and a transient uniform ring per frame in flight (cross-frame
  safety a *checked invariant*, not an assumption), one residency set, shared-event pacing,
  samplers, sRGB/BC1/cubemap formats, depth-only passes — all behind the thin `lmx::rhi`.
- One frame, documented: [docs/frame-pipeline.md](docs/frame-pipeline.md) walks the full pipeline
  and records the current resource flow.

## Roadmap

M1–M3 shipped the Metal/RHI foundation, editor renderer, scene content, and forward-lighting
baseline. M3.1 made that baseline maintainable; M4 begins the render graph, HDR image formation,
and glTF PBR work. See the compact [accepted roadmap](docs/roadmap.md), current
[architecture](docs/architecture/overview.md), and [milestone records](docs/milestones/m3.1.md).

## Credits

- lumine — the college-era DX12 predecessor this project succeeds
- Crytek Sponza — © 2010 Frank Meinl/Crytek, CC BY 3.0; downloaded from Morgan McGuire's
  [Computer Graphics Archive](https://casual-effects.com/data)
- Apple's Metal 4 samples and documentation (*Drawing a triangle with Metal 4*, WWDC25 205/254/211)
- [WickedEngine](https://github.com/turanszkij/WickedEngine) — reference for a production Metal
  RHI backend
- [metal-cpp](https://github.com/apple/metal-cpp) — Apple's official C++ Metal bindings
- [Slang](https://shader-slang.org) — the Khronos-hosted shading language this project builds on

## License

Luminex source code is Apache License 2.0 — see [LICENSE](LICENSE). Fetched sample assets and
their derived gallery images retain their upstream terms; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
