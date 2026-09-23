# Architecture Overview

**Status**: Implemented

Luminex is a Metal 4-first rendering playground organized as a one-way dependency stack. The RHI,
RojoRHI, is a repository-root component; the other runtime layers live under `Source/`. Root
`xmake.lua` includes each unit's own `xmake.lua` plus the shared `xmake/` tasks, rules, and setup
that build and test them together; RojoRHI keeps the same shape one level down for its standalone
build. The diagram below shows the layering. The [module contract](../conventions/modules.md) lists
each unit's full dependency set, including the TextureBake and Benchmarks units and every unit
that includes RojoRHI; each page after the diagram covers one subsystem.

```
Core     -> Asset
Core     -> Engine
Asset    -> Engine
Engine   -> Render
Engine   -> Scenes
Core     -> Render
Render   -> AppModel
Scenes   -> AppModel
AppModel -> App
Scenes   -> App
RojoRHI  -> Engine   (independent root component, no dependency on Core)
RojoRHI  -> Render

Render never depends on Scenes.
```

[Core](core.md) is the base of the stack: logging and assertions; file and JSON I/O; glm-based math
and geometry (AABBs, spheres, frusta, projections, low-discrepancy sequences, IBL sampling, TRS
transforms); generic containers (a generational handle with its slot allocator, a dirty set, an
interval, a ring buffer); and small utilities (numeric parsing, SHA-256, a stopwatch, ASCII
lowercasing). Every unit in the module contract may depend on it, and it depends on nothing else
in the repository.

[Engine, Asset and Scenes](engine.md) covers the three units under `Source/Engine` and
`Source/Scenes`. Asset is a CPU-only content library for glTF, DDS, Radiance HDR, and PNG/BMP,
including texture baking and IBL generation. Engine owns the scene vocabulary and the GPU-resident
scene itself, with generational instance, mesh, material, texture, and light identities, paced
upload tables, and playback. Scenes holds the scene catalog built on top of Engine.

[Render Graph](render-graph.md) is the frame-scoped graph builder under `Source/Render/Graph`. It
declares and validates one frame's raster, compute, copy, and external passes before any of them
reach the GPU, culls the passes no declared sink reaches, aliases transient resources into
`TransientPool`, and returns a `CompiledFrameRecord` that `GraphDump.h` can render as deterministic
text.

[Renderer and Passes](render-passes.md) describes `Source/Render/Renderer` and the ten
shader-matched pass families under `Source/Render/Passes/<family>/`: `SceneView` and its builder,
visibility and submission modes, shadow and masked materials, clustered local lighting, temporal
reconstruction, exposure/bloom/display, and the editor-only selection outline.

[Shaders](shaders.md) covers the Slang-to-MSL compilation pipeline and the `Shaders/Common/`,
`Shaders/Passes/<family>/`, and `Shaders/Tests/` folders, along with the import and basename rules
`check_shader_imports.py` enforces.

[App](app.md) is the editor and headless-runner layer above the renderer. It splits into
`AppModel`, the ImGui/SDL/Metal-free editor logic under `Source/App/Model`, and `App`, the SDL3
shell, panels, and frame loops under `Source/App` outside `Model`.

[RojoRHI](rojorhi.md) is the repository-root RHI component: a dependency-free API over Metal 4,
mounted as a git submodule of the public `rojo-rhi` repository. Engine, Scenes, Render and the
layers above them link it directly, outside the Core-based stack. That page covers the Luminex
side; `RojoRHI/docs/architecture/overview.md`, inside the submodule, owns its internal layout and
standalone build.

[Frame walkthrough](frame-pipeline.md) orders the passes of one rendered frame, from `beginFrame`
through presentation.
