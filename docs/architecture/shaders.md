# Shaders

**Status**: Implemented

Shaders are authored in Slang and compiled offline into readable MSL and, when the toolchain is
present, a metallib; App, Tests and FrameDataBench each compile the whole Slang tree into their own
target directory and load the result at runtime.

## Compilation

Each shader compiles in two steps, per [ADR 0003](../decisions/0003-slang-shaders.md): `slangc`
emits MSL source, then `xcrun metal -std=metal4.0` turns that MSL into a `.metallib`. The metallib
step needs the offline Metal toolchain (`xcodebuild -downloadComponent MetalToolchain`); when it is
absent, only the MSL is written, and RojoRHI's Metal backend compiles that MSL when a shader
library is loaded, preferring the metallib when one exists. [The frame pipeline](frame-pipeline.md)
walks the frame's live resource sequence. Both artifacts land under the compiling target's own
`<targetdir>/Shaders/`, as `<basename>.metal` and `<basename>.metallib`, and the generated `.metal`
stays there for inspection when a shader misbehaves. Visibility, occlusion, HZB and light-cluster
shaders compile with precise floating point (`-fp-mode precise`, `-fno-fast-math
-ffp-contract=off`).

`xmake/shaders.lua` defines the `slang2metallib` build rule that runs these steps for every target
that opts in. Slang resolves `import X;` against the importing file's own directory before any
`-I` directory, so a pass family's entries find the modules that sit beside them with no search
path at all; the rule therefore passes `-I` for `Shaders/Common/` only, plus, for a source under
`Shaders/Tests/`, every `Shaders/Passes/<family>/` directory, since an oracle may test a
family-local module. The rule treats every `.slang` file reachable through those directories as a
dependency of every source it compiles, so editing any module rebuilds the target's shaders. Output
basenames are a flat, case-insensitive namespace: the rule rejects two sources in the same target
that would emit the same basename, and the tree-wide import checker (below) rejects the same
collision across the whole `Shaders/` tree. Runtime code loads a shader by basename alone, so a
file's folder never appears in its runtime name.

## Shaders/Common/

`Shaders/Common/` holds the modules shared across pass families: `Encode`, `Lighting`,
`LocalLights`, `Shadow`, `Motion`, `Tonemap`, `SceneTables` and `AlphaMask`. A module here declares
no shader entry point.

## Shaders/Passes/<family>/

Ten folders under `Shaders/Passes/` hold each pass family's entry files and any module local to
that family, one folder per family in Render's pass vocabulary: `Bloom`, `Display`, `Exposure`,
`LocalLights`, `Occlusion`, `Scene`, `SelectionOutline`, `Shadow`, `Temporal` and `Visibility`. Each
entry function is marked with a `[shader("...")]` attribute, so the build passes no `-entry` flag;
names follow `vertexMain`, `fragmentMain` and `computeMain`, or a stage prefix plus the pass
(`vertexMainMotion`, `computeBloomThreshold`).

## Shaders/Tests/

`Shaders/Tests/` holds the test and benchmark oracles: `FrameDataQuad`, the sampler, shadow,
fullscreen, MRT, compute-image, buffer-hazard and scene-table ABI fixtures, the HZB depth and
readback fixtures, the occlusion probe, the punctual-light oracle and the visibility depth
readback. An oracle belongs to no family, so it alone may import a module local to any
`Shaders/Passes/<family>/` folder as well as `Common/`.

`RojoRHI/Shaders/Tests/` is a separate tree, owned by the RojoRHI component. It compiles into its
own target directory, so it may reuse a basename this tree also uses, and it may not import this
tree's modules. It holds its own `Modules/Shadow.slang`, the `Triangle` shader, the cube,
render-area, compute, indirect, instance-index and binding-limit smoke shaders, and byte-identical
copies of the six oracles both test binaries need.

## Placement and import rules

Every Slang source must sit in `Common/`, `Tests/` or `Passes/<family>/`; a `Common/` module may
import only `Common/` modules, and a family-local module may be imported only from its own family
folder, with `Tests/` exempted as the one place that may cross a family boundary. No import may
reach a file that declares a shader entry point, and imports must be named module imports rather
than textual includes. [Shader style](../conventions/shader-style.md) owns this rule; `slangc`
enforces the family and `Common/` boundaries by refusing to resolve the import, and
`Tools/check_shader_imports.py` checks all of it, including the basename, folder-placement and
no-entry-point-import rules that `slangc` does not check. `xmake policy` runs the checker, locally
and in CI.

## Tests

- `Tests/Render/Passes/Bloom/`: the threshold, downsample and upsample bloom shaders.
- `Tests/Render/Passes/Display/`: the display-transform shader.
- `Tests/Render/Passes/Exposure/`: the histogram-accumulate and exposure seed/resolve shaders.
- `Tests/Render/Passes/LocalLights/`: the light-cluster and local-light shading shaders.
- `Tests/Render/Passes/Occlusion/`: the HZB reduce/publish/debug and occlusion-reference shaders.
- `Tests/Render/Passes/Scene/`: the scene, mask and sky shaders, alpha-mask coverage and BRDF math.
- `Tests/Render/Passes/SelectionOutline/`: the editor-only selection mask and outline shaders.
- `Tests/Render/Passes/Shadow/`: the shadow-pass shaders and shadow-fit math.
- `Tests/Render/Passes/Temporal/`: the reproject, resolve, upscale and vendor-pack shaders.
- `Tests/Render/Passes/Visibility/`: the visibility classify, scan and emit shaders.
- `Tests/Render/Graph/`: the MRT, compute-image, buffer-hazard and fullscreen oracles, through the
  render graph.
- `Tests/Engine/Scene/`: the scene-table row ABI that the shared `SceneTables` module binds to.

The import checker's own cases are `Tools/tests/test_shader_imports.py`, outside `Tests/`.
