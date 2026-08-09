# Luminex

[![Platform](https://img.shields.io/badge/platform-macOS_26+-black)](#requirements)
[![GPU API](https://img.shields.io/badge/GPU_API-Metal_4-555555)](#architecture)
[![Language](https://img.shields.io/badge/C%2B%2B-23-00599C)](#build-and-run)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue)](LICENSE)

A compact C++23 renderer built directly on Metal 4. Luminex combines an explicit RHI, Slang
shaders, real glTF content, GPU validation tests, and a docked editor in one inspectable codebase.

[Rendering today](#rendering-today) · [Frame flow](#frame-flow) · [Build and run](#build-and-run) ·
[Architecture](#architecture) · [What's next](#whats-next)

![Crytek Sponza atrium rendered by Luminex](docs/media/sponza.png)

<p align="center"><sub>Crytek Sponza · forward lighting, normal mapping, PCF shadows · captured by the offscreen renderer</sub></p>

## Rendering today

| Area | Current implementation |
|---|---|
| GPU backend | Native Metal 4 through metal-cpp; three frames in flight, argument tables, explicit residency and shared-event pacing |
| Frame | Depth-only shadow pass → forward scene and sky → offscreen color → docked ImGui viewport |
| Lighting | Gamma-correct linear Blinn-Phong, normal mapping, cubemap reflections and three directional lights |
| Shadows | 2048² directional shadow map with selectable 25-tap Poisson PCF or PCSS |
| Content | Deterministically converted Crytek Sponza and Khronos Damaged Helmet through a focused glTF loader |
| Editor | Scene selection, fly camera, light and object transforms, wireframe and shadow-filter controls |
| Diagnostics | Object/pass labels, Metal validation, deterministic GPU smoke tests, capture sidecars and profiling tools |

<p align="center">
  <img src="docs/media/damaged-helmet.png" alt="Khronos Damaged Helmet rendered by Luminex" width="760">
  <br>
  <sub>Damaged Helmet · focused material, tangent-space and texture-path coverage</sub>
</p>

## Frame flow

```mermaid
flowchart TB
    subgraph Scene["Scene preparation"]
        direction LR
        Assets["glTF + procedural geometry"] --> Engine["Engine scene"] --> View["SceneView"]
    end
    subgraph Frame["Metal 4 frame"]
        direction LR
        Shadow["Shadow pass"] -->|depth map| Forward["Forward + sky pass"] --> Color["Offscreen color"]
    end
    subgraph Display["Presentation"]
        direction LR
        Barrier["Texture barrier"] --> UI["ImGui viewport"] --> Present["Metal swapchain"]
    end
    View --> Shadow
    View --> Forward
    Color --> Barrier
```

The renderer keeps scene ownership above the rendering layer: Engine produces a plain per-frame
`SceneView`, Render consumes it, and the RHI records the Metal 4 work without leaking Metal types
through public interfaces. The complete resource and synchronization walkthrough is in
[docs/frame-pipeline.md](docs/frame-pipeline.md).

## Build and run

### Requirements

- Apple Silicon running macOS 26 or newer
- Xcode 26
- [Homebrew](https://brew.sh) and [xmake](https://xmake.io) 3.x

```bash
brew install xmake
xmake setup
xmake
xmake run App
```

`xmake setup` fetches pinned dependencies and sample assets, verifies their hashes, and converts
the official Crytek Sponza OBJ distribution into the core glTF subset used at runtime. Generated
content remains under the gitignored `Assets/Fetched/` directory.

The editor opens on Sponza. Hold right mouse in the viewport and use WASD + Q/E to fly. Select
Damaged Helmet from the Inspector, or render either scene without a window:

```bash
xmake run App --scene damaged-helmet --screenshot helmet.bmp
```

<details>
<summary><strong>Testing, validation and GPU capture</strong></summary>

```bash
xmake test Tests/unit
MTL_DEBUG_LAYER=1 xmake test Tests/gpu
xmake format --check
xmake policy
```

Press `c` in a windowed run launched with `MTL_CAPTURE_ENABLED=1` to write a `.gputrace` for Xcode.
Automated capture and profiling workflows are documented in
[docs/guides/gpu-debugging.md](docs/guides/gpu-debugging.md).

GitHub-hosted macOS runners compile and inventory the GPU suite but cannot execute Metal 4 on their
paravirtual GPU. Renderer changes therefore require the local Metal-validation command above.

</details>

## Architecture

```mermaid
flowchart TB
    subgraph Layers["Renderer layers"]
        direction LR
        App["App<br/>SDL3 + ImGui"] --> Engine["Engine<br/>scenes + assets"] --> Render["Render<br/>camera + passes"]
    end
    subgraph GPU["GPU interface"]
        direction LR
        RHI["RHI<br/>API-neutral contracts"] --> Metal["Metal 4<br/>backend"]
        Shaders["Slang shaders"] --> Metal
    end
    Render --> RHI
```

The RHI is intentionally thin and currently has one backend: Metal 4. It owns resource creation,
command recording, synchronization, residency and swapchain contracts; rendering policy stays in
`Source/Render`, while scene and asset policy stays in `Source/Engine`.

| Path | Responsibility |
|---|---|
| `Source/RHI` | Backend-neutral interfaces and validation |
| `Source/RHI/Metal4` | Metal objects, frame lifetime, command encoding and ImGui integration |
| `Source/Render` | Camera, meshes, shadow/scene/sky passes and frame targets |
| `Source/Engine` | Scene catalog, glTF/DDS loading, geometry and color handling |
| `Source/App` | SDL3 window, editor shell, CLI and offscreen screenshots |
| `Shaders` | Slang modules and render/test entry points |
| `Tools/GpuDebug` | Capture inspection, schema validation and profiling |

## What's next

- A validating render graph with GPU timestamps and transient resource lifetimes
- Scene-linear HDR, exposure and a replaceable display transform
- glTF metallic-roughness PBR, image-based lighting and a deterministic material lab
- Correct filtered mip generation, normal transforms and broader material coverage
- Temporal state, motion vectors and a reference anti-aliasing/upscaling path
- GPU scene data, visibility culling and indirect submission after the frame contract is stable

The dependency order and exit gates live in the [engineering roadmap](docs/roadmap.md).

## Current boundaries

- Metal 4 on Apple Silicon is the only runtime backend.
- The renderer is forward, LDR and Blinn-Phong; full glTF PBR and post-processing are not present.
- Large sample scenes load synchronously and require `xmake setup` before first use.
- Sample assets and derived gallery images keep their upstream licenses; see
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Documentation

- [Architecture overview](docs/architecture/overview.md)
- [One frame end to end](docs/frame-pipeline.md)
- [GPU debugging guide](docs/guides/gpu-debugging.md)
- [Engineering roadmap](docs/roadmap.md)
- [Engineering conventions](docs/conventions/engineering.md)

## Credits and license

Crytek Sponza is © 2010 Frank Meinl/Crytek and distributed under CC BY 3.0 through Morgan
McGuire's [Computer Graphics Archive](https://casual-effects.com/data). Luminex also builds on
Apple's Metal 4 documentation and samples, [metal-cpp](https://github.com/apple/metal-cpp),
[Slang](https://shader-slang.org), and the open-source graphics community.

Luminex source code is licensed under the [Apache License 2.0](LICENSE). Fetched sample assets are
not covered by the source license; their attribution and terms are recorded in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
