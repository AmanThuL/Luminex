# Architecture Overview

**Status**: Implemented

Luminex is a Metal 4-first rendering playground organized as a one-way dependency stack:

`Core → RHI → Render → Engine → App`

- **Core** owns logging, assertions, and dependency-free utilities.
- **RHI** exposes API-neutral resource, pipeline, command, and synchronization contracts. Its public
  headers contain no Metal types.
- **RHI/Metal4** implements the current backend with metal-cpp, three frames in flight, argument
  tables, a per-frame uniform ring, residency, shared-event pacing, per-pass GPU timing, and
  capture support.
- **Render** owns camera, mesh, the validating render graph (`RenderGraph`), the shadow/scene/sky/
  display passes it declares, and the plain per-frame `SceneView` it consumes. The graph is
  declared fresh every frame and validates its declarations before any of them reach the GPU.
- **Engine** owns scenes, procedural geometry, color conversion, DDS/glTF decoding, deterministic
  image-based-lighting generation (`Ibl.h`), and deterministic offline texture mip baking
  (`TextureBake.h`).
- **App** owns SDL3, the editor shell, scene selection, startup error reporting, and the frame loop.

Shaders are authored in Slang and compiled to readable MSL, then to a metallib when the offline Metal
toolchain is present. The runtime MSL path remains a supported fallback. The live frame sequence and
resource transitions are documented in `docs/frame-pipeline.md`.

The current RHI grows only when a rendering feature supplies a real portability requirement. Metal
is the first implementation, not the public vocabulary: accepted contracts do not leak native
handles upward. The roadmap reserves an API-model experiment after the next execution-substrate
slice and before temporal and scalable-scene layers build on it. D3D12 is the intended second
production backend; Vulkan remains research evidence rather than a planned target.
