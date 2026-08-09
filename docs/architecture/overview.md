# Architecture Overview

**Status**: Implemented

Luminex is a Metal 4-first rendering playground organized as a one-way dependency stack:

`Core → RHI → Render → Engine → App`

- **Core** owns logging, assertions, and dependency-free utilities.
- **RHI** exposes API-neutral resource, pipeline, command, and synchronization contracts. Its public
  headers contain no Metal types.
- **RHI/Metal4** implements the current backend with metal-cpp, three frames in flight, argument
  tables, a per-frame uniform ring, residency, shared-event pacing, and capture support.
- **Render** owns camera, mesh, pass orchestration, and the plain per-frame `SceneView` it consumes.
- **Engine** owns scenes, procedural geometry, color conversion, and DDS/glTF decoding.
- **App** owns SDL3, the editor shell, scene selection, startup error reporting, and the frame loop.

Shaders are authored in Slang and compiled to readable MSL, then to a metallib when the offline Metal
toolchain is present. The runtime MSL path remains a supported fallback. The live frame sequence and
resource transitions are documented in `docs/frame-pipeline.md`.

The RHI grows only when a rendering feature supplies a real cross-backend requirement. Metal is the
first implementation, not the public vocabulary: Vulkan and D3D12 must be able to implement each
accepted contract without leaking their native handles upward.
