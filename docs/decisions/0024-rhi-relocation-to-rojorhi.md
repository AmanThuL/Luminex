# ADR 0024: The RHI relocates to RojoRHI

**Status**: Proposed (2026-09-19)

## Context

The RHI is the root `RHI/` component: public `lmx::rhi` headers that depend on nothing, a shared
implementation, the Metal 4 backend and its optional ImGui adapter. Its code is already separable.
The implementation reaches four Core headers and no other Luminex unit, reads no environment
variable and owns no shader-toolchain duty. What ties it to Luminex is everything around the code:
Core as a link dependency, its tests and smoke shaders inside the flat test trees, four units in the
module contract, the `[checkpoint-a]` filter spanning RHI and render-graph cases, and the `lmx`
identity.

[R1](../roadmap/codebase-module-boundaries.md#r1--module-boundaries-and-shared-foundations)
deferred publishing the RHI standalone, and the refactoring roadmap held extraction as a candidate
for the time a second backend exists. The owner opened it earlier, on 2026-09-19, so that the
interface has its own rules, history and conformance suite before the next rendering milestones
grow it further. [Part III](../roadmap/codebase-restructuring.md#r2--rhi-becomes-rojorhi) owns the
outcome and gates; the [R2 record](../milestones/r2.md) keeps the design.

## Decision

The RHI becomes RojoRHI, developed in the private `rojo-rhi` repository and mounted in Luminex as a
git submodule at the root path `RojoRHI/`. The `RHI/` path disappears, so a stale reference fails
loudly. Luminex stays the primary project and RojoRHI's required consumer. This supersedes R1's
deferral of a standalone RHI.

**Identity.** Namespace `rojoRHI` and `rojoRHI::metal4`, include root `rojoRHI/`, macros
`ROJORHI_ASSERT` and `ROJORHI_LOG_*`, default GPU label prefix `rojorhi.`, targets `RojoRHI` and
`RojoRHIMetal4ImGui`. The mixed-case namespace and include root are the one approved exception to
lowercase naming. The `lmx` namespace, renderer-owned `lmx.*` labels and `LMX_*` environment
variables are unchanged everywhere outside the component.

**Decision ownership.** ADRs [0002](0002-metal4-first.md), [0004](0004-thin-rhi.md),
[0007](0007-d3d12-backend-target.md), [0009](0009-checkpoint-a-conformance.md) and
[0010](0010-execution-model-partial-reshape.md) stay accepted and immutable as the history of how
the interface was decided. RojoRHI restates the parts that bind it as its own founding ADRs, and
from the mount onwards those govern the component. Every later RHI decision is a RojoRHI ADR; a
Luminex ADR records only how Luminex consumes it.

**Superseded text.** Two accepted ADRs are superseded in part, each from the slice that makes the
new text true rather than from this ADR's acceptance. Of
[ADR 0020](0020-module-layering-and-units.md): the units `rhi-public`, `rhi-impl`,
`metal4-backend` and `imgui-adapter`, every other unit's dependency on them, the ownership of the
public-header standalone-compile check, and the spelling of the Asset allowance. The module
contract replaces the four units with one external entry listing the headers each consumer may
include, from the in-place decoupling, when that check moves to RojoRHI with the component; the
rename changes the allowance's spelling to `rojoRHI/Format.h` and the `rojoRHI` types Asset may
never name. The allowance itself stands: Asset
still reaches only `Format.h` and `TextureDesc.h`, and the archive symbol check still proves it
links nothing from the RHI. Of ADR 0009: the single run command and the file locations of its
cases, as the next paragraph states. Its areas, case names, assertions and tolerances stay frozen.

**Interface.** The relocation adds one public interface: a message callback taking a severity and
a message, writing to stderr when unset. RojoRHI carries a private base for assertion, alignment,
JSON escaping and logging instead of depending on Core. Luminex installs a sink at startup that
forwards to spdlog and the Console. No RHI capability or semantic changes with the move.

**Checkpoint A.** The frozen suite of ADR 0009 becomes two filters whose union equals the frozen
inventory by case name. Cases that exercise only the RHI move with the RHI tests and are owned by
RojoRHI's conformance ADR. The `GpuTransientTests` and `RenderGraphTransientTests` cases stay in
Luminex under `[checkpoint-a]`. The split takes effect with the in-place decoupling. A future
backend passes both filters unchanged.

**Two-repository workflow.** An RHI change is a `rojo-rhi` branch and pull request, driven by a
Luminex consumer. While it is in development the Luminex branch may pin the RojoRHI branch commit.
Before the Luminex pull request merges, the RojoRHI change is merged and the pin is a commit on
`rojo-rhi` `main`; a Luminex policy check added with the mount verifies that. RojoRHI changes meet
RojoRHI's conventions and conformance tests; the pin bump meets Luminex's GPU suite. A Luminex
commit never edits a file under `RojoRHI/`.

**Build.** Both repositories use xmake. Luminex includes RojoRHI's targets file, never its root
project file, so project settings, requires and the language level are declared once per root.
RojoRHI owns a minimal setup task and a test-only copy of the shader rule so that it builds and
tests with no Luminex checkout; the production shader rule stays in Luminex.

## Consequences

An interface change now costs two pull requests and a pin bump, and hosted CI needs a read-only
credential for the private submodule. Agent and contributor sessions follow two instruction files:
work under `RojoRHI/` follows RojoRHI's. The shader rule, the smoke shaders both sides use and the
test bootstrap exist twice, deliberately, so that each repository runs alone; drift between the
copies is an accepted cost. The extraction rewrites `rojo-rhi` `main` once, after an archive tag
preserves its 2024 stub, and never again.

In return the interface has a conformance suite that runs without a renderer, a history that
follows its files, and a boundary no include can cross unnoticed. A second backend remains governed
by the restated ADR 0007 and is not a capability of either repository. Releases, a package
registry entry and a public repository stay deferred.
