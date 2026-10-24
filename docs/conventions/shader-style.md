# Shader Style (Slang)

**Status**: Accepted

- `Shaders/Passes/<family>/` holds each family's entry files and any
  module local to that family, `Shaders/Common/` holds a module imported by two or more families or
  kept shared by a recorded decision (`Shadow` is that one decision, owner 2026-09-20), and
  `Shaders/Tests/` holds test/benchmark oracles and their nonentry root modules.
  `import X;` resolves a file beside the importer
  before any `-I` directory, so a family's entries find their local modules with no search path; a
  source under `Shaders/Tests/` additionally receives `-I` for every family folder, since an oracle
  may test a family module and an oracle is not a family. Entry points
  `vertexMain`/`fragmentMain`/`computeMain` use `[shader("...")]` attributes for discovery by the shared rule; a
  module has no shader entry points. Use named imports, never textual includes.
  A family-local module is imported only from its own family, Tests or the explicit reflection
  probes. Common modules cannot import family-local or Tests modules. Tests modules are importable
  only from Tests and `TestLayout`; production sources cannot depend on test roots.
- `Shaders/Layout/LuminexLayout.slang` and `Shaders/Layout/TestLayout.slang` are leaf reflection
  probes, with no shader entry points. Both can import Common and pass-family modules;
  `TestLayout` can additionally import Tests modules. No source may import either probe, and no
  other file has an implicit Layout role.
- `import RojoRHI;` reaches exactly the public mounted module at
  `RojoRHI/Shaders/RojoRHI/RojoRHI.slang`, which must exist with that spelling. Local sources
  cannot shadow that module. Private component modules, aliases and other external roots are not
  importable; the component owns its separate shader tree and checker.
  `Tools/check_shader_imports.py` checks these boundaries, placement, unresolved and malformed
  imports, textual includes, entry-point imports and case-insensitive basename collisions in
  local policy and CI, ignoring commented-out examples.
- Globals: `gPascalCase` resources, `kPascalCase` constants. Ported shaders import the public
  RojoRHI module, which owns every binding and reserved index; consumer code declares no binding
  attributes or registers. Use typed roots, `Addr` buffer fields and view/sampler indices through
  its helpers. Mirror flat roots in C++ with explicit padding and size/offset checks; no nested
  root structs or `float3`. Upload larger constants once and carry their Addr in the root.
- Build: `xmake/luminex_shaders.lua` selects entry files only and configures the mounted
  `rojorhi.slang` rule. Common/family/root modules remain dependencies, not runtime libraries.
  App selects kept pass entries; Tests adds kept oracles; dormant FrameDataBench selects only
  FrameDataQuad. `xmake/port.lua` owns exclusions. Each target has its own shader output directory,
  and `LMX_SHADER_LIBRARY_EXTENSION` selects `.metal4lib`, `.nulllib` or `.d3d12lib`.
  Metal 4 requires the offline Metal toolchain (`xcodebuild -downloadComponent MetalToolchain`);
  generated MSL stays in the build tree for inspection. Basenames are case-insensitively unique.
  `LuminexLayout` reflects Source roots and `TestLayout` reflects oracle roots through
  `rojorhi.shader_layout`; neither leaf probe is a runtime entry or importable module.

## Mirrored exposure variants

These three pairs remain separate compiled pipelines. Mirror every behavior, layout and arithmetic
edit across each pair except the named exposure source:

| Manual variant | Auto-exposure twin | Sole intended difference |
| --- | --- | --- |
| `ScenePass.slang` | `ScenePassAuto.slang` | Final scene color and emissive pre-exposure read `gPass.preExposure` versus `gExposureOverride[0]`. |
| `ScenePassMask.slang` | `ScenePassAutoMask.slang` | The same exposure-source substitution, with identical mask coverage and two-sided shading. |
| `Sky.slang` | `SkyAuto.slang` | Final sky radiance reads `gSky.preExposure` versus `gExposureOverride[0]`. |

In the retained pending scene shaders, auto variants declare the persistent exposure buffer at
buffer slot 3; manual variants do not bind or reference it. That slot describes those unported
files only; ported entries use module-owned bindings. Their existing uniform layouts stay the same. Each
pair keeps matching ordinary and motion entry points, with reactive emissive exposure following the
same source in the scene pairs. Opaque and masked files are separate coverage variants, not this
exposure twin relationship. Do not introduce a runtime exposure branch, generated rewriting or
whole-file deduplication here: prior byte-parity evidence requires distinct manual shader output.
R4 re-tested a shared scene-pass module and deferred it; [ADR 0027](../decisions/0027-scene-pass-deduplication-defer.md) owns reopening.
