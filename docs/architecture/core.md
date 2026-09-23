# Core

**Status**: Implemented

Core (`lmx`, `Source/Core`) is the base of Luminex's one-way dependency stack. Every unit in the
module contract may depend on it. The [module contract](../conventions/modules.md) lists nothing
in Core's own "may depend on" row. It holds math, containers, and general services that carry no
domain meaning; nothing in it names a scene, a pass, or a panel.

## Folders

Core has five folders:

- `Diagnostics/`: logging, log sinks, and assertions.
- `IO/`: whole-file reads and JSON escaping.
- `Math/`: alignment and dispatch division; colour transfer; finite AABBs with their eight-corner
  transform (`Aabb.h`); spheres; frusta; reversed-infinite-Z and orthographic-fit projections;
  low-discrepancy sequences; cubemap/GGX sampling measures; and TRS transforms.
- `Containers/`: a generational handle with its slot allocator, a dirty set, an interval, and a
  ring buffer.
- `Util/`: numeric parsing, SHA-256, a stopwatch, and ASCII lowercasing.

Core owns the shared colour-transfer functions used above it and the contract-preserving
primitives, such as the generational handle and its slot allocator, that other units build their
own identities on.

Core's two public third-party packages are spdlog and glm. glm is the project's one vector,
matrix, and quaternion vocabulary, used in Core and every unit above it; Core does not define
aliases of its own, and it does not wrap standard containers.

## What may enter Core

[ADR 0026](../decisions/0026-core-charter-and-placement.md) sets the admission rule: math and data
structures with no domain meaning enter Core whatever their consumer count; code that takes or
returns a domain type, or fixes a domain constant, stays in its owning unit; any other helper
needs consumers in two or more units sharing one contract. Code arrives with its own unit tests
and keeps its numerical contract, including `-ffp-contract=off`.

## Dependencies

Core depends on nothing else in the repository: not on [RojoRHI](rojorhi.md), and not on Asset,
Engine, Render, Scenes, AppModel or App. Every unit in the module contract declares `core`
directly: `asset`, `engine`, `render`, `scenes`, `app-model`, `app-shell`, `tests`,
`texture-bake` and `benchmarks`.

A `forbidUndefined` entry on the `Core` archive, checked by `check_module_deps.py --link`, fails
if the archive names an undefined symbol in `lmx::asset`, `lmx::engine`, `lmx::scenes`,
`lmx::render`, `lmx::app` or `rojoRHI`; Core includes no RojoRHI header.

## Tests

- `Tests/Core/Math/`: AABB accumulation, colour transfer, frusta, projections, low-discrepancy
  sequences, sampling measures, spheres and TRS decomposition; `CoreUtilityTests` also sits here
  with alignment, dispatch division, numeric parsing and the two IO cases (JSON escaping,
  whole-file reading).
- `Tests/Core/Containers/`: the generational handle with its slot allocator, the dirty set, the
  interval and the ring buffer.
- `Tests/Core/Util/`: the stopwatch and ASCII lowercasing.
- `Tests/Core/Diagnostics/`: logging initialisation; the file also keeps one alignment case.
