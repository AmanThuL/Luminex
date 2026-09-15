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
