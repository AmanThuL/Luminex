# Luminex Foundation — Design Spec

**Date**: 2026-08-07
**Status**: Implemented
**Supersedes**: the current repo state (Premake + GLFW + Vulkan triangle test), which is treated as disposable.
**Current sequencing**: decisions D1–D10 remain binding; [ADR 0007](../decisions/0007-d3d12-backend-target.md)
and the [roadmap](../roadmap.md) supersede this historical spec's future-backend ordering.

## 1. Purpose & Goals

Luminex is a playground for modern rendering work, starting with a Metal 4-native foundation.
The project goals, in priority order:

1. **A playground for modern rendering features** that doubles as a **portfolio piece**.
2. **Mature, native use of graphics APIs** — starting with **Metal 4** on macOS (Apple Silicon), the only dev machine available. Vulkan/D3D12 return later behind the RHI.
3. **A robust but honest RHI** (`lmx::rhi`) — grown feature-by-feature, never speculative.
4. **Modern-but-stable C++ infrastructure** as used in the open-source graphics world in 2026.

Non-goals for milestone 1: new rendering techniques, Windows support, engine features (ECS, asset pipeline, editor).

## 2. Decisions (with rationale)

| # | Decision | Rationale (research-verified Aug 2026) |
|---|---|---|
| D1 | **Build system: xmake** (not Premake or CMake) | Most actively maintained tool in the space (~5–6 week release cadence, v3.0.9 May 2026); built-in `xcode.metal` rule; xrepo (~2,000 pkgs) replaces submodules; Lua config like Premake. Premake's Xcode exporter has decade-old gaps incl. unresolved Metal compiler setup — disqualifying for Metal-first. |
| D2 | **Metal 4 native as first backend** (not Vulkan-via-MoltenVK/KosmicKrisp) | Native API of the dev machine; Xcode GPU debugging of *our own* calls; MTL4's allocator/argument-table/residency model is the most modern of the three APIs and keeps the RHI honest; portfolio distinctiveness (open-source C++ Metal 4 renderers are rare). metal-cpp is an official Apple repo since June 2026 with complete MTL4 coverage. |
| D3 | **C++23** (Apple Clang / Xcode 26) | `std::expected`, deducing this, `std::print`, ranges — modern but proven. Modules excluded (toolchain still lags). |
| D4 | **SDL3** windowing (not GLFW) | Monthly releases; `SDL_Metal_CreateView`/`SDL_Metal_GetLayer` give a `CAMetalLayer` from plain C — no Obj-C++ glue. GLFW is alive but slow-cadence and needs Cocoa glue for Metal. |
| D5 | **Slang shaders from day 1** | Khronos-hosted, 34% adoption and rising; one `.slang` source → MSL now, SPIR-V/DXIL later. |
| D6 | **Slang → MSL source → `xcrun metal -std=metal4.0` → `.metallib`** (two-step, not direct metallib) | Slang's direct metallib path has open Metal 4 bugs (slang #12325, #12096). Readable MSL in the build tree aids debugging. Collapse to one step when bugs close. |
| D7 | **glm** replaces cglm | C++ community standard; fits C++23. cglm is a C library from the old design. |
| D8 | **clang-format** (+ curated clang-tidy) | Still the de facto standard in 2026; nothing mainstream has displaced it. clang-tidy (`modernize-*`, `bugprone-*`, `performance-*`) runs as a non-blocking CI job. |
| D9 | **Catch2 v3** for tests | Mainstream, maintained, in xrepo. |
| D10 | **spdlog** for logging | Mainstream, stable. |

Each of D1, D2, D5/D6, and the RHI shape gets a retroactive ADR (0001–0004) in `docs/decisions/`.

## 3. Repository structure

```
Luminex/
├── xmake.lua                  # workspace: options, toolchain, C++23
├── CLAUDE.md                  # kept updated at milestone boundaries
├── README.md                  # portfolio-facing
├── .clang-format
├── docs/
│   ├── conventions/           # cpp-style.md, shader-style.md, commits.md
│   ├── decisions/             # numbered ADRs (0001-build-system.md, ...)
│   └── specs/                 # this file and future design docs
├── Source/
│   ├── Core/                  # lmx::        log, assert, time, platform
│   ├── RHI/                   # lmx::rhi     API-agnostic interfaces
│   │   └── Metal4/            #              the only backend for now
│   ├── Render/                # lmx::render  placeholder, milestone 2+
│   └── App/                   # SDL3 window + frame loop + triangle
├── Shaders/                   # .slang sources
├── Tests/                     # Catch2 (Core + CPU-side RHI) + GPU smoke test
└── ThirdParty/                # ONLY non-xrepo deps, pinned: metal-cpp, slang binary
```

- xrepo deps: `libsdl3`, `glm`, `imgui` (SDL3 + Metal 4 backends), `stb`, `nlohmann_json`, `spdlog`, `catch2`.
- Existing submodules (glfw, cglm, imgui, stb, json), the Premake tree, `.bat` scripts, `Luminex.sln`, and the Vulkan triangle are **removed**. Vulkan returns later as backend #2 behind the RHI.
- Scene content and rendering techniques (PCF/PCSS shadows and sky rendering) arrive in milestone
  3, once a renderer exists to consume them.

## 4. RHI design (`lmx::rhi`)

**Philosophy: thin, explicit, honest.** Model the shared conceptual core of Metal 4 / Vulkan / D3D12; expose only what the current milestone needs; grow per real feature demand. References: NVRHI (interface shape), WickedEngine `wiGraphicsDevice_Metal.cpp` (full MTL4 RHI backend in production), Apple game-porting-toolkit skills (`translating-to-metal4-api`, `managing-metal4-synchronization`, `managing-metal-cpp-lifetimes`).

### Public surface (milestone 1)

```cpp
namespace lmx::rhi {
  class Device;            // creation hub: buffers, textures, pipelines, swapchain
  class Swapchain;         // wraps CAMetalLayer; acquire → render target, present
  class CommandList;       // begin/end, beginRenderPass(desc), bindPipeline, draw
  struct Buffer;  struct Texture;          // opaque handles + Desc structs
  struct ShaderLibrary;    // loaded .metallib (later: .spv)
  struct GraphicsPipeline; // shaders + vertex layout + attachment formats
}
```

- **No Metal types leak** through public headers; backend selected at build time behind a factory.
- **Creation returns `std::expected<T, Error>`**; API misuse is a fatal `LMX_ASSERT`, not an error code.
- **Binding model is argument-table/bindless-first** — all three APIs converge there; retrofitting bindless onto a slot-based RHI is the classic mistake.

### Metal 4 mapping (per Apple's "Drawing a triangle with Metal 4" sample)

| RHI concept | Metal 4 implementation |
|---|---|
| `Device` | `MTLDevice` + `MTL4CommandQueue` + `MTL4Compiler` + one queue-attached `MTLResidencySet` |
| Frame pacing | 3 frames in flight (matching Apple's sample); per-frame `MTL4CommandAllocator` (reset per frame); `MTLSharedEvent` throttle |
| `CommandList` | long-lived `MTL4CommandBuffer` + `MTL4RenderCommandEncoder`; bindings via `MTL4ArgumentTable` |
| `Swapchain::present` | `queue.waitForDrawable` → `commit` → `signalDrawable` → `drawable.present` |
| `GraphicsPipeline` | PSO via `MTL4Compiler` from metallib functions |

Notes that shape the implementation: MTL4 command buffers are device-created, long-lived, and hold **unretained** references (RHI owns lifetimes — see Apple's `managing-metal-cpp-lifetimes` skill); resources are **untracked** (explicit barriers when multi-pass arrives); `MTLGPUFamilyMetal4` is checked at device creation and is a hard requirement (no Metal 3 fallback — this is a Metal 4 project).

### Deliberate v1 omissions (documented in the RHI header)

Explicit barriers (one pass), compute, multi-queue, dynamic residency (everything resident in one set), queries, RT. Each has a known Metal 4 answer when a feature demands it.

### Debugging

`MTL_DEBUG_LAYER` validation in debug builds; programmatic `MTLCaptureManager` capture (keybind → `.gputrace` → Xcode GPU debugger — replaces xmake's weak Xcode project generation); GPU labels on every object from day 1.

## 5. Shader pipeline

- Author in `.slang` (`Shaders/Triangle.slang`, `vertexMain`/`fragmentMain`).
- xmake rule: `slangc -target metal` → readable MSL → `xcrun metal -std=metal4.0` → `.metallib`, with dependency tracking.
- Flat, explicit binding indices mapping 1:1 to `MTL4ArgumentTable` slots now, Vulkan descriptor sets later.
- Shader style conventions in `docs/conventions/shader-style.md`.

## 6. Tooling, testing, CI

- **Format**: project `.clang-format`, `xmake format`, and a blocking CI check.
- **Static analysis**: clang-tidy curated set, non-blocking CI job.
- **Tests** (`xmake test`): Catch2 CPU tests plus offscreen Metal tests that read back and assert
  rendered pixels, synchronization, and resource contracts.
- **CI**: GitHub Actions `macos-26` builds Release, runs CPU and Metal-validation GPU tests, checks
  formatting and C++ layout; repository policy also runs on Linux.
- **Error handling**: `std::expected` at creation/loading boundaries; `LMX_ASSERT` fatal + descriptive for contract violations; `spdlog` leveled logging; fail fast, no silent fallbacks.
- **Commits**: imperative mood, English, no AI co-author trailers (matches repo history style).

## 7. Milestones

- **M1 — Foundation + triangle through RHI (this spec's scope)**
  DoD: `xmake && xmake run App` opens an SDL3 window rendering the Metal 4 triangle through `lmx::rhi`; `xmake test` green (unit + local GPU smoke); format/lint clean; CLAUDE.md, ADRs 0001–0004, this spec committed; CI green.
- **M2 — Renderer skeleton** (done 2026-08-07): `lmx::render` becomes real — mesh/camera/uniform plumbing, depth buffer, ImGui (Metal 4 + SDL3 backends) overlay.
- **M3 — Scene content and forward lighting** (done 2026-08-08): a gamma-corrected forward
  pipeline with test scenes behind a scene dropdown,
  shadow mapping (PCF/PCSS), normal mapping, DDS/glTF/text asset loaders, and the new
  `Source/Engine` layer. The maintained forward path now lives in `docs/roadmap.md`.

## 8. Key references

- Apple: [Understanding the Metal 4 core API](https://developer.apple.com/documentation/metal/understanding-the-metal-4-core-api) · [Metal 4 compilation API](https://developer.apple.com/documentation/metal/using-the-metal-4-compilation-api) · [Drawing a triangle with Metal 4](https://developer.apple.com/documentation/metal/drawing-a-triangle-with-metal-4) · WWDC25 205/254/211 · [game-porting-toolkit](https://github.com/apple/game-porting-toolkit) (Metal 4 skills + C++ port samples) · [metal-cpp (official)](https://github.com/apple/metal-cpp) · MSL spec 4.1
- Community: [Metal by Example — Metal 4](https://metalbyexample.com/metal-4/) · [Hello Triangle, Metal 4, pure C++](https://dev.to/javiersalcedopuyo/writing-a-hello-triangle-with-metal-4-and-exclusively-c-3kgm) (+ [repo](https://github.com/javiersalcedopuyo/metal-4-with-only-cpp)) · [WickedEngine Metal 4 backend](https://github.com/turanszkij/WickedEngine) · imgui `example_sdl3_metal4`
- Tooling: [xmake](https://xmake.io) · [Slang Metal target](https://shader-slang.org/slang/user-guide/metal-target-specific) · [SDL3 Metal view](https://wiki.libsdl.org/SDL3/SDL_Metal_CreateView)
