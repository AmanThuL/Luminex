# Shader Style (Slang)

**Status**: Accepted

- Shaders sit in three folders: `Shaders/Passes/<family>/` holds each family's entry files and any
  module local to that family, `Shaders/Common/` holds a module imported by two or more families or
  kept shared by a recorded decision (`Shadow` is that one decision, owner 2026-09-20), and
  `Shaders/Tests/` holds test/benchmark oracles. `import X;` resolves a file beside the importer
  before any `-I` directory, so a family's entries find their local modules with no search path; a
  source under `Shaders/Tests/` additionally receives `-I` for every family folder, since an oracle
  may test a family module and an oracle is not a family. Entry points
  `vertexMain`/`fragmentMain`/`computeMain` use `[shader("...")]` attributes (no `-entry` flags); a
  module has no shader entry points. Use named imports, never textual includes.
  `Tools/check_shader_imports.py` and the compiler both enforce family-local and `Common/`-only
  import locality — a `Common/` module imports `Common/` modules only, and a family-local module is
  imported only from its own folder. The checker alone also enforces that no import reaches a file
  that declares an entry point, and that every file sits in `Common/`, `Tests/` or
  `Passes/<family>/` — checking these edges and unresolved imports in local policy and CI, ignoring
  commented-out examples.
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
