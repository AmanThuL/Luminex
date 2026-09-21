# ADR 0025: The Engine subsystem and the Render-on-Engine edge

**Status**: Proposed

## Context

[ADR 0020](0020-module-layering-and-units.md) retired the Engine name and split its two halves
into `asset`, CPU content, and `scene`, GPU-resident scenes. It let `scene` depend on `render`,
because the vocabulary a scene is written in lived in Render: `Mesh`, `Camera`, `AlphaMode`,
`LocalLight`, `LocalLightMath`, `Bounds` and the table row ABI `SceneTables.h`. The checker made
that direction true; it did not make it right. A scene is described in terms its renderer owns, so
scene description cannot exist below the renderer, and a second consumer of a scene — a baker, an
importer, a scene document — has to link a renderer to name a camera.

Scene reaches seven Render headers. Five include nothing else from Render, and `LocalLightMath.h`
adds only `SceneTables.h`. `SceneView.h` is the exception: beside `DrawItem` and
`DirectionalLight`, which describe a scene, it carries the temporal settings and status, the shadow
filter and the visibility and occlusion modes, which configure a renderer. Render includes nothing
from Scene or Asset, and Asset includes nothing from either. The edge is held in place by where
ten files sit and by one member function, `Scene::view()`.

NVIDIA's Donut, the structure [R3](../milestones/r/r3.md) checks itself against, places scene
types and the scene in `engine` and has `render` depend on it.

## Decision

Luminex has four subsystems over the RojoRHI component: Core, Engine, Render and App. Engine
returns as a name for a directory and a unit; it does not return as the single target ADR 0020
retired.

`Source/Engine/` holds two units. `asset`, at `Source/Engine/Asset/`, is unchanged in everything
but its path: its own static library `Asset`, the namespace `lmx::asset`, a dependency on `core`
alone, the header allowance of `rojoRHI/Format.h` and `rojoRHI/TextureDesc.h`, and the three
layers that prove its independence. `TextureBake` and any GPU-free build still link `core` and
`asset` and no Metal framework. `engine`, the rest of `Source/Engine/`, is the static library
`Engine` with the namespace `lmx::engine`. It owns the scene vocabulary (`Types/`), the scene, its
identities, material records and table ABI (`Scene/`), GPU uploads (`Upload/`) and the catalog with
its labs (`Catalog/`). It depends on `core` and `asset` and may use every RojoRHI public header.
Ownership inside `Source/Engine/` follows the contract's existing longest-path rule, as
`app-model` inside `app-shell` does.

The edge reverses. `render` may depend on `engine` and, through it, on `asset`: the checker charges
a unit for every project header it reaches transitively, and `Scene.h` includes three Asset
headers. `engine` may not depend on `render`. Two checks hold this: the unit table, which rejects
any Engine include of a Render header, and a `forbidUndefined` entry that fails the build's link
check if the `Engine` archive has an undefined `lmx::render::` reference. `scene` ceases to exist
as a unit and `Scene` as a target; `lmx::scene` becomes `lmx::engine`. The vocabulary that moves
from Render takes `lmx::engine`. `Bounds.h` is glm-only math with consumers in three units; it
moves to Core and its types take `lmx`.

`SceneView` stays Render's plain input contract, with no field change. The scene-description types
inside it, `DrawItem` and `DirectionalLight`, move to leaf headers in `Engine/Types/` that
`SceneView.h` includes; `MotionClass` leaves `Temporal.h` the same way. `Scene::view()` becomes
`render::buildSceneView`, a free function in Render that takes the scene, the caller's item vector
and the same arguments. Renderer configuration — temporal settings and status, shadow filter,
visibility, occlusion and lighting modes — stays in Render.

## Consequences

The renderer still consumes a plain struct, so every test that builds a `SceneView` by hand is
unaffected, and App and Tests, which already depend on both units, change a call and an include.

Render gains a dependency it did not have. It is confined to one translation unit, the builder,
and the unit table cannot express that confinement; review holds it until a second Render file
wants a scene, at which point the question is reopened rather than drifted into.

The namespace change is one mechanical commit over the qualified and unqualified uses of a frozen
identifier list, reproducible from a recorded substitution and separate from every move and edit,
as ADR 0020's was. Unqualified uses inside `namespace lmx::render` gain an `engine::` qualifier;
no using-directive is introduced.

`LocalLightMath.cpp` is compiled with `-ffp-contract=off`, and the flag moves with the file. It is
part of the file's numerical contract, not of Render's build.

This decision changes no rendered output, shader, uniform layout, `SceneView` field, pass label,
schema, test case name or RHI semantics. It supersedes ADR 0020 in two places only: the retirement
of the Engine name and the `scene → render` dependency. The unit model, the ownership map, the
reach semantics, the allowlist policy and Asset's independence stand. It is accepted before the
first file moves between units.

## Alternatives considered

**Keep Scene above Render and move only folders.** Rejected: it produces Donut's tree with the
opposite dependency, and leaves scene description unavailable to anything that is not a renderer's
client.

**Move `SceneView` to Engine with the vocabulary.** Rejected: it would carry temporal, shadow,
visibility and lighting configuration below the renderer that defines them, or force those to
split from the struct the renderer consumes. Extracting two leaf types is the smaller change.

**Merge Asset into the Engine library, as Donut links one.** Rejected: Asset's value is that a
tool can link it without a GPU, and a checked unit with its own archive is what proves that.

**A third library for `Engine/Types/`.** Rejected: the vocabulary has no consumer that may not also
link the scene, so a separate archive would add a target and a unit with no boundary to defend.

## Cross-references

- [Module contract](../conventions/modules.md) — the enforceable form of this decision.
- [R3.3 record](../milestones/r/r3.3.md) — path tables, commit sequence and evidence.
- [Part III](../roadmap/codebase-restructuring.md#r33--engine) — outcome and exit gate.
- [ADR 0021](0021-gpu-scene-handoff-contract.md) — the scene-identity and update contract, which
  moves with `Scene/` and does not change.
