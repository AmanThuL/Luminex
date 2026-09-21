# Module layering and units

**Status**: Accepted (2026-09-13)

## Context

The runtime grew as five targets in a chain, `Core → RHI → Render → Engine → App`. Engine sits
above Render because `Scene.h` includes `Render/Renderer/Renderer.h`, so one target holds two layers: glTF,
DDS, Radiance HDR and PNG decoding, texture baking, IBL generation and animation clip data, which
need no GPU, and scenes, catalog and labs, which own GPU meshes and textures. `TextureBake`, a CPU
tool, links Render, the RHI and the Metal backend to reach the decoders.

Nothing checks direction, so helpers settled wherever they were first needed and duplicated:
`srgbToLinear` exists in Engine and in Render because Render cannot include Engine; two `alignUp`
functions carry different alignment contracts under one name; `divRoundUp` and JSON string escaping
each exist several times; repository asset discovery exists four times. `render::AlphaMode` lives in
Render because the glTF loader must name it. Tests and App compile the same App sources separately,
and nothing keeps those sources free of ImGui, SDL and Metal. Only RHI public headers compile
standalone. The dependency direction is described in the architecture overview and enforced nowhere.

The GPU-driven work adds scene data, identities and visibility at exactly these seams. A layering
that a tool can check keeps those slices small and their review about rendering rather than about
where code lives.

## Decision

The chain is replaced by named units, each with one sentence of ownership and an explicit set of
units and third-party packages it may depend on. Anything not listed is forbidden. The units are
`core`, `rhi-public`, `rhi-impl`, `metal4-backend`, `imgui-adapter`, `asset`, `render`, `scene`,
`app-model`, `app-shell`, `tests`, `texture-bake` and `benchmarks`. The
[module contract](../conventions/modules.md) holds the table, the transitional file lists, the
placement rules and the allowlist policy, and is the document a checker implements.

Engine is retired and its two halves are named: `asset` owns CPU content and finishes decode and
validation; `scene` owns GPU-resident scenes, uploads and playback. `lmx::asset` and `lmx::scene`
replace `lmx::engine`. `App/Model` becomes a linked library but keeps `lmx::app`, because it is the
same domain as the shell with the platform libraries removed, and renaming it would churn the
editor for no boundary.

`asset` depends on `core` alone, with a header allowance rather than a target dependency: it may
include `RHI/Format.h` and a texture descriptor header extracted from `Texture.h`, and may never
name `rhi::Device`, `rhi::Texture` or `rhi::Buffer`. That extraction — moving `TextureKind`,
`TextureDesc`, `TextureMip` and the range, view and copy descriptors into a leaf header that
`Texture.h` includes, with no signature, semantic or umbrella change — is the only change this
decision makes to an RHI public header.

Ownership comes from an explicit map from unit to paths, where the longest matching path wins, and
the map is reconciled with build-target membership so the two cannot disagree. A compilation
database contributes compilation context only, because a file compiled by several targets cannot
establish ownership. Reach has two semantics: project includes are followed transitively, so a
model header cannot borrow ImGui through a shell header, while a third-party package is charged
only to the unit whose own file names it. The two ImGui adapter sources move from the backend
directory into the adapter's own directory so that map and target agree.

## Consequences

The namespace change is one mechanical commit over about 300 qualified uses — roughly 190 spelled
`engine::` and 99 spelled `lmx::engine` across sources, tests, tools, benchmarks and documentation
— reproducible from a recorded substitution and kept separate from every edit and move.

`core` takes glm as a public package so that one colour-transfer header can carry both the scalar
and the vector overloads that Render and the asset decoders need. RHI public headers still include
neither glm nor spdlog and keep their dependency-free standalone compile.

`TextureBake` links `core` and `asset` only, and Tests link libraries instead of compiling App
sources a second time. The allowlist starts non-empty and names every current crossing; each
refactoring slice's exit gate empties it for the units that slice owns, and the layered proof of
Asset independence — the header allowance, the tool's link line, and an archive symbol check that
is explicitly the weakest of the three — replaces review as the thing that holds the boundary.

This decision changes no rendered output, no shader, no uniform layout and no RHI semantics. It
stays `Proposed` until the checkers and the adapter move land, and is accepted before the first
file moves between units.

## Alternatives considered

**Asset-owned duplicate descriptors.** Let `asset` declare its own texture description types and
convert at the upload boundary, leaving RHI public headers untouched. Rejected: it duplicates a
contract that has to stay identical field for field, adds a translation step with no behaviour in
it, and makes future drift silent instead of a compile error. Sharing one leaf header costs a
smaller change to `Texture.h` than the duplicate costs forever.

**An unchecked include restriction.** State the layering in the architecture overview and rely on
review to hold it. Rejected: that is the present state, and it is how Engine ended up above Render
and how four copies of asset discovery appeared. A rule with no checker is a preference.

**Numbering this work as a rendering milestone.** Rejected: the structural slices change no
rendered output, and a rendering number would both promise a visible outcome and place the work
inside the interface gate's sequence instead of before it, where the restructured tree is what the
gate reviews.

## Cross-references

- [Module contract](../conventions/modules.md) — the enforceable form of this decision.
- [Codebase refactoring roadmap](../roadmap/codebase-module-boundaries.md) — slice order and exit gates.
- [Architecture overview](../architecture/overview.md) — current structure, updated as units move.
- [ADR 0018](0018-masked-material-coverage.md) — the glTF alpha-mode contract the loader keeps
  when `AlphaMode` moves to its own leaf header.
