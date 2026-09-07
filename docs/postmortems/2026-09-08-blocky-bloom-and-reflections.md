# Blocky bloom and studio reflections

**Status**: Closed

## Symptom and isolation

Owner review of native TAA found coarse rectangular halos around TemporalLab's bright emissive
sign and recalled similar pixelation on MaterialLab's glossy spheres. Pausing the sign and toggling
Bloom isolated its halo to the post-process. Fixed-camera MaterialLab captures with Bloom disabled
still showed coarse reflection detail inside the spheres, identifying a second source.

## Causes

Bloom starts at half resolution and builds four further 2× downsample levels. Its accumulation
upsample used integer `pixel / 2` loads, and display composition again selected a single half-size
texel. Each coarse texel therefore expanded into a block. TAA runs before bloom and cannot filter
that later addition.

MaterialLab reduced the studio HDRI to 32² cubemap faces. The CPU specular prefilter reconstructed
that source with nearest-neighbor sampling into a 64² mirror level. Linear runtime sampling could
soften block edges but could not recover the missing environment detail. This was distinct from
bloom and from the diagnostic spheres' geometric tessellation.

## Correction

- Bloom accumulation and final composition reconstruct with clamped, pixel-center bilinear
  interpolation using actual source and destination extents, including odd and single-axis sizes.
  The scene-color fetch stays exact, and Bloom disabled still skips its texture read entirely.
- CPU specular prefilter samples interpolate across cubemap faces. MaterialLab uses 128² studio
  sky and reflection faces, retaining all five roughness levels. Other scenes and the missing-HDRI
  fallback retain the 64² reflection default.
- A solid-angle-weighted source mip pyramid supplies trilinear samples at the footprint derived
  from the GGX sample PDF. This suppresses bright-source noise on rough materials at the unchanged
  1024-sample budget. The mirror level retains source mip 0 and constant environments retain their
  original arithmetic.
- A separate 32² conversion of the same HDR radiance feeds MaterialLab's exact diffuse
  convolution. It preserves diffuse output and bounds its source-texel integration cost.
  `ibl::GenerationOptions` carries the reflection size and optional borrowed diffuse source only
  during generation; `SceneEnvironment` forwards the options.

## Prevention and verification

The original full-chain bloom GPU oracle dispatched only one 8×8 group even for a 32×32 target and
mostly inspected the origin. It now dispatches all required groups and compares every RGB texel.
Hand-derived ramp cases separately reject nearest sampling in both the compute upsample and the
final display pass; odd dimensions, clamped edges, singleton axes, scene detail and disabled bloom
are covered. The old shaders fail the added cases.

IBL regressions reconstruct an analytic face ramp, a continuous directional-radiance environment
across all cube edges/corners, and an explicit adjacent-face contribution. Nonconstant output
remains byte-deterministic. The old nearest sampler fails these cases. Texture-upload coverage
checks the selected studio/default reflection extent. An HDR checker environment verifies the
roughness-1 integral and fails when source mip selection is forced to zero.
Odd-sized source coverage checks constant closure and border-radiance retention against an
independent diffuse quadrature; source mip weights integrate clipped spherical rectangles.

Local validation: `xmake`, `xmake format --check`, `xmake policy`, then `xmake build Tests` and
the Tests binary from its build directory:

| Filter / configuration | Result |
|---|---|
| `~[gpu]` | 100,395 assertions / 504 cases passed |
| `[gpu]`, `MTL_DEBUG_LAYER=1` | 244,389 assertions / 138 cases passed |
| Frozen `[checkpoint-a]`, `MTL_DEBUG_LAYER=1` | 1,856 assertions / 19 cases passed |
| Bloom/display focused, API + shader validation | 3,750 assertions / 6 cases passed |

Metal API and shader validation reported no validation errors. The GPU suite's expected negative
capture-path checks still emit their application error messages.

### Fixed-camera evidence

Apple M3 Max, macOS 26.5.2, release build, 1280×720. Comparison baseline is the merged native-TAA
release, [PR #22](https://github.com/AmanThuL/Luminex/pull/22). All final captures ran with
`MTL_DEBUG_LAYER=1`. Commands from the repository root:

```sh
MTL_DEBUG_LAYER=1 xmake run App --scene temporal-lab --frames 200 --temporal taa --screenshot /tmp/sign.bmp
MTL_DEBUG_LAYER=1 xmake run App --scene material-lab --frames 32 --temporal taa --screenshot /tmp/material.bmp
LMX_SCREENSHOT_NO_BLOOM=1 MTL_DEBUG_LAYER=1 xmake run App --scene material-lab --frames 32 --temporal taa --screenshot /tmp/material-no-bloom.bmp
```

The sign's rectangular halo becomes continuous. Glossy studio detail retains sharper boundaries,
and the roughness sweep no longer shows the bright-source sampling mottling. Temporal-off,
one-frame MaterialLab captures show the same correction, isolating it from temporal reconstruction.
Final BMP SHA-256 values:

| Capture | SHA-256 |
|---|---|
| TemporalLab, TAA200, Bloom on | `969fcc865435d80bbd472a7ff3fec82a8bd45e4617c5b52fb3ec883abb427ec2` |
| MaterialLab, TAA32, Bloom on | `ddbcd5cfdbb3f2e75a32d4de63dabca610b19dcfc1ebd1692b227a9d5ed18a72` |
| MaterialLab, TAA32, Bloom off | `ea773041eb94fe68241b409b4dae3a99062115bc831c4ce0526c980488de8846` |

Bloom-off TemporalLab TAA200 and Damaged Helmet TAA32 remain byte-identical to the baseline.
Sponza TAA32 repeated Bloom-off captures match the accepted baseline; separate captures exposed
small 1–2 LSB outliers, so this record does not claim universal Sponza byte determinism. No visible
sampling regression appeared in those scenes. Raw local captures, manifests, paired timings and
logs are retained under the ignored `build/review/bloom-reflections/` directory.

### Cost

The studio sky and five-level reflection textures add 1.45 MiB of GPU storage. Diffuse source and
output sizes, the DFG table and specular sample budget are unchanged. CPU generation builds the
source pyramid once per scene, then discards it after upload; `SceneLibrary` caches the scene.

A bounded startup comparison ran the release App directly from its runtime directory with
`LMX_SCREENSHOT_NO_BLOOM=1 ./App --scene material-lab --temporal off --frames 1 --screenshot /tmp/startup.bmp`.
With hot filesystem/shader caches, alternating baseline/final order, one excluded warmup pair and
five measured pairs, whole-process medians were **0.244 s before / 1.498 s after**. This includes
initialization, scene generation, rendering, readback and process shutdown; it is neither isolated
scene-build time nor a steady-state GPU benchmark. Runtime bloom uses four reads per interpolation
instead of one; no frame-rate improvement is claimed.

## Evidence boundaries

Historical M6.2 bloom-enabled capture hashes remain historical evidence and are not rewritten.
This correction intentionally changes bloom-enabled output and MaterialLab's studio reflections.
Bloom-disabled captures of the neutral-environment scenes are the parity boundary. The two-float
exposure contract, temporal constants, graph pass shape, RHI surface and original checkpoint-a
cases are unchanged.

The 1K studio source and finite cubemap resolution still bound reflection detail. The 32-segment
sphere mesh can show polygonal silhouettes at close range; this correction addresses rectangular
sampling artifacts inside the reflection and bloom halo rather than changing scene geometry.

## References

- [Current frame pipeline](../frame-pipeline.md)
- [Native TAA evidence](../milestones/m6.2.md)
- `Shaders/BloomUpsample.slang`, `Shaders/DisplayTransform.slang`
- `Source/Engine/Ibl.h`, `Source/Engine/MaterialLab.cpp`
