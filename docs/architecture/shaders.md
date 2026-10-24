# Shaders

**Status**: Implemented

Shaders are authored in Slang and compiled offline with the mounted component's `rojorhi.slang`
rule. Each target selects runtime entry files and writes its own backend-specific libraries;
modules are dependencies, not standalone runtime libraries.

## Compilation

`xmake/luminex_shaders.lua` shares entry selection and settings across App, Tests and the dormant
FrameDataBench target. It subtracts the pending source/test-shader patterns in `xmake/port.lua`,
then selects files containing shader attributes. App currently selects DisplayTransform alone;
Tests additionally selects BufferHazardSmoke, ComputeImageSmoke, FullscreenSample, MrtSmoke,
SamplerSmoke and VisibilityDepthReadback. BENCH remains skipped; its declared entry is FrameDataQuad
only. SCENE, VISIBILITY, LIGHTS, POST and TEMPORAL stay out of the active entry list.

`RojoRHI/xmake/slang.lua` owns `rojorhi.slang` and the backend compilation/package contract.
A Metal 4 configure requires `xcodebuild -downloadComponent MetalToolchain`: Slang emits MSL,
then the offline compiler builds the packaged `.metal4lib`. Generated MSL remains available for
inspection. There is no runtime source compilation fallback. Null emits `.nulllib`; the D3D12
package extension is `.d3d12lib`, without implying consumer hardware acceptance on that backend.
`LMX_SHADER_LIBRARY_EXTENSION` selects the suffix used by
`GpuContext::loadShaderLibrary` for `Shaders/<basename><extension>`.

Targets retain separate shader directories; the adapter keeps its embedded shader and
`rojorhi-imgui` directory. Runtime basenames stay flat and case-insensitively unique. The shared
rule receives the public `RojoRHI/Shaders/RojoRHI` module and Common/family search paths; test
entries additionally see their oracle-root modules. Module edits participate in rebuild dependencies.

## Roots and reflection

Ported entries `import RojoRHI;` and read typed roots with `rojoRHI::root<T>()`. The module owns
all backend bindings; consumer entries declare no binding/register numbers. Buffers use `Addr`
with `load/store`, and textures/samplers use view/sampler indices through the module helpers.
`uploadFrameData` returns an Addr for nested constants; `pushRoot` supplies the draw/dispatch root.
Roots are flat mirrored structs with explicit padding and checked sizes/offsets.

DisplayRoot is 16 bytes: sceneColor and bloom view indices at 0/4, zero padding at 8 and
bloomIntensity at 12. Display retains integer texel loads and manual interpolation, without a
sampler. The [shader convention](../conventions/shader-style.md) owns the retained exposure twins.

Two leaf `rojorhi.shader_layout` probes have separate ownership roles:

- `Shaders/Layout/LuminexLayout.slang` reflects Source roots for App, currently DisplayRoot.
- `Shaders/Layout/TestLayout.slang` reflects test-only oracle roots for Tests. Its five flat
  layouts cover BufferHazard, ComputeImage, FullscreenSample, SamplerSmoke and VisibilityDepth;
  MrtSmoke remains root-free.

The rule reflects Metal, SPIR-V scalar layout and HLSL, maps probe names to C++ types, and generates
`ShaderLayout.h` for the target's real assertion TU. This is layout validation; HLSL reflection
alone does not establish DXIL execution. Layout probes have an ordinary probe function and no
shader attribute, are not runtime entries, and cannot be imported. Production/Common modules
cannot import Tests roots. Pending families keep their probe sections until their roots are ported.

## Shaders/Common/

`Shaders/Common/` holds the modules shared across pass families: `Encode`, `Lighting`,
`LocalLights`, `Shadow`, `Motion`, `Tonemap`, `SceneTables` and `AlphaMask`. A module here declares
no shader entry point.

## Shaders/Passes/<family>/

Ten folders under `Shaders/Passes/` hold each pass family's entry files and any module local to
that family, one folder per family in Render's pass vocabulary: `Bloom`, `Display`, `Exposure`,
`LocalLights`, `Occlusion`, `Scene`, `SelectionOutline`, `Shadow`, `Temporal` and `Visibility`. Each
entry function is marked with a `[shader("...")]` attribute, which the shared rule discovers;
names follow `vertexMain`, `fragmentMain` and `computeMain`, or a stage prefix plus the pass
(`vertexMainMotion`, `computeBloomThreshold`).

## Shaders/Tests/

`Shaders/Tests/` holds the test and benchmark oracles: `FrameDataQuad`, the sampler, shadow,
fullscreen, MRT, compute-image, buffer-hazard and scene-table ABI fixtures, the HZB depth and
readback fixtures, the occlusion probe, the punctual-light oracle and the visibility depth
readback. An oracle belongs to no family, so it alone may import a module local to any
`Shaders/Passes/<family>/` folder as well as `Common/`.

`RojoRHI/Legacy/Shaders/Tests/` is a separate tree, owned by the RojoRHI component. It compiles into its
own target directory, so it may reuse a basename this tree also uses, and it may not import this
tree's modules. It holds its own `Modules/Shadow.slang`, the `Triangle` shader, the cube,
render-area, compute, indirect, instance-index and binding-limit smoke shaders, and frozen oracle
copies independent of the consumer root ABI.

## Placement and import rules

Runtime entries and modules sit in `Common/`, `Tests/` or `Passes/<family>/`; the two leaf probes
have the explicit `Layout/` role. Common modules import only Common and the public RojoRHI module;
family-local modules are imported only by their own family, Tests or the leaf reflection probes. No import may
reach a file that declares a shader entry point, and imports must be named module imports rather
than textual includes. [Shader style](../conventions/shader-style.md) owns this rule; `slangc`
enforces the family and `Common/` boundaries by refusing to resolve the import, and
`Tools/check_shader_imports.py` checks those boundaries, the two explicit Layout roles,
basenames, placement and no-entry-point imports. Run it directly from a worktree; CI runs it in policy.

## Tests

The inventory below includes pending families; `xmake/port.lua` owns the currently compiled subset.

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
