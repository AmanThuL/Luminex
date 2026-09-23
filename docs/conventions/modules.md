# Module Contract

**Status**: Accepted

The repository is a set of named units with an explicit dependency set each. A unit may depend on
the units and third-party packages listed for it and on nothing else; anything absent from its row
is forbidden, and the policy checker rejects it. The layering, the unit names and the namespace
renames are decided in [ADR 0020](../decisions/0020-module-layering-and-units.md), narrowed by
[ADR 0025](../decisions/0025-engine-subsystem-and-render-on-engine.md) for the Engine subsystem and
the render-on-engine edge, and by
[ADR 0026](../decisions/0026-core-charter-and-placement.md) for the placement rule and Core's
ownership row; the migration order lives in the
[refactoring roadmap](../roadmap/codebase-module-boundaries.md).

## Units

`Paths` names the unit's directory and its build target. App model logic owns its Model directory
and AppModel static target.

| Unit | Paths (target) | Namespace | Owns | May depend on | Third-party |
|---|---|---|---|---|---|
| `core` | `Source/Core` (`Core`) | `lmx` | Logging, assertions, whole-file reading, JSON escaping; the geometry and algorithms over glm types — AABBs with their corner transform, spheres, frusta, colour transfer, TRS transforms, reversed-infinite-Z and orthographic-fit projections, low-discrepancy sequences, IBL sampling measures, alignment and dispatch division; the generic containers: a generational handle with its slot allocator, a dirty set, an interval, a ring buffer; numeric parsing, SHA-256, a stopwatch, ASCII lowercasing | — | spdlog, glm |
| `asset` | `Source/Engine/Asset` (`Asset`) | `lmx::asset` | CPU decoding, texture baking, IBL generation, procedural geometry, animation clip data and sampling, the asset error domain, repository asset discovery | `core` | glm, cgltf, stb |
| `engine` | `Source/Engine` outside `Asset` (`Engine`) | `lmx::engine` | Scene vocabulary — `Camera`, mesh geometry, `LocalLightMath`, `AlphaMode`, `LocalLight`, `DrawItem`, `DirectionalLight`, `MotionClass` and the shared scene-table row ABI/bindings — plus GPU-owning scenes: generational identities, immutable geometry pool, paced scene tables, uploads, environment rig, playback, initial camera | `core`, `asset` | glm |
| `scenes` | `Source/Scenes` (`Scenes`) | `lmx::scenes` | The catalog: `SceneLibrary` and `SceneId`, the four labs, San Miguel, the Sponza light rig and camera tour | `core`, `asset`, `engine` | glm |
| `render` | `Source/Render` (`Render`) | `lmx::render` | `SceneView` production via the builder, render graph, renderer and draw stages, shared frame declaration, leaf frame input and compiled-record contracts, and the RHI-to-project-log forwarding sink | `core`, `engine`, `asset` | glm |
| `app-model` | `Source/App/Model` (`AppModel`) | `lmx::app` | ImGui/SDL/Metal-free editor logic: options, selection, workspace schema, actions, performance and graph models, dynamic-resolution policy, capture metadata and scene session | `core`, `asset`, `engine`, `render`, `scenes` | glm |
| `app-shell` | `Source/App` outside `Model` (`App`) | `lmx::app` | SDL3, Dear ImGui, panels, the editor shell, the frame loops, `main` | `core`, `asset`, `render`, `engine`, `app-model`, `scenes` | glm, imgui, imgui-node-editor, libsdl3 |
| `tests` | `Tests` (`Tests`) | — | Unit and GPU cases for the units it may depend on | `core`, `render`, `asset`, `engine`, `app-model`, `scenes` | glm, catch2 |
| `texture-bake` | `Tools/TextureBake` (`TextureBake`) | — | The offline mip-bake entry point | `core`, `asset` | glm, stb |
| `benchmarks` | `Benchmarks` (`FrameDataBench`) | — | Paired CPU-encoding measurement harnesses | `core` | glm |

Every unit's access to the RHI is the external entry below, not a row in this table.

A top-level `pendingPaths` list names unit paths a staged move has not created yet, or has already
emptied; each entry must still be a path some unit lists verbatim, so a move commit needs no
contract edit for the gap. The contract carries none at rest: the field exists only while a
restructuring is mid-flight, and is removed once every move it covers has landed.

## External components

An external component is a foreign library Luminex consumes and does not police. Luminex checks
what its own files include from it; what the component does inside itself is the component's own
contract. `externals` in `Tools/module_contract.json` holds one entry per component:

| Field | Meaning |
|---|---|
| `kind` | `external`, so an entry cannot be read as a unit |
| `paths` | the repository paths the component owns; no unit may own a path inside them |
| `targets` | the component's build targets; no unit may claim one, and `targets` still declares each one's allowed dependency closure |
| `includeRoots` | the include roots the `"*"` allowance covers; each must be a directory inside `paths` |
| `consumers` | unit → the headers it may include, spelled as an `#include` writes them, or `"*"` |

Today there is one entry, `rhi`: paths `RojoRHI`, targets `RojoRHI`, `RojoRHIMetal4ImGui` and `RojoRHITests`,
include root `RojoRHI/Include`.

| Consumer | May include |
|---|---|
| `render`, `engine`, `app-model`, `scenes`, `benchmarks` | `"*"` |
| `app-shell` | `"*"`, and `rojoRHI/Metal4/Metal4ImGui.h` |
| `tests` | `"*"` |
| `asset`, `texture-bake` | `rojoRHI/Format.h` and `rojoRHI/TextureDesc.h`, and nothing else |

[ADR 0024](../decisions/0024-rhi-relocation-to-rojorhi.md) supersedes in part the four `rhi-*`
units of [ADR 0020](../decisions/0020-module-layering-and-units.md), every other unit's dependency
on them, and the spelling of the Asset allowance; the allowance itself stands. ADR 0020 stays
accepted and unedited as the record of how the layering was decided.

Rules the entry carries:

- A resolved include inside `paths` is an external edge, charged to the unit of the file the check
  started from. A unit absent from `consumers` may not include the component at all.
- `"*"` covers every header under `includeRoots` and nothing else, so `RojoRHI/Source`,
  `RojoRHI/Backends/.../Source` and every other component-private file stay unreachable through it. The
  optional ImGui adapter ships a second public include root; `app-shell` names its single header
  explicitly rather than widening `includeRoots`, so the wildcard does not hand the editor's ImGui
  bridge to every consumer.
- Reach stops at the component boundary. A unit including `rojoRHI/RHI.h` inherits nothing from what
  that header includes inside the component, and the component's own files are not walked for unit
  ownership, reach or line budgets — they are absent from `roots`, and excluded even if a root
  reached them.
- The component's targets are still reconciled: `RojoRHI` declares no dependency, `RojoRHIMetal4ImGui`
  declares `RojoRHI` and `ImGui`, `RojoRHITests` declares `RojoRHI`. Nothing under `RojoRHI/` can link a Luminex
  target without failing the target-closure check, which is how the standalone build stays
  standalone. `Asset`'s `forbidUndefined: rojoRHI::` still runs under `--link`.

`tests` takes no header out of the component's own suite: the repository suite keeps its own copy of
the GPU bootstrap, merged into `Tests/Support/GpuTestSupport.h` alongside its Asset, Render and
Engine helpers, so no edge reaches `RojoRHI/Tests`. The two test binaries partition the suite: a
case lives in exactly one of them, and `RojoRHITests` links the `RojoRHI` target and no other
project library.

### Directory ownership

Asset, Engine and AppModel own their directories and static targets.
`Source/App/Model` owns the shared editor models and scene session in `Options/`, `Scene/`,
`Graph/`, `Performance/`, `Console/`, `Capture/`, `Workspace/` and
`Rendering/{Settings,Temporal,Lighting,Visibility}/`.
The remaining App sources live in `Shell/` (including `main.cpp` and EditorShell partials),
`Headless/` (Screenshot, Measurement and OcclusionValidation) and `Panels/` (Scene, Inspector,
Viewport, Graph, Performance, Console and Shared). The root holds its build file only.
Workspace persistence/docking and camera input are EditorShell partials; Inspector's subject
units share private `Panels/Inspector/InspectorInternal.h`. These folders add no contract units.
App and Tests link AppModel. Tests compiles only its own C++ sources, alongside test shaders.
Render owns shared FrameDeclaration graph execution; App retains its returned record. AppModel
must not include RenderGraph.h directly or transitively; graph observers use CompiledFrameRecord.h.

### Test placement

`Tests/` mirrors `Source/` one level down, including `Render/Passes/<family>/` and
`App/Model/<feature>/`; `Engine/Asset/` stays one folder. The subject of a case is the Source
symbol whose behaviour it asserts, not the fixtures it builds, and a file sits in its subject's
folder. In a file whose cases assert several subjects, a subject group of three or more cases
splits into its own file; smaller groups stay in the file of the largest group, and ties go to
the group of the file's first case. The [R3.7 record](../milestones/r/r3.7.md#cases-outside-their-subjects-folder)
lists the cases that stay outside their subject's folder.

A GPU case that renders a full frame belongs to
the pass family whose output it asserts; `Render/Renderer/` holds only cases about `Renderer`'s
own composition, not about a pass family it composes. A case whose subject is RojoRHI stays
beside its Luminex consumer, since a Luminex commit cannot move a case into `rojo-rhi`.
`Tests/xmake.lua` adds `Tests/` itself as a private include directory and compiles every `.cpp`
under it; shared test helpers live in `Tests/Support/`, the only folder that holds headers, each
listed by the contract's `privateHeaders` for the `tests` unit and included as `Support/<Name>.h`.

## Ownership precedence

Ownership comes from one explicit map from unit to paths, never from inference:

- The longest matching path wins. A file entry beats the directory that contains it, and a nested
  directory entry beats its parent, so `Source/App/Model` carves `app-model` out of `app-shell`
  and a single file can be reassigned without moving it.
- Every source and header under a mapped root matches exactly one entry. An unmatched file is a
  failure, not an exemption; adding a file to the tree means adding it to a unit.
- The map is reconciled with xmake target membership and disagreement fails. The compilation
  database supplies compilation context only — include directories and defines — because a file
  compiled by several targets cannot establish ownership on its own.
- Every source is compiled by a target, and sources listed by targets are checked even outside
  the walked roots. A `shared-source` allowance admits its exact target set only while that set
  still includes an owning target. Every non-vendored target declares a dependency contract.
- Compilation paths resolve against the entry's working directory under the selected repository
  root. A missing compilation context is a could-not-run error, never an empty include search.

## Placement rules

- The placement rule, as [ADR 0026](../decisions/0026-core-charter-and-placement.md) states it:
  "Math and data structures with no domain meaning go to `core` whatever their consumer count. A
  domain meaning keeps code in its owning unit: a function that takes or returns a scene, asset,
  render or app type, or that fixes a constant belonging to one (the 16×9×24 froxel grid, the
  light-row layout), is not generic, and the generic part is cut out from under it rather than the
  whole moved. Every other helper keeps ADR 0020's two-consumer rule." Every other helper:
  consumers in two or more units, sharing one contract, goes to `core`; one consumer keeps it
  local.
- Extraction preserves each caller's contract. Two helpers with different contracts keep different
  names and a test that pins the difference. A behaviour change is its own commit with its own
  test, never part of a move.
- CPU decode and validation finish in `asset`. GPU creation and upload happen in `engine` or
  `render` on the render-owning thread, as the
  [engineering conventions](engineering.md) require.
- Vocabulary shared by a producer and a consumer lives in the lower of the two units, in its own
  leaf header, never inside the orchestrator that consumes it.
- Tests link libraries, never loose sources from another unit.
- Every project header compiles standalone: a generated translation unit that includes only that
  header, compiled with its owning target's include paths, must build. The Source header check
  keeps each target's own flags and does not add `-Werror`, since it is checking inclusion
  completeness rather than a warning-free public surface. The RHI component owns the stricter
  dependency-free check of its own public headers, in `RojoRHI/Tools/check_rhi_headers.py`.

## Include-level form

The rules above are checked as include edges:

- A quoted include resolves relative to the including file first, then against the owning target's
  include directories. It is a project edge from the includer's unit to the unit that owns the
  resolved file.
- An angle include resolving to a project header is a project edge, just like a quoted include.
  Otherwise, resolution against package and `ThirdParty` roots produces a third-party edge.
  Its name comes from where it resolved: a header under an xrepo package directory takes that
  package's name — spdlog, glm, cgltf, stb, catch2, libsdl3 — and a header under `ThirdParty/<dir>`
  takes the directory name — imgui, imgui-node-editor, metal-cpp. The rule covers subdirectories, so
  `imgui/backends` headers such as `imgui_impl_sdl3.h` and `imgui_impl_metal4.h` are imgui, and the
  Metal, MetalFX, QuartzCore and Foundation headers, which resolve under `ThirdParty/metal-cpp`, are
  metal-cpp. A quoted include that resolves to no project file is resolved the same way. A resolved
  path under no package or `ThirdParty` root falls back to `thirdPartyPrefixes`, keyed by include-spec
  prefix; today that covers only `SDL3/` → libsdl3, because Homebrew installs SDL3 under
  `/opt/homebrew/include` rather than an xrepo package directory. An unresolved angle include also
  takes this fallback; an unresolved quoted spec is treated as a system include.
- The vendored `ImGui` and `ImGuiNodeEditor` xmake targets build third-party packages, not units.
  Ownership reconciliation skips them, and a unit that links one still needs the package in its
  third-party set.
- `forbidHeaders` names exact repository-relative headers a unit must never reach, including
  through an otherwise allowed unit. The checker reports the include chain and rejects stale paths.
- `privateHeaders` enumerates canonical repository-relative headers owned by each unit. Foreign
  direct or transitive inclusion fails even through an allowed public header; allowances cannot
  widen private visibility. Missing, duplicate, malformed or foreign-owned entries fail. Empty
  lists explicitly mean no private headers. Public API documentation excludes these headers, but
  standalone compilation and file-envelope checks still cover them.
- Project reach is transitive. A unit reaches every unit its includes reach, at any depth, so an
  `app-model` header cannot borrow ImGui by including a shell header that includes it.
- Third-party reach is direct. A package is charged to the unit whose own file names it, not to
  that file's includers, so `core` including spdlog does not spend spdlog everywhere.
- External reach is neither transitive nor an exception to the unit table: it is the separate
  `externals` contract above, matched per header. `asset` and its `texture-bake` consumer may
  include `rojoRHI/Format.h` and `rojoRHI/TextureDesc.h` and nothing else — not the target, not the
  umbrella `rojoRHI/RHI.h`, and not any other header those two happen to include. TextureBake reaches
  them through Asset's public mip-chain structure; it still links Core and Asset only.
- `headers` remains a per-unit, per-header allowance against another unit, and is the only
  exception to the unit table. No unit needs one today, so no unit carries the field.

## Private implementation boundaries

The exact inventory is `Tools/module_contract.json`. Render's exposure/bloom/display owners,
range/temporal implementation declarations are private. Shared `SceneTables.h` row layouts are
public Engine vocabulary that Render consumes; scene identity stores and table ownership stay in
Engine. Engine's environment assembly is private; uploads used by tests remain public. Every shell/panel header
is private to App, while AppModel's shared model headers remain public. Test and benchmark
fixtures are private to their units. Core and Asset have no private entries; the RHI component
keeps its own private inventory, which Luminex neither reads nor needs: a file outside the entry's
`includeRoots` is unreachable from Luminex whether the component calls it private or not. Public
Renderer uses incomplete stage owners with out-of-line destruction.

Root `xmake.lua` includes unit-local target definitions; reusable shader rules, dependency setup
and maintenance tasks live under `xmake/`. The RHI component keeps the same shape one level down:
`RojoRHI/xmake.lua` carries root settings for a standalone configure, `RojoRHI/xmake/targets.lua` is the
one file the repository root includes, and `RojoRHI/xmake/` owns the component's own shader rule and
dependency setup. Slang entry points stay at `Shaders/Passes/<family>/`, common modules
at `Shaders/Common/` and oracles at `Shaders/Tests/`; the RHI component owns a second tree of its
own smoke shaders at `RojoRHI/Shaders/Tests/`. `check_shader_imports.py` enforces each tree's import
boundary and basename uniqueness. These paths do not change runtime shader basenames.

## Asset independence

`asset` is CPU-only content and must stay linkable without a GPU. Three layers prove it, and the
weakest one is deliberately last:

1. **Include layer.** Its consumer entry admits the format and descriptor headers only, so
   `rojoRHI::Device`, `rojoRHI::Texture` and `rojoRHI::Buffer` are not nameable from `asset`. This is the
   layer that actually holds the boundary.
2. **Framework layer.** `check_link`'s `frameworks` entry checks a target's own linked frameworks
   (`otool -L`) against its allowed set — today only `TextureBake`'s empty set, so it may link
   neither the RHI target's frameworks nor Metal. This check is vacuous for a static-library
   target: a static archive links nothing at build time, so it names no framework regardless of
   what its sources reference; it only proves something for an executable or shared target like
   `TextureBake`.
3. **Archive layer.** A `forbidUndefined` entry checks a target's built archive or binary for
   undefined symbols (`nm -u`) starting with a forbidden prefix — the layer a static library like
   `asset` needs, since the framework layer cannot see it. `Asset` declares
   `forbidUndefined: rojoRHI::`. On its own this proves little: the RHI's API is virtual interfaces,
   so a caller reaches it through a vtable without leaving an undefined symbol behind. It would see
   non-virtual RHI symbols only and be a backstop under the include and framework layers, never a
   substitute for them.

## Core's archive check

A `forbidUndefined` entry on the `Core` archive uses the same archive-layer technique as `Asset`'s
`rojoRHI::` check above, aimed at every unit above it instead of one: `Core` forbids
`lmx::asset::`, `lmx::engine::`, `lmx::scenes::`, `lmx::render::`, `lmx::app::` and `rojoRHI::`.
`forbidUndefined` takes a list of prefixes, not one.

## Render depends on engine, not the reverse

Two checks hold the render-on-engine edge [ADR 0025](../decisions/0025-engine-subsystem-and-render-on-engine.md)
states: the unit table rejects any `engine` file that includes a Render header, and
`forbidUndefined: lmx::render::` and `lmx::scenes::` entries on the `Engine` archive fail `--link`
if either names an undefined symbol — the same archive-layer technique `Asset`'s `rojoRHI::` check
above uses, aimed at Render and the catalog unit instead of the RHI component. Render's reach into
the scene itself (`Engine/Scene/Scene.h`), beyond the `Engine/View/`, `Engine/Lights/`,
`Engine/Geometry/` and `Engine/Material/` vocabulary and the `Engine/Scene/` `SceneTables.h`,
`DrawItem.h` and `MotionClass.h` it includes, is confined to one translation unit,
`Render/Renderer/SceneViewBuilder.cpp`; review holds that, not the checker.

## Review budgets

Per-file line budgets are review candidates, never failures: 1,000 lines for production sources and
headers, 1,500 for test sources. The checker reports files over budget so a reviewer looks at them;
it does not block. Responsibility justifies a split — a file that does one thing at 1,200 lines
stays, and a file that does three at 600 does not. Splitting to satisfy a number, with no
responsibility named for each part, is not an improvement.

## Allowlist policy

The checker reads an allowlist from one checked-in file, currently empty. The allowlist is a
record of migration debt, not a configuration surface:

- Each entry names the file or target it applies to, the forbidden edge, a reason, and an `until`
  field naming the slice that removes it. All four are required, and the entry is scoped to that one
  edge: one file-to-unit/package reach or one target-to-dependency/framework edge, never every
  file in a unit or directory or every consumer of a package.
- An entry that matches no current violation fails the check. A stale allowance is an error, not a
  harmless leftover, so fixing an edge forces the entry out in the same change.
- The allowlist shrinks by default and grows only with a plan-stated `until`: a slice that would add
  an entry either fixes the edge instead, or records in its plan why the edge is temporary and which
  slice removes it, and writes that slice into `until`.
- An empty allowlist for a unit is that unit's exit gate. Widening the allowlist is not how a
  crossing gets approved; a rule that no longer fits opens the next refactoring milestone.
- Link-only kinds (`framework`) are exempt from the unused-entry check when `--link` is not
  running, since only the link pass can confirm they are still needed. The `header` kind is a
  foreign entry here: it is never marked used by this checker, because `check_source_headers.py`
  owns it. Every allowlisted header is still checked; only a failed compilation consumes the entry.
  A repaired header therefore fails with an unused allowance until that allowance is removed.
