# Architecture Overview

**Status**: Implemented

Luminex is a Metal 4-first rendering playground organized as a one-way dependency stack. The RHI
is a repository-root component; the other runtime layers remain under `Source/`:

`Core → RHI → Render → Engine → App`

- **Core** owns logging, assertions, and dependency-free utilities.
- **RHI** is built from `RHI/xmake.lua`. Its self-contained core public headers live under
  `RHI/Include/RHI/` and expose API-neutral resource, pipeline, command, synchronization, capture,
  and domain-owned error contracts without Metal or ImGui dependencies.
- **RHI/Backends/Metal4** implements the current backend with private metal-cpp headers, three
  frames in flight, argument tables, a per-frame uniform ring, residency, shared-event pacing,
  render, compute, and copy pass encoders, indirect draws and dispatches, untracked placement heaps
  with resources created at explicit offsets, per-pass GPU timing, and capture support. The optional `RHIMetal4ImGui` target owns the adapter,
  its ImGui-dependent public extension header, and the dependency on Dear ImGui; the core RHI does
  not inherit any of them.
- **Render** owns camera, mesh, the validating render graph (`RenderGraph`), the shadow/scene/sky/
  display passes it declares, and the plain per-frame `SceneView` it consumes. The graph is
  declared fresh every frame and validates its declarations before any of them reach the GPU. It
  declares raster, compute, and copy passes with per-subresource uses over resources it either
  imports from a caller or creates as one-frame transients, culls every pass no declared sink
  reaches, places lifetime-disjoint transients in the shared bytes of a `TransientPool` placement
  heap, and answers with a `CompiledFrameRecord` describing the frame it encoded — schedule,
  barriers, transient lifetimes and assignments, and memory totals; `GraphDump.h` renders that
  record as deterministic text.
- **Engine** owns scenes, procedural geometry, color conversion, DDS/glTF/Radiance HDR decoding,
  deterministic equirectangular environment conversion and image-based-lighting generation
  (`HdrEnvironment.h`, `Ibl.h`), and deterministic offline texture mip baking (`TextureBake.h`).
- **App** owns SDL3, the editor shell, scene selection, startup error reporting, and the frame loop.

Shaders are authored in Slang and compiled to readable MSL, then to a metallib when the offline Metal
toolchain is present. The runtime MSL path remains a supported fallback. The live frame sequence and
resource transitions are documented in `docs/frame-pipeline.md`.

The root component is a physical and build boundary, not yet a separately published library: it
still participates in this repository's Core contracts and validation. The RHI grows only when a
rendering feature supplies a real portability requirement. Metal is the first implementation, not
the public vocabulary: accepted contracts do not leak native handles upward. Before temporal and
scalable-scene layers build more contracts on the production RHI, the roadmap requires a measured
decision on whether to retain, partially reshape, or replace its caller-facing execution model.
D3D12 is the intended second production backend; Vulkan remains research evidence rather than a
planned target.
