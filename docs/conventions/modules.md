# Module Contract

**Status**: Accepted

The repository is a set of named units with an explicit dependency set each. A unit may depend on
the units and third-party packages listed for it and on nothing else; anything absent from its row
is forbidden, and the policy checker rejects it. The layering, the unit names and the namespace
renames are decided in [ADR 0020](../decisions/0020-module-layering-and-units.md); the migration
order lives in the [refactoring roadmap](../roadmap/codebase-refactoring.md).

## Units

`Paths` names the unit's directory and its build target. Three units are mid-migration: their
target directory does not exist yet, so the [transitional file lists](#transitional-file-lists)
below define their membership until the move that creates the directory.

| Unit | Paths (target) | Namespace | Owns | May depend on | Third-party |
|---|---|---|---|---|---|
| `core` | `Source/Core` (`Core`) | `lmx` | Logging, assertions, alignment, colour transfer functions; later, helpers that reach two consumers with one contract | — | spdlog, glm |
| `rhi-public` | `RHI/Include` (`RHI`) | `lmx::rhi`, `lmx::rhi::metal4` | API-neutral GPU contracts, compiled standalone | — | — |
| `rhi-impl` | `RHI/Source` (`RHI`) | `lmx::rhi`, `lmx::rhi::debug` | Backend-neutral shared implementation and validation | `core`, `rhi-public` | — |
| `metal4-backend` | `RHI/Backends/Metal4/Source` (`RHI`) | `lmx::rhi::metal4` and nested | The only backend: devices, command lists, resources, swapchain, temporal scaler, capture | `core`, `rhi-public` | metal-cpp |
| `imgui-adapter` | `RHI/Backends/Metal4/ImGui` (`RHIMetal4ImGui`) | `lmx::rhi::metal4` | Optional Dear ImGui renderer glue over the Metal 4 backend | `core`, `rhi-public`, `metal4-backend` | metal-cpp, imgui |
| `asset` | `Source/Asset` (`Asset`) | `lmx::asset` | CPU decoding, texture baking, IBL generation, procedural geometry, animation clip data and sampling, the asset error domain, repository asset discovery, SHA-256 | `core` | glm, cgltf, stb |
| `render` | `Source/Render` (`Render`) | `lmx::render` | Camera, mesh, render graph, renderer and passes, plus the frame input contract in its own leaf header | `core`, `rhi-public` | glm |
| `scene` | `Source/Scene` (`Scene`) | `lmx::scene` | GPU-owning scenes: uploads, catalog and `SceneId`, environment rig, labs, San Miguel, playback, `SceneView` production, initial camera | `core`, `rhi-public`, `asset`, `render` | glm |
| `app-model` | `Source/App/Model` (`AppModel`) | `lmx::app` | ImGui/SDL/Metal-free editor logic: options, selection, workspace schema, actions, performance and graph models, dynamic-resolution policy, capture metadata | `core`, `rhi-public`, `asset`, `scene`, `render` | glm |
| `app-shell` | `Source/App` outside `Model` (`App`) | `lmx::app` | SDL3, Dear ImGui, panels, the editor shell, the frame loops, `main` | `core`, `rhi-public`, `rhi-impl`, `metal4-backend`, `imgui-adapter`, `asset`, `render`, `scene`, `app-model` | glm, imgui, imgui-node-editor, libsdl3 |
| `tests` | `Tests` (`Tests`) | — | Unit and GPU cases for the units it may depend on | `core`, `rhi-public`, `render`, `asset`, `scene`, `app-model` | glm, catch2 |
| `texture-bake` | `Tools/TextureBake` (`TextureBake`) | — | The offline mip-bake entry point | `core`, `asset` | glm, stb |
| `benchmarks` | `Benchmarks` (`FrameDataBench`) | — | Paired CPU-encoding measurement harnesses | `core`, `rhi-public` | glm |

`app-shell` reaches the backend and the adapter because it creates the device and the editor's
ImGui bridge; no other unit above `rhi-public` may name a backend or adapter header, where "backend
header" means any header under `RHI/Backends`. The Metal 4 extension headers
`RHI/Include/RHI/Metal4/Metal4Capture.h` and `RHI/Include/RHI/Metal4/Metal4FrameData.h` are
`rhi-public`, not backend headers: they live under the public include directory and pass the
dependency-free standalone header check like every other public header.

`tests` reaches the GPU through `rhi-public` alone — `createDevice` in `RHI/Include/RHI/Device.h`
hands back the interface, and no test names a backend, adapter or `RHI/Source` header. That the
Tests target links the `RHI` target is the link-level view, a target's dependency closure, which the
link checks own; it is not an include edge and does not widen this row.

### Transitional file lists

Until the moves that create `Source/Asset`, `Source/Scene` and `Source/App/Model`, these lists are
the ownership map for those three units. The header and its implementation always share a unit.

- `asset` — `Source/Engine/`: `Asset`, `Color`, `DdsLoader`, `GltfLoader`, `HdrEnvironment`, `Ibl`,
  `PngImage`, `TextureBake`, `GeometryGenerator`, `SceneAnimation` (`.h` and, where present,
  `.cpp`).
- `scene` — `Source/Engine/`: `Scene`, `SceneLibrary`, `SceneEnvironment` (`.h` and `.cpp`),
  `MaterialLab.cpp`, `TemporalLab.cpp`, `SanMiguel.cpp`.
- `app-model` — `Source/App/`: `AppOptions`, `CaptureMetadata`, `DynamicResolution`,
  `EditorActions`, `EditorSelection`, `ExposureReset`, `FrameRecordRing`, `GraphInspectorModel`,
  `GraphLayout`, `GraphNodeModel`, `PassTimingHistory`, `PerformanceModel`, `TemporalEditorState`,
  `WorkspaceModel` (`.h` and `.cpp`), plus `DirectionalLightRole.h` and `EditorRenderSettings.h`.
- `app-shell` — everything else under `Source/App/`, including `Panels/`, `EditorShell`,
  `Screenshot` and `main.cpp`.

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

## Placement rules

- A helper with no domain meaning and consumers in two or more units, sharing one contract, goes to
  `core`. One consumer keeps it local; a domain meaning keeps it in the owning unit.
- Extraction preserves each caller's contract. Two helpers with different contracts keep different
  names and a test that pins the difference. A behaviour change is its own commit with its own
  test, never part of a move.
- CPU decode and validation finish in `asset`. GPU creation and upload happen in `scene` or
  `render` on the render-owning thread, as the
  [engineering conventions](engineering.md) require.
- Vocabulary shared by a producer and a consumer lives in the lower of the two units, in its own
  leaf header, never inside the orchestrator that consumes it.
- Tests link libraries, never loose sources from another unit.
- Every project header compiles standalone: a generated translation unit that includes only that
  header, compiled with its owning target's include paths, must build. `rhi-public` keeps a
  stricter dependency-free command line, because its headers are required to need nothing; the
  Source header check instead keeps each target's own flags and does not add `-Werror`, since it is
  checking inclusion completeness rather than a warning-free public surface.

## Include-level form

The rules above are checked as include edges:

- A quoted include resolves relative to the including file first, then against the owning target's
  include directories. It is a project edge from the includer's unit to the unit that owns the
  resolved file.
- An angle include resolves against the package and `ThirdParty` roots and is a third-party edge.
  Its name comes from where it resolved: a header under an xrepo package directory takes that
  package's name — spdlog, glm, cgltf, stb, catch2, libsdl3 — and a header under `ThirdParty/<dir>`
  takes the directory name — imgui, imgui-node-editor, metal-cpp. The rule covers subdirectories, so
  `imgui/backends` headers such as `imgui_impl_sdl3.h` and `imgui_impl_metal4.h` are imgui, and the
  Metal, MetalFX, QuartzCore and Foundation headers, which resolve under `ThirdParty/metal-cpp`, are
  metal-cpp. A quoted include that resolves to no project file is resolved the same way. For an
  angle include only, a resolved path under no package or `ThirdParty` root — or one that resolves
  nowhere at all — falls back to the contract's `thirdPartyPrefixes` table, keyed by include-spec
  prefix; today that covers only `SDL3/` → libsdl3, because Homebrew installs SDL3 under
  `/opt/homebrew/include` rather than an xrepo package directory. A quoted include never takes this
  fallback: an unresolved quoted spec is a system include, not a third-party edge.
- The vendored `ImGui` and `ImGuiNodeEditor` xmake targets build third-party packages, not units.
  Ownership reconciliation skips them, and a unit that links one still needs the package in its
  third-party set.
- Project reach is transitive. A unit reaches every unit its includes reach, at any depth, so an
  `app-model` header cannot borrow ImGui by including a shell header that includes it.
- Third-party reach is direct. A package is charged to the unit whose own file names it, not to
  that file's includers, so `core` including spdlog does not spend spdlog everywhere.
- Header allowances are per unit and per header, and are the only exception to the unit table.
  `asset` may include `RHI/Format.h`; the texture descriptor header extracted in R1.2 joins that
  allowance when it exists. An allowance grants those headers only — not the target, not the umbrella
  `RHI/RHI.h`, and not any other header the allowed header happens to include.

## Asset independence

`asset` is CPU-only content and must stay linkable without a GPU. Three layers prove it, and the
weakest one is deliberately last:

1. **Include layer.** The header allowance admits format and descriptor headers only, so
   `rhi::Device`, `rhi::Texture` and `rhi::Buffer` are not nameable from `asset`. This is the
   layer that actually holds the boundary.
2. **Framework layer.** `check_link`'s `frameworks` entry checks a target's own linked frameworks
   (`otool -L`) against its allowed set — today only `TextureBake`'s empty set, so it may link
   neither the RHI target's frameworks nor Metal. This check is vacuous for a static-library
   target: a static archive links nothing at build time, so it names no framework regardless of
   what its sources reference; it only proves something for an executable or shared target like
   `TextureBake`.
3. **Archive layer.** A `forbidUndefined` entry checks a target's built archive or binary for
   undefined symbols (`nm -u`) starting with a forbidden prefix — the layer a static library like
   `asset` needs, since the framework layer cannot see it. No contract target declares
   `forbidUndefined` yet, so this check runs on nothing today; R1.2 adds `Asset`'s entry once the
   target exists. On its own it would prove little even then: the RHI's API is virtual interfaces,
   so a caller reaches it through a vtable without leaving an undefined symbol behind. It would see
   non-virtual RHI symbols only and be a backstop under the include and framework layers, never a
   substitute for them.

## Review budgets

Per-file line budgets are review candidates, never failures: 1,000 lines for production sources and
headers, 1,500 for test sources. The checker reports files over budget so a reviewer looks at them;
it does not block. Responsibility justifies a split — a file that does one thing at 1,200 lines
stays, and a file that does three at 600 does not. Splitting to satisfy a number, with no
responsibility named for each part, is not an improvement.

## Allowlist policy

The current tree does not satisfy the contract yet, so the checker reads an allowlist from one
checked-in file. The allowlist is a record of debt, not a configuration surface:

- Each entry names the file or target it applies to, the forbidden edge, a reason, and an `until`
  field naming the slice that removes it. All four are required, and the entry is scoped to that one
  edge — never a whole unit, directory or package.
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
  owns it.
