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
  `vertexMain`/`fragmentMain`/`computeMain` use `[shader("...")]` attributes (no `-entry` flags); a
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
- Globals: `gPascalCase` resources, `kPascalCase` constants. Explicit flat binding indices map 1:1
  to `MTL4ArgumentTable` slots in the current API; any replacement binding model or D3D12 lowering
  must update this convention with its shader ABI. The shared buffer namespace has 16 slots;
  texture and sampler namespaces retain 16 and 8 slots respectively.
- Build: Slang → MSL source → `xcrun metal -std=metal4.0` → `.metallib` (two-step; see ADR 0003).
  Generated `.metal` stays in the build tree for debugging — read it when a shader misbehaves.
  `xmake/shaders.lua` passes `-I Shaders/Common` for every shader; a source under `Shaders/Tests/`
  also receives `-I` for each `Shaders/Passes/<family>/`, since an oracle may test a family module.
  The dependency list is every `.slang` source of the target, so editing any module rebuilds every
  shader conservatively. App, Tests and FrameDataBench compile all sources, including modules and
  oracles, into their separate target directories.
  Artifacts retain `<targetdir>/Shaders/<basename>.metal` and `.metallib` paths. Basenames must be
  globally unique, including case: the build rule and import checker reject collisions.
  Offline Metal compilation remains optional; absent metallibs use the emitted MSL at runtime.

## Mirrored exposure variants

These three pairs remain separate compiled pipelines. Mirror every behavior, layout and arithmetic
edit across each pair except the named exposure source:

| Manual variant | Auto-exposure twin | Sole intended difference |
| --- | --- | --- |
| `ScenePass.slang` | `ScenePassAuto.slang` | Final scene color and emissive pre-exposure read `gPass.preExposure` versus `gExposureOverride[0]`. |
| `ScenePassMask.slang` | `ScenePassAutoMask.slang` | The same exposure-source substitution, with identical mask coverage and two-sided shading. |
| `Sky.slang` | `SkyAuto.slang` | Final sky radiance reads `gSky.preExposure` versus `gExposureOverride[0]`. |

The auto variants additionally declare the required persistent exposure buffer at buffer slot 3;
manual variants do not bind or reference it. Their existing uniform layouts stay the same. Each
pair keeps matching ordinary and motion entry points, with reactive emissive exposure following the
same source in the scene pairs. Opaque and masked files are separate coverage variants, not this
exposure twin relationship. Do not introduce a runtime exposure branch, generated rewriting or
whole-file deduplication here: prior byte-parity evidence requires distinct manual shader output.
R4 re-tested a shared scene-pass module and deferred it; [ADR 0027](../decisions/0027-scene-pass-deduplication-defer.md) owns reopening.
