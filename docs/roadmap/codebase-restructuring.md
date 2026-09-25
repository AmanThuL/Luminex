# Codebase Refactoring — Repository, Subsystem and Shader Restructuring (R2–R4)

**Status**: Accepted

Second file of Part III of the [rendering roadmap](../roadmap.md).
[Module Boundaries](codebase-module-boundaries.md) holds the module contract, the comparison
protocol and R1, which is complete; this file holds the milestones the owner placed between M7 and
scene documents (now [UX3](editor-experience.md#ux3--scene-documents-and-hierarchy)) on 2026-09-19: **R2 → R3 → R4**.
They add no rendering scope. Unless a section states otherwise they use the
[R1 comparison protocol](codebase-module-boundaries.md#r1--module-boundaries-and-shared-foundations)
unchanged, and earlier parity exceptions relax nothing here. Records hold design detail:
[R2](../milestones/r/r2.md) and [R3](../milestones/r/r3.md) (Proposed), [R4](../milestones/r/r4.md) (closed as DEFER).

## R2 — RHI becomes RojoRHI

**Outcome:** the RHI is the separately published, Apache-2.0 `rojo-rhi` repository, mounted at
`RojoRHI/` as a submodule with its own conventions, ADRs, conformance tests and CI. Luminex remains the primary project and
the RHI's required consumer; every RHI edit is a RojoRHI commit. This supersedes R1's deferral of
a standalone RHI and the earlier candidate that waited for a second backend.

**Identity:** namespace `rojoRHI` and include root `rojoRHI/` (the one owner-approved exception to
lowercase naming, guarded by an include-spelling check), `ROJORHI_` macros, lowercase `rojorhi.`
default labels, targets `RojoRHI` and `RojoRHIMetal4ImGui`. xmake in both repositories.

**Sequence:** R2.1 → R2.2 → R2.3 → R2.4, before any other milestone plan.

**Exit gate:** every slice gate below; the public message callback is the only RHI interface
addition; every pre-existing test case passes under its name in one of the two repositories.

**Defer:** a second backend ([ADR 0007](../decisions/0007-d3d12-backend-target.md) still governs
it); releases or a package registry entry; any RHI capability or semantic change; unifying the two
shader rules.

### R2.1 — RojoRHI foundations

**Outcome:** RojoRHI has its own rules before it has any code.

**Deliver:** in `rojo-rhi`, documents only: `AGENTS.md`, conventions for naming, C++ style,
commits, documentation and testing derived from Luminex's with each deliberate difference stated,
founding ADRs restating the RHI decisions of Luminex ADRs 0002, 0004, 0007, 0009 and 0010, and the
recorded substitution R2.3 executes. In Luminex: a new ADR recording the relocation, the
two-repository workflow and the checkpoint A split. Accepted Luminex ADRs stay immutable.

**Exit gate:** the owner accepts the conventions and ADRs before any source changes; the 2024
stub in `rojo-rhi` carries an archive tag.

### R2.2 — Decouple in place

**Outcome:** `RHI/` builds and tests as if already alone, while parent/candidate comparison is
still possible inside Luminex.

**Deliver:** a private base (assert, alignment, JSON escaping, `std::format` logging) replacing
the four Core includes, with one public message callback that Luminex forwards to spdlog and the
Console; RHI-only tests split out by a checkable include rule, with copied smoke shaders and a
split test bootstrap; checkpoint A as two filters whose union is the frozen inventory; a
standalone xmake build with its own setup task and a test-only shader-rule copy; the public-header
check, ImGui patch and buffer probe gathered under the component; the module contract's four
`rhi-*` units replaced by one external entry.

**Exit gate:** the protocol holds commit by commit; `xmake -P RHI` builds and passes linking no
Luminex target; GPU runs are validation-clean.

### R2.3 — Mechanical rename

**Deliver:** one commit reproducible from the recorded substitution (namespace, macros, include
root, targets, default label strings and the directory `RHI/` → `RojoRHI/`) with Luminex call
sites; formatting separate.

**Exit gate:** the protocol holds with one declared difference: default labels and log text change
exactly as the substitution predicts. Renderer-owned `lmx.*` labels, `LMX_*` variables and the
`lmx` namespace outside the component are untouched.

### R2.4 — Extract, mount and wire

**Deliver:** an immutable tag on the last in-tree commit; `git filter-repo` extraction keeping
history for every path RojoRHI receives; after the owner confirms, `rojo-rhi` `main` replaced by
the result and the repository made public under Apache-2.0; the submodule mount with Luminex
including the component's targets file; CI in both repositories, with anonymous submodule checkout
now that `rojo-rhi` is public; a policy check that the pinned commit is reachable from `rojo-rhi`
`main`; `AGENTS.md`, architecture and worktree guidance updated, and Luminex's own conventions
scoped to stop at the mount, since the component now carries and enforces its own.

**Exit gate:** a fresh recursive clone builds; Luminex at the mount commit matches the
pre-extraction tag under the protocol; `rojo-rhi` builds and passes with no Luminex checkout;
policy is green in both.

**Implemented 2026-09-20.** `RojoRHI/` is a submodule pinned to `rojo-rhi` `8da2a79`, checked out
anonymously in CI; `Tools/check_submodule_pin.py` enforces the pin is reachable from
`origin/main`. Evidence and limits: [validation record](../milestones/r/r2.4-validation.md).

## R3 — Subsystems and tree restructure

**Outcome:** Luminex has Donut's four subsystems (`Source/Core`, `Source/Engine`,
`Source/Render`, `Source/App`) over the `RojoRHI/` submodule, with Donut's dependency direction
(Render depends on Engine, never the reverse) and its division of responsibility: Core owns the
math and data structures that carry no domain meaning. Every tree has a second level matching its
responsibilities, and no leaf folder holds one file or more than about sixteen. Rendered output
and shipped behaviour do not change.

**Scope:** this is refactoring, not only moving. Header extractions, function relocations, a
dependency reversal, target and namespace renames, extraction into Core, removal of duplicated
implementations and decompositions by responsibility are in scope, each as its own commit under the
comparison protocol; from R3.4 on, a unit that reaches its folder still mixing responsibilities has
not met its slice. No pass label, shader basename, CLI option, schema, file format or existing test
case name changes. Moves are one commit per destination folder, reproducible from a recorded path
and include substitution. `xmake format --check` passes at every commit, not only in CI: a recorded
substitution lengthens the paths, string literals and comments that name what moved, and a line
crossing the 100-column budget is invisible to the policy checker and the tests. A new ADR
supersedes the parts of [ADR 0020](../decisions/0020-module-layering-and-units.md) that retired the
Engine name and let Scene depend on Render; it keeps Asset a separately linked, CPU-only library
inside `Engine/`, so `TextureBake` and GPU-free builds still link no Metal. R3.4 opens with a second.

**Sequence:** R3.1 → R3.2 → R3.3 → R3.4 → R3.5 → R3.6 → R3.7, after R2. Core precedes Render,
App and Tests so each pushes generic code into a Core that already has a charter.

**Defer:** a repository-wide include directory or Donut's `include/`–`src/` split; headers shared
between C++ and Slang; an engine-level shader factory or binding cache; a pass base class; moving
the render graph out of Render; renaming Stage classes or test tags; Core vector types of its own,
container wrappers, a VFS, a thread pool, platform or profiling layers; everything UX2 and UX3 own.

### R3.1 — Documentation records

**Deliver:** `docs/milestones/<series>/` folders for `m1-m4`, `m5`, `m6` (with interface gate B),
`m7`, `r` and `ux`, so no series folder holds a single file; each frozen design beside its record
as `<id>-design.md`; the two postmortems in the series of their period; the founding design
(D1–D10) in `docs/decisions/`; `docs/specs/` and `docs/postmortems/` removed; the fifteen closed
plans deleted as the documentation convention already requires, leaving `docs/plans/` as the one
transient folder. The convention, the policy checker's tables and `AGENTS.md` follow; `AGENTS.md`
states that a brainstormed design is written as the `Proposed` milestone record and a plan goes
to `docs/plans/`.

Then the living documents are copyedited into plain technical prose, one commit per folder, with
every claim, number, identifier, date, status value, gate result and evidence limit preserved and
no heading moved; accepted ADRs, the founding design, every `-design.md`, research notes and
postmortems are not. Four tracked mentions of the owner's personal notes are removed under the
owner-approved exception the [record](../milestones/r/r3.1.md) scopes.

**Exit gate:** policy green, every local link resolving, no document type losing its status field
or precedence. Documents only; the comparison protocol does not apply.

### R3.2 — Shader folders

**Deliver:** `Shaders/Passes/<family>/` for ten families (Scene, Shadow, Visibility, Occlusion,
LocalLights, Temporal, Exposure, Bloom, Display, SelectionOutline), `Shaders/Common/` replacing
`Modules/`, and `Shaders/Tests/`. A module imported by one family lives with it; one imported by
two or more lives in `Common/`; test oracles do not count as a family. The import checker enforces
that a family-local module is imported only from its own folder.

**Exit gate:** [R1.5](codebase-module-boundaries.md#r15--consolidation-and-decomposition)'s build
and shader evidence: generated MSL identical to the parent's once `#line` directives are dropped,
metallib inventory and runtime artifact paths unchanged, importer rebuilds intact, the runtime-MSL
fallback loading, basename collisions still rejected. The format check holds at every commit,
because renaming a shader folder lengthens every path that names it in C++ source.

**Implemented 2026-09-20.** `Shaders/` holds `Common/`, ten `Passes/<family>/` folders and
`Tests/`; `Tools/check_shader_imports.py` enforces placement and import locality, and the runtime
still loads `Shaders/<basename>`. Evidence and limits:
[validation record](../milestones/r/r3.2-validation.md).

### R3.3 — Engine

**Outcome:** scene description lives below the renderer, as in Donut's engine.

**Deliver:** the ADR, accepted first. `Source/Engine/Asset/` (own library; `Image/`, `Model/`,
`Texture/`) and `Source/Engine/{Scene,Types,Upload,Catalog}/` replacing `Source/Asset` and
`Source/Scene`. The scene vocabulary leaves Render for `Engine/Types/` (`Mesh`, `Camera`,
`AlphaMode.h`, `LocalLight.h`, `LocalLightMath`) and the table row ABI `SceneTables.h` for
`Engine/Scene/`; `Bounds.h`, glm-only math with consumers in three units, goes to Core as Donut's
`core/math/box.h` does. Two leaf-header extractions the old headers keep including: `DrawItem` and
`DirectionalLight` out of `SceneView.h`, `MotionClass` out of `Temporal.h`. `Scene::view()`
becomes a Render-side builder, so `SceneView`, scene description plus renderer settings, stays
Render's input contract and the renderer still consumes a plain struct. The module contract
reverses the edge and the checker rejects any Engine include of Render. Then one mechanical
commit: target `Scene` → `Engine`, `lmx::scene` → `lmx::engine`, and the moved vocabulary
`lmx::render` → `lmx::engine`; `lmx::asset` stays, naming the checked CPU-only unit.

**Exit gate:** the protocol and the format check hold at every commit; the Asset archive still has
no undefined RHI references and `TextureBake` links neither Engine nor a Metal framework; the
Engine archive has no undefined `lmx::render` references; every test case is present under its
name.

**Implemented 2026-09-21.** `Source/Engine/` holds `Types/`, `Scene/`, `Upload/`, `Catalog/` and the
unchanged Asset unit under `Asset/{Image,Model,Texture}/`; `Bounds.h` is in Core, target `Scene` has
dissolved into `Engine`, and Render builds `SceneView` with `render::buildSceneView`. The contract
rejects any Engine include of Render and any undefined `lmx::render::` reference in its archive.
Evidence, deviations and limits: [validation record](../milestones/r/r3.3-validation.md).

### R3.4 — Core

**Outcome:** Core owns domain-free math and data structures; Engine keeps only scene meaning. **Implemented 2026-09-21:** evidence and limits in the [validation record](../milestones/r/r3.4-validation.md).

**Deliver:** [ADR 0026](../decisions/0026-core-charter-and-placement.md), accepted first: Core's
charter, glm as the one vector vocabulary, and the rule that domain-free math and data structures go to Core whatever their consumer count while a
domain meaning keeps code in its owning unit. `Source/Core/{Math,Containers,Util,IO,Diagnostics}/`
from code that exists today: geometry, projection, sequences and sampling out of Render, Engine
and Asset; a ring buffer, generational handles and slots, a dirty set and intervals; a stopwatch,
SHA-256 and string helpers ([sources and cuts](../milestones/r/r3.md#what-moves-from-where)). Each
extraction is two commits, the Core type with its tests and then the call sites; a duplicate is
unified only where pinned output proves the contracts identical.

Engine, the first consumer: `Engine/Types/` divides into `View/`, `Lights/`, `Geometry/` and
`Material/`; the five identifier structs become same-layout `Handle<Tag>` aliases; `SceneTables.cpp`
and `Scene.cpp` are decomposed; `Catalog/` becomes the unit `Source/Scenes` (target `Scenes`), so
Engine holds no authored content. Render and App adopt Core only where a whole helper leaves them.

**Exit gate:** the protocol and the format check hold at every commit, image and bake hashes
exact; Core reaches no Engine, Render, App or RojoRHI header or symbol; every Core type has direct
unit tests; the duplicates the record lists are gone; the Engine archive defines no catalog
symbol; R3.3's archive checks and every pre-existing test case name still pass.

### R3.5 — Render
**Implemented and owner-accepted 2026-09-22:** the executor plan is closed. The [record](../milestones/r/r3.5.md) describes the delivered structure; [acceptance](../milestones/r/r3.5-validation.md#owner-acceptance-and-integration) scopes the single retained part A parity failure. Part B passes its local gates; measured adoption limits and both PRs remain in the validation record.

**Deliver:** `Render/Graph/` (graph, compile units, dump, transient pool, frame declaration,
compiled record), `Render/Renderer/` (orchestrator and its partial units, `SceneView.h`, its
builder, `DisplayDomain.h`) and `Render/Passes/<the same ten names as the shaders>/`, one folder
per pass family holding its stages, CPU mirrors, checks and readbacks.

Then the code: stage math and range algebra come from Core; `Renderer.cpp`, `SceneStage.cpp` and
barrier derivation are decomposed; the stages' shared shape enters the engineering convention and is
followed; repeated setup and draw encoding move into shared helpers. No class name, label or
`SceneView` field changes; signatures change only as the [record](../milestones/r/r3.5.md) lists.

**Exit gate:** the protocol and the format check hold; graph dumps and capture semantics match the
parent; no Render source implements math that Core's charter claims.

### R3.6 — App

**Deliver:** `App/{Shell,Headless}`; `Model/` and `Panels/` kept as checked layers, because the
checker proves by directory that AppModel reaches no ImGui, SDL or Metal; feature folders repeated
under both: `Scene`, `Graph`, `Performance`, `Console`, `Capture`, `Workspace`, `Options`, and
`Rendering/{Settings,Temporal,Lighting,Visibility}` in Model; `Scene`, `Inspector`, `Viewport`,
`Graph`, `Performance`, `Console`, `Shared` in Panels. After the moves, `InspectorPanel.cpp` is
decomposed by subject and `EditorShell.cpp` brought under the review budget.

Part A is implemented and [accepted with one scoped exception](../milestones/r/r3.6-validation.md#owner-acceptance-and-integration);
B is implemented; its Core adoptions, retained double fit and closed plan are recorded in the [record](../milestones/r/r3.6.md).

**Exit gate:** the protocol and the format check hold; scripted editor runs are validation-clean;
workspace schema 3 files load unchanged; AppModel still links no ImGui, SDL or Metal.

### R3.7 — Tests and architecture pages

**Deliver:**
`Tests/` mirroring `Source/` to `Render/Passes/<family>` and `App/Model/<feature>`, plus `Tools`, `Support` and `Golden`, as the [record](../milestones/r/r3.7.md) tabulates (implemented 2026-09-24 with its plan closed; [validation](../milestones/r/r3.7-validation.md)), with
CPU and GPU cases of one family side by side, since tags and not folders select GPU runs; tags, case
names, run filters and Luminex's checkpoint A filter unchanged. Test helpers defined more than once
become one in `Support/`; the shader oracles stay independent of Core.
`docs/architecture/overview.md` reduced to the subsystem diagram and an index over `core.md`,
`engine.md`, `render-graph.md`, `render-passes.md`, `app.md`, `shaders.md`, a pointer page for
RojoRHI, and the frame walkthrough beside them. README's directory table, the module convention and
`AGENTS.md` describe the tree as built.

**Exit gate:** the test inventory equals R3.3's by case name plus the Core cases R3.4 declared;
each page is within budget; policy and the format check green.

## R4 — Shader source deduplication

**Outcome:** the scene shader variants share one implementation and differ only where the
[shader-style convention](../conventions/shader-style.md) says they must, while every compiled pipeline stays
separate. Entry is satisfied: M7.1 settled the binding model, so the shared code is the code M7 keeps. It runs
after R3.2 has placed the family in `Shaders/Passes/Scene/` and before M8 and M9 multiply the mirrored edits
the twin rule demands today.

**Scope:** the `ScenePass` family, four files of 320 to 346 lines that differ by exposure source and alpha
coverage. Thin entry-point files over one shared implementation module, with compile-time choices and no
runtime branch: the runtime-branch regression behind the twin files justifies separate pipelines, not
whole-file duplication. `TemporalResolve` and `TemporalUpscale` are not merged for overlap alone; each keeps
its kernel, and only a helper with one contract moves into the temporal family's shared module.

**Sequence:** R4.1 → R4.2, after R3. R4 changes generated shader source, so R3.2's identical-MSL gate does not
apply; the gates below replace it.

**Defer:** merging pipelines or adding a runtime exposure or coverage branch; any shading, binding or
uniform-layout change; other families until this one is adopted.

### R4.1 — Bounded experiment

**Deliver:** on a short-lived `exp/` branch, the shared module and thin entries for the `ScenePass` family;
for each variant, the generated MSL, the reflected resource layout and the rendered output compared with the
parent under the strict parity matrix; an immutable evidence tag; a recorded adopt or DEFER decision.

**Exit gate:** the comparison is complete and recorded for all four variants whatever the outcome.
**Implemented 2026-09-24:** the recorded comparison is complete, so this exit gate holds. **R4 closed as DEFER
2026-09-25** after a [follow-up](../milestones/r/r4.1-followup.md); R4.2 is not opened; [ADR 0027](../decisions/0027-scene-pass-deduplication-defer.md) owns reopening.

### R4.2 — Adoption

**Deliver:** only if R4.1 decides to adopt: the production change on `main`, and the shader-style convention's
twin rule replaced by the rule the shared module now enforces.

**Exit gate:** reflected resource layouts identical to the parent's; rendered output passes the strict parity
matrix; pipeline inventory and labels unchanged; GPU suite validation-clean. A DEFER leaves the twin files and
the convention as they are and closes R4 with its evidence.

## Opening a further R milestone

No identifier beyond R4 is reserved. When a planned rendering slice would cross the checked
layering or grow a unit past a review budget, the owner opens the next R milestone here with its
own outcome and gates rather than widening the allowlist.
