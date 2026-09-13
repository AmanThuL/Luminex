# Shader Style (Slang)

**Status**: Accepted

- Production pipeline entry files live in `Shaders/`, reusable modules in `Shaders/Modules/`,
  and test/benchmark oracles in `Shaders/Tests/`. Entry points
  `vertexMain`/`fragmentMain`/`computeMain` use `[shader("...")]` attributes (no `-entry` flags).
  Entries and shared modules import only shared modules; modules have no shader entry points.
  Use named imports, never textual includes. `Tools/check_shader_imports.py` checks these edges
  and unresolved imports in local policy and CI, ignoring commented-out examples.
- Globals: `gPascalCase` resources, `kPascalCase` constants. Explicit flat binding indices map 1:1
  to `MTL4ArgumentTable` slots in the current API; any replacement binding model or D3D12 lowering
  must update this convention with its shader ABI.
- Build: Slang → MSL source → `xcrun metal -std=metal4.0` → `.metallib` (two-step; see ADR 0003).
  Generated `.metal` stays in the build tree for debugging — read it when a shader misbehaves.
  `xmake/shaders.lua` passes the module search directory and tracks all `.slang` sources recursively;
  editing an imported module rebuilds every shader conservatively. App, Tests and FrameDataBench
  compile all sources, including modules and oracles, into their separate target directories.
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
