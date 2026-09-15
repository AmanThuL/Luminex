# Frozen screenshot comparison

**Status**: Implemented

[The comparator](../../Tools/Screenshots/compare.py) compares existing parent/candidate captures;
it does not render, replace reference images, or update the strict SHA-256 runner. Install Pillow
from [the temporal comparison requirements](../../Tools/TemporalCompare/requirements.txt).

## Run and retain evidence

The default `strict` profile preserves the original per-image gate: at most 1% of pixels differ
in any RGB channel, and at most 0.05% differ by more than eight code values. Equality at either
boundary passes. All comparisons use opaque, 8-bit encoded SDR RGB; alpha does not enter the metric.

```sh
python3 Tools/Screenshots/compare.py --selftest
python3 Tools/Screenshots/compare.py --parent /absolute/parent --candidate /absolute/candidate \
  --output /absolute/evidence/strict.json
python3 Tools/Screenshots/compare.py --parent /absolute/parent --candidate /absolute/candidate \
  --profile vendor-quantization-v1 --output /absolute/evidence/vendor-quantization-v1.json
```

The output path must be new. Each report retains the image SHA-256 hashes, comparator hash,
requested profile, per-image applied profile and thresholds. `differingPixels` always counts
nonzero differences; `thresholdDifferingPixels` counts differences above the applied tolerance.
`strictPass` retains the original gate result even when the optional profile is selected.
A complete report returns exit code zero only if every image passes its applied profile.

Without `--sequence`, the comparator reads the fifteen fixed cases and their reconstruction modes
from [reference.json](../../Tools/Screenshots/reference.json). It applies the optional rule only to
cases whose reference mode is `metalfx`; Native TAA and Off keep the strict rule. Names alone do not
select a profile. Keep the original strict report alongside any optional-profile result.

Add `--sequence` for exactly three paired TemporalLab frames. This path checks matching v2
manifests, settings and per-frame state and rejects vendor fallback. The manifest reconstruction
mode selects the per-image profile; Native/Raw/Off retain strict comparison.

## Compare visibility and submission modes

Culling and indirect submission are the defaults. Screenshot and sequence commands accept
`--visibility cull|off` and `--submission direct|indirect|batched`, so replay each frozen case with
only the tested switch changed. Preserve its scene, camera track, frame count, reconstruction,
scale, dimensions and other settings. Keep `reference.json` and the strict hash runner unchanged.

```sh
xmake run App --scene temporal-lab --frames 32 --visibility cull --submission indirect \
  --screenshot /absolute/cull-indirect.bmp
xmake run App --scene temporal-lab --frames 32 --visibility off --submission indirect \
  --screenshot /absolute/off-indirect.bmp
cmp /absolute/cull-indirect.bmp /absolute/off-indirect.bmp
```

For the [CPU visibility/indirect verification](../milestones/m7.2.md#output-preservation-and-comparison-rule),
culled versus unculled and indirect versus direct require exact bytes in all fifteen cases; a
passing tolerant comparison does not satisfy those gates. Compare matching BMP bytes/hashes and
retain each result. Batched versus direct uses `strict`, because sorting can change depth ties.
Parent versus candidate retains strict results and the explicitly scoped MetalFX profile; Native
TAA sequence frames retain strict comparison. The milestone record owns outcomes and pending
review. Commands here do not claim a passing matrix or owner acceptance.

The [first-principles reassessment](../milestones/m7.2-visibility-followup.md) preserves that
failure: exact pre-temporal fixtures passed, but a separately frozen empirical-range experiment
failed its independent holdout. Its [verifier procedure](visibility-image-validation.md) remains
experimental and does not supersede the original gate.

For additional workload inspection, `--scene visibility-lab --lab-instances 1024` uses a deterministic
grid and camera rail. N includes boundary probes; the lab is additional coverage and does not
replace any case in the fixed fifteen-image matrix. Image parity and timing are separate checks;
see [measurement scopes](gpu-debugging.md#measure-visibility-and-submission).

## Optional vendor quantization profile

`vendor-quantization-v1` requires all three conditions independently for each MetalFX image:

| Metric | Inclusive limit |
|---|---:|
| Pixels with maximum RGB channel difference >1 | 1% |
| Pixels with maximum RGB channel difference >8 | 0.05% |
| Mean absolute RGB difference over all pixels and channels | 0.1 code value |

One code value is one storage quantization step. The first condition tolerates that step while
retaining the original area budget for larger differences. The third condition prevents a
whole-image one-step brightness change from passing solely because of the pixel tolerance.
Its 0.1 budget means a tenth of a code value averaged across all RGB samples. These are explicit
engineering tolerances, not Apple-specified limits or a calibrated guarantee of perceptual equality.
The profile is opt-in and does not supersede a milestone's recorded acceptance criteria.
The owner accepted it for the frozen [M7.1 comparison](../milestones/m7.1.md#image-comparison-criteria);
that scoped acceptance does not set the gate for later milestones.

Self-tests pin inclusive area/mean boundaries and verify that full-image +1/+4 code-value changes,
a one-pixel shift and a 32×32 high-contrast bad block fail. A small local defect can still fit the
area budget, and low-amplitude flicker requires sequence inspection. This comparison complements
motion, reset, exposure, ABI and GPU validation; it cannot establish their correctness alone.

## Why a temporal result can differ after current inputs converge

MetalFX combines current jittered color, depth and motion with retained prior output. Motion
locates previous samples, depth informs foreground/disocclusion handling, and jitter varies sample
positions. Earlier input differences can therefore remain in history even when a later frame's
inputs match. This is a state-dependence explanation, not a claim that MetalFX is random or that
all output differences are harmless. See Apple's
[temporal upscaling walkthrough](https://developer.apple.com/videos/play/wwdc2022/10103/).

Exposure and reset also belong to that input contract. Apple documents an exposure texture that
multiplies input color, and recommends resetting history on camera cuts or extreme camera motion.
Incorrect motion, jitter, exposure or reset can cause real reconstruction defects. Preserve paired
input/sequence evidence when diagnosing differences; do not infer their cause solely from final
8-bit images. See [exposureTexture](https://developer.apple.com/documentation/metalfx/mtlfxtemporalscalerbase/exposuretexture)
and [Bring your game to Mac, Part 3](https://developer.apple.com/videos/play/wwdc2023/10125/).
