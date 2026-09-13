# Codebase Refactoring

**Status**: Accepted

Part III of the [rendering roadmap](../roadmap.md): R-series milestones restructure the codebase
between rendering milestones without changing rendered output, RHI semantics or shipped
behaviour. R1 follows [M6.5](rendering-foundations.md#m65--display-boundary-and-edr-evaluation)
and runs before the [interface gate B](rendering-foundations.md#m6--temporal-and-display-foundation)
review that admits [M7.1](gpu-driven-hybrid-rendering.md#m71--gpu-scene-foundation), so that
review reads the restructured tree; R1 prepares the review and approves nothing gate B owns.
Shared delivery rules stay in the roadmap entry.

The [foundation spec](../specs/2026-08-07-luminex-upgrade-design.md) assigned `Source/Core`
logging, assertions, time and platform utilities; only logging, assertions and one alignment
helper arrived, and later helpers settled wherever they were first needed. Six rendering milestones
then grew `Renderer`, `RenderGraph`, the editor shell and their tests as single large units. M7
adds GPU scene data, identities and visibility to exactly these seams; restructuring first keeps
those slices small and their review about rendering rather than about where code lives.

## Observed structure before R1.1

The inventory below motivated R1. [R1.1](../milestones/r1.1.md) has since added enforcement and
moved the adapter sources; [R1.2](../milestones/r1.2.md) separates Asset and Scene and extracts
the shared leaf contracts. The remaining structural work belongs to R1.3–R1.4.
The runtime had five build targets in a chain, `Core → RHI → Render → Engine → App`, plus Tests,
TextureBake and FrameDataBench:

| Module | Files | Observation |
|---|---|---|
| `Source/Core` | 4 | `Log`, `Assert`, `alignUp`. spdlog is a public dependency of every target |
| `RHI/` | 46 | Own build description, public headers compiled standalone, backend private: the clearest boundary. The two ImGui adapter sources sit in the backend directory but belong to another target |
| `Source/Render` | 26 | `Renderer.cpp` is about 2,000 lines with a 900-line `declarePasses`; `RenderGraph.cpp` about 1,950 with a 450-line transition derivation; `Renderer.h` carries the frame input contract and the orchestrator together and is included by 19 files |
| `Source/Engine` | 27 | Two layers in one: decoding, baking, IBL and clip data need no GPU; scenes, catalog and labs own GPU meshes and textures. `Scene.h` includes `Render/Renderer.h`, which is why Engine sits above Render |
| `Source/App` | 45 | 14 pure source files that Tests compile from an explicit list; `main.cpp`'s 300-line loop and `Screenshot.cpp`'s 180-line loop repeat device, renderer, scene and playback setup |
| `Tests/` | 63 | Flat; `RenderGraphTests.cpp` and `GpuTemporalTests.cpp` exceed 3,000 lines each |

Consequences of the missing standard:

- `srgbToLinear` exists twice (`Engine/Color.h`, `Render/ColorTransfer.h`) because Render cannot
  include Engine. Two `alignUp` functions carry different contracts: Core's requires a power-of-two
  alignment, `RenderGraph.cpp`'s accepts any alignment and treats zero as a no-op. `divRoundUp` is
  defined in `Renderer.cpp` and `TemporalResolve.cpp`. JSON string escaping exists three times
  (`CaptureSchema`, `CaptureMetadata`, `TextureBake`); repository asset discovery four times
  (`Scene.cpp`, `SceneLibrary.cpp`, `MaterialLab.cpp`, `SanMiguel.cpp`). Every file writer has its
  own error contract, and only `CaptureSchema` writes atomically.
- `render::AlphaMode` lives in Render because the glTF loader must name it; `Ibl.h` and
  `DdsLoader.h` mix CPU generation with `rhi::Device` upload; the BMP encoder lives in App while
  PNG lives in Engine; `cameraFromScene` is declared by the editor shell and used by the headless
  capture path and two panels.
- `TextureBake`, a CPU tool, links Render, the RHI and the Metal backend to reach the decoders.
- Tests and App compile the same App sources separately, and nothing checks that those files stay
  free of ImGui, SDL and Metal.
- Only RHI public headers are compiled standalone. Dependency direction is described in the
  [architecture overview](../architecture/overview.md) and enforced nowhere.

## Target module contract

R1 replaces the chain with a checked layering. Each unit has one sentence of ownership and an
explicit dependency set; anything not listed is forbidden and the policy checker rejects it.

| Unit | Namespace | Owns | May depend on |
|---|---|---|---|
| `Source/Core` | `lmx` | Logging, assertions, alignment, colour transfer functions; later, helpers that reach two consumers with one contract | spdlog, glm |
| `RHI/Include` | `lmx::rhi` | Unchanged API-neutral contracts; `RHI/Source` shared implementation, the Metal 4 backend and the ImGui adapter are separate units with their own allowed sets | Public headers depend on nothing; implementation units on Core |
| `Source/Asset` | `lmx::asset` | CPU decoding (glTF, DDS, Radiance HDR, PNG, BMP), texture baking, IBL generation, procedural geometry, animation clip data and sampling, the asset error domain, repository asset discovery, SHA-256 | Core; `RHI/Format.h` and the texture descriptor header R1.2 extracts; never a `Device`, `Texture` or `Buffer` |
| `Source/Render` | `lmx::render` | Unchanged responsibilities, plus the frame input contract (`Material`, `AlphaMode`, `DrawItem`, `DirectionalLight`, `SceneView`) in its own leaf header | Core, RHI |
| `Source/Scene` | `lmx::scene` | GPU-owning scenes: uploads, catalog and `SceneId`, environment rig, labs, San Miguel, playback (animation clock, camera track, previous transforms), `SceneView` production, initial camera | Core, RHI, Asset, Render |
| `Source/App/Model` | `lmx::app` | ImGui/SDL/Metal-free editor logic: options, selection, workspace schema, actions, performance and graph models, dynamic-resolution policy, capture metadata | Core, RHI, Asset, Render, Scene |
| `Source/App` | `lmx::app` | SDL3, Dear ImGui, panels, the editor shell, the frame loops, `main` | Everything above, ImGui, RHIMetal4ImGui |

Tests may depend on every unit except the App shell and panels; `Tools/TextureBake` on Core and
Asset; `Benchmarks` on Core and RHI. Third-party headers map to units: spdlog to Core; glm to
every unit but RHI public headers; cgltf and stb to Asset; Catch2 to Tests; ImGui, the node editor
and SDL to the App shell, panels and the ImGui adapter; Metal, MetalFX, QuartzCore, Foundation and
metal-cpp to the backend and the adapter.

Placement rules the convention states and the checker approximates:

- A helper with no domain meaning and consumers in two or more units, sharing one contract, goes
  to Core. One consumer keeps it local; a domain meaning keeps it in the owning unit.
- Extraction preserves each caller's contract. Two helpers with different contracts keep
  different names and a test that pins the difference. A behaviour change is its own commit with
  its own test, never part of a move.
- CPU decode and validation finish in Asset; GPU creation happens in Scene or Render on the
  render-owning thread, as the [engineering conventions](../conventions/engineering.md) require.
- Vocabulary shared by a producer and a consumer lives in the lower of the two units in its own
  leaf header, never inside the orchestrator that consumes it.
- Tests link libraries, never loose sources. Every project header compiles standalone.
- `Engine` is retired: Asset and Scene name what each half was. A new ADR records the layering,
  the names, the namespace renames and the descriptor-header decision.

## R1 — Module boundaries and shared foundations

**Outcome:** the module contract above is written, enforced and true for the units M7 touches:
Asset and Scene replace Engine, the frame input contract is a leaf header, App's pure logic is a
library, the renderer's draw stages are separable, and no rendered output, RHI contract or shipped
behaviour changed. R1.1–R1.4 are the gate B prerequisite; R1.5 collects optional consolidation and
decomposition accepted on their own and never required by gate B.

**Deliver:** contract, ADR and checkers (R1.1); the Asset/Scene split with `Core/Color.h`, the RHI
descriptor header and the frame input header (R1.2); the App model library and shared scene session
(R1.3); the renderer's draw-stage seams and compiled-record header (R1.4); optional items (R1.5).

**Comparison protocol:** every R1 change is a pure refactor, proved by comparing each commit with
its parent, both built in one session on one device, with both builds' binary and shader hashes
and every output kept in the slice's evidence bundle outside the source tree.

- Byte-stable artifacts are byte-identical from one run each: graph dumps, capture manifests and
  PNG metadata, baked DDS and PNG, capture schema JSON and bake manifests.
- Tests compare by case, not by file or inventory: every pre-existing case remains present under
  its name or a rename the plan records, and passes; new cases are additive; the checkpoint A
  filter selects the same cases. Runs are validation-clean.
- GPU captures compare semantically through the capture dump: pass, encoder and resource labels,
  pass order and uniform layouts are equal, timings excluded.
- Rendered images use the strict parity matrix on both builds for alternating rounds. The plan
  fixes one total maximum of rounds before measurement, extensions included (M6.5 used eight). A
  case is unchanged when every hash the refactored build produced also occurred from the parent
  build in that session. A hash seen only from the refactored build is unresolved, not attributed:
  the commit merges only once a cause is found and fixed or the parent produces that hash within
  the maximum; at the maximum, sampling stops and the slice records the blocker. `reference.json`,
  the strict runner's failure behaviour and the
  [M6.5 exception](../milestones/m6.5.md#open-parity-investigation) stay as they are: no retry
  until passing, no widened exception, no re-baseline.
- Pass labels, schedule and transient assignments do not change. No shader, uniform layout or
  `SceneView` field changes. An RHI public header changes only by moving existing declarations
  into a new leaf header the existing header includes, with no signature, semantic or umbrella
  change, and the RHI header check and checkpoint A green.
- One move per commit, renames separate from edits; the namespace rename is one mechanical commit
  reproducible from a recorded substitution; formatting and policy green at every commit.
  Performance is not a claim of R1.

**Sequence:** R1.1 → R1.2 → R1.3 → R1.4; R1.5 items may follow R1.1 individually. Each slice has
its own plan; only one plan is active.

**Exit gate:** R1.1–R1.4 accepted: the checker's allowlist is empty for Asset, Scene, Render and
`App/Model`; every project header compiles standalone; Tests link the model library and list no
App sources; the Asset archive has no undefined references into `lmx::rhi`, and `TextureBake`
links neither Render nor a Metal framework; the architecture overview, frame walkthrough, README
directory table and `AGENTS.md` describe the new layout; the protocol held on every commit.

**Defer:** any new rendering feature, shader change or RHI capability; GPU-scene identity and
tables (M7.1 owns them); a second backend or publishing the RHI standalone; a shared cross-domain
error type; a logging facade or precompiled headers without a compile-time measurement; an entity
system or general scene database; changing the build system, test framework or frames in flight.

## R1.1 — Module contract and enforcement

**Outcome:** the layering is a convention with a checker, and the current tree's violations are an
explicit, shrinking allowlist.

**Deliver:**

- `docs/conventions/modules.md`: the contract, units and third-party map above; an explicit
  ownership map from unit to directories and headers; the include-level form of each rule; and
  per-file line budgets the checker reports as review candidates, never failures (recommended
  1,000 production and 1,500 test lines). Responsibility justifies a split.
- `Tools/check_module_deps.py`: reconciles the ownership map with xmake target membership and
  fails on disagreement; the compilation database supplies compilation context only, because a
  file compiled by several targets cannot establish ownership (the two ImGui adapter sources move
  into the adapter's own directory so map and targets agree); resolves quoted includes relative
  to the including file and then the target's include directories, and angle includes against
  the third-party map; follows project includes transitively so a model header cannot reach ImGui
  through the shell; rejects edges outside the matrix; reads its allowlist from one checked-in
  file; has unit tests under `Tools/tests/`; runs in `xmake policy` and CI.
- Link-level checks beside it, since textual allowlists prove nothing about linking: each
  target's declared dependency closure (`xmake lua Tools/xmake_targets.lua`) against the contract; an archive symbol
  check that Asset has no undefined `lmx::rhi` references; `TextureBake` links no Metal framework.
- Standalone headers: a generated per-header translation unit compiled by the build for every
  header under `Source/`, inheriting each target's include paths. The RHI check keeps its
  dependency-free command line because it proves the public headers need nothing.
- The ADR: target layering, unit names, namespace renames, the descriptor-header decision.

**Exit gate:** checker, link checks and header check pass on the otherwise unchanged tree with the
allowlist naming every current violation and the ownership map agreeing with target membership;
CI runs them; the ADR is accepted before R1.2 moves a file.

**Defer:** source moves other than the adapter sources; function-length checks; promoting
clang-tidy to blocking.

## R1.2 — Asset and Scene replace Engine

**Outcome:** CPU content and GPU scenes are separate units with the dependency direction the
contract states, and Render's frame input contract is a leaf header.

**Deliver:**

- First commits: `Core/Color.h` (sRGB transfer, scalar and glm overloads; Core takes glm as a
  public package) replaces `Engine/Color.h` and `Render/ColorTransfer.h`. A texture descriptor
  header such as `RHI/TextureDesc.h` receives `TextureKind`, `TextureDesc`, `TextureMip` and the
  range, view and copy descriptors from `Texture.h`, which then includes it; the umbrella and
  every caller are unchanged. The ADR records why Asset-owned duplicate descriptors and an
  unchecked include restriction were rejected.
- A leaf header such as `Render/SceneView.h`: `Material`, `AlphaMode`, `DrawItem`,
  `DirectionalLight`, `ShadowFilter`, `TemporalSettings`, `TemporalStatus` and `SceneView` move out
  of `Renderer.h` unchanged; `Renderer.h` keeps the orchestrator and the pass constants.
- `Source/Asset`: the asset error domain, `GltfLoader` with its own glTF alpha-mode enumeration
  (BLEND still rejected at load, [ADR 0018](../decisions/0018-masked-material-coverage.md)),
  `DdsLoader` decoding, `HdrEnvironment`, the CPU half of `Ibl`, `PngImage`, the BMP encoder from
  `Screenshot.cpp`, `TextureBake` with its SHA-256 (its only production consumer is the bake
  tool), `GeometryGenerator`, `SceneAnimation` clip data and sampling, and one repository asset
  discovery function. Asset's build takes the RHI public include directory without an RHI
  dependency and may include `RHI/Format.h` and the descriptor header only.
- `Source/Scene`: `Scene`, `SceneLibrary`, `SceneEnvironment`, the labs, San Miguel, the DDS,
  cubemap and IBL upload functions, `cameraFromScene`, and playback: animation clock, camera-track
  follow, `resetMotion`/`commitFrame` and `SceneView` production.
- Build: two static targets replace `Engine`; `TextureBake` links Core and Asset; Tests keep their
  groups, with `[engine]` split into `[asset]` and `[scene]` and the renames recorded.
- Namespaces `lmx::asset` and `lmx::scene` replace `lmx::engine` in one mechanical commit after
  the moves; the architecture overview, frame walkthrough, `AGENTS.md` and the README directory
  table follow in the same slice.

**Exit gate:** the allowlist is empty for Asset, Scene and Render; the Asset archive has no
undefined `lmx::rhi` references and `TextureBake` links neither Render nor a Metal framework;
every prior scene, loader, IBL, bake and PNG case passes under its recorded name; the protocol
holds; the RHI header check and the checkpoint A filter are unchanged.

**Defer:** an asset database, streaming or residency (M11); scene identity or GPU tables (M7.1);
any loader feature; skinning or morph targets; Core file, JSON and parsing helpers (R1.5).

## R1.3 — App model library and shared scene session

**Outcome:** App's pure logic is a linked library, and the editor and headless paths share scene
preparation and frame declaration while keeping their own scheduling.

**Deliver:**

- `Source/App/Model/` compiled as a static target linked by App and Tests: options, capture
  metadata, selection, workspace schema, actions, performance and graph models, pass timing
  history, frame record ring, dynamic-resolution policy, temporal editor state, exposure reset and
  light roles. Tests list no App sources; the checker keeps Model free of ImGui, SDL and Metal.
- A scene session shared by the editor shell and the headless path: activation and camera from
  the scene, playback advance (clock, camera track, animate), view production and motion
  reset/commit; and a frame declaration step: transient pool begin, graph creation and pooling
  flag, `declarePasses`, record retention. The editor keeps drawable starvation, three-frame
  pacing, the retired-timing join, resize and platform-window rendering after present; the
  headless path keeps its wait per frame and its warmup timing. The session header states its
  invariants: `SceneView` borrows the caller's item span and scene data for one declare and
  execute; destruction order is shell, renderer, transient pool, device, then the SDL-owned layer;
  the transient pool outlives every per-frame graph and rotates on the slot `beginFrame` retired;
  motion commits only after execute accepted the frame and resets on activation or a discontinuity.
- `EditorShell` keeps panel coordination and input and composes the session and the ImGui
  settings handler.

**Exit gate:** the Tests target compiles `Tests/**` only; screenshots, sequences, manifests and
PNG metadata satisfy the protocol; scripted editor runs under `LMX_MAX_FRAMES` (with its resize
hook) and `LMX_CAPTURE_AT_FRAME` are validation-clean; scene switch, playback, camera cut and
dynamic resolution are checked in the editor and recorded as evidence; the session has unit tests.

**Defer:** a separate headless executable; new CLI options; further OS windows beyond the Render
Graph; undo, persistence or asset browsing.

## R1.4 — Renderer seams for M7

**Outcome:** the stages M7.1 and M7.2 rewrite are separable units, and App's graph models depend
on the compiled record rather than the graph builder.

**Deliver:** the shadow and scene draw stages (pipelines, per-object frame data binding, draw
encoding, masked variants) leave `Renderer.cpp` on the `TemporalResolve` pattern, with `Renderer`
as the composition root and pass labels, order, uniform layouts and transients unchanged;
`CompiledFrameRecord` and its debug types get their own header so `FrameRecordRing`,
`GraphNodeModel` and `GraphInspectorModel` include the record, not the builder.

**Exit gate:** graph dumps and capture semantics match the parent commit; the protocol holds;
checkpoint A is unchanged; the App model library no longer includes `RenderGraph.h`.

**Defer:** exposure, bloom and display stage extraction and graph implementation splits (R1.5);
any change to pass order or content.

## R1.5 — Optional consolidation and decomposition

Not a gate B prerequisite; each item is accepted on its own plan under the protocol.

- Core helpers with two or more consumers of one contract: whole-file read; JSON string escaping
  replacing the three escapers, without object or array framing; whole-string numeric parsing
  primitives, with positivity, range and finiteness rules staying at their callers; two alignment
  functions under distinct names with a test pinning the power-of-two and zero contracts; one
  `divRoundUp`. The atomic write stays with `CaptureSchema`; adopting it for a truncating writer
  is a behaviour change with its own test, not a move.
- Decomposition by responsibility of the over-budget inventory: production `Renderer.cpp`
  (exposure, bloom and display stages), `RenderGraph.cpp` (declaration, compile, transition
  derivation, validation units), `RenderGraphPanel.cpp` (canvas, details pane, dump) and
  `TemporalResolve.cpp` (native, upscale and vendor declaration units); tests `RenderGraphTests`,
  `GpuTemporalTests`, `RHIValidateTests`, the scene tests and `GpuRendererTests`, keeping tags and
  the checkpoint A filter.
- Build description: the setup task and rules move to `xmake/*.lua` includes; each unit owns a
  `xmake.lua` the root includes, like `RHI/xmake.lua`; test-oracle shaders under `Shaders/Tests/`
  and shared modules under `Shaders/Modules/`, with the rule's dependency glob widened, import
  search paths passed to the compiler and basename collisions rejected.

Build items need their own evidence: a clean build; an incremental rebuild after editing an
imported shader module rebuilds every importer; runtime shader output paths unchanged; the
runtime-MSL fallback still loads with metallibs absent; App model sources compile once; Tests
relink after a library change; each target's dependency closure matches the contract.

## Later refactoring milestones

No further R identifiers are reserved. When a planned rendering slice would cross the checked
layering or grow a unit past a review budget, the owner opens the next R milestone here with its
own outcome and gates rather than widening the allowlist. Visible candidates: RHI extraction once
a second backend exists, and a logging facade if compile-time measurement justifies it.
