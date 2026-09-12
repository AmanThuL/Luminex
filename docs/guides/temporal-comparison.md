# Comparing temporal reconstruction

**Status**: Implemented

Use TemporalLab to isolate a motion/history failure and San Miguel to inspect reconstruction on
textured surfaces, foliage and fine geometry. A screenshot at rest does not establish temporal
quality: inspect both synchronized motion and the same paused frame/crop.

## Scene and capture

San Miguel is optional because the upstream archive is about 511 MiB. Install it explicitly:

```bash
xmake setup --san-miguel
xmake run App --scene san-miguel
```

The scene uses the archive's realtime variant, imported at its authored metre scale, with diffuse
alpha cutouts, two-sided foliage, tangent-space normal maps and a deterministic 12-second camera
rail. In the Rendering Inspector, enable playback and Follow camera track for repeatable movement.
Select Raw, Native TAA or MetalFX Temporal and compare the same render scale; scale 0.5 makes the
reconstruction work easier to inspect. Keep dynamic resolution off for a controlled comparison.

The archive is from [Morgan McGuire's Computer Graphics Archive](https://casual-effects.com/data/).
Guillermo M. Leal Llaguno authored it; the archive credits improvements to Morgan McGuire, Guedis
Cardenas, Michael Mara and Nicholas Hull. Its metadata calls it San Miguel 2.0 and CC BY 3.0, while
the enclosed license names version 2.1 and provides its own attribution/use wording. Setup preserves
both files verbatim and records the source URLs, hashes, chosen variant and conversion in
`Assets/Fetched/SanMiguel/PROVENANCE.json`. Assets remain gitignored.

## Repeatable visual report

The [comparison tool](../../Tools/TemporalCompare/README.md) captures three independent sequences
with identical camera, simulation time, jitter, warmup and rendering settings. It refuses a vendor
fallback, incomplete sequence or mismatched conditions. Use its documented isolated Python
environment; the output directory must be new or empty:

```bash
python3 -m venv /tmp/luminex-compare-venv
/tmp/luminex-compare-venv/bin/pip install -r Tools/TemporalCompare/requirements.txt
xmake build App
/tmp/luminex-compare-venv/bin/python Tools/TemporalCompare/compare.py \
  --app build/macosx/arm64/release/App --scene san-miguel \
  --frames 120 --warmup 32 --scale 0.5 --output /tmp/san-miguel-half
```

Repeat at `--scale 1 --output /tmp/san-miguel-native`. Open each output's `index.html` directly.
All columns advance together and share the crop position/zoom. Inspect leaf boundaries and railing
gaps for lost detail or trails, textured walls for shimmering, and newly uncovered surfaces for
history contamination. Native and MetalFX are alternatives sharing the input contract; a more
similar image is not automatically a better one.

The direct App interface is `--capture-sequence <directory> --frames N --warmup W`. It saves frames
W through W+N−1 at 60 Hz, with actual effective mode, camera, exposure and status in `manifest.json`.
Existing `--screenshot` still renders N frames and saves only the final image. Every saved frame
waits for GPU readback; capture timing is not a realtime performance measurement.

## FLIP's role

Optional official [NVIDIA FLIP](https://github.com/NVlabs/flip) runs on the CPU through the pinned
Python package; it is not part of App, the RHI or normal setup. Install the tool's optional
requirements and regenerate a captured report with `--report-only --flip --ppd 67`.

This report applies **LDR-FLIP to final sRGB output**, after Luminex's display transform. It records
the viewing assumption in pixels per degree, package versions, per-frame mean and pixel p95, and
fixed-scale difference maps. It compares Raw/MetalFX against the matching native frame. Native TAA
is an algorithm baseline, not a supersampled ground truth; therefore these are perceptual
**differences**, not accuracy scores, quality rankings or a pass/fail gate.

Single-frame FLIP does not model temporal history. Frame-wise maps can help locate differences,
but flicker, ghosting and disocclusion still need sequence inspection and the existing scripted
temporal metrics. No frozen native/vendor tolerance changes because this tool was added.

## Limits

- Static imported geometry and camera motion do not replace TemporalLab's moving-object/invalid
  motion cases. San Miguel does not gain animated vegetation, skinning or particles.
- Masked surfaces use ordinary filtered alpha mips. Alpha coverage preservation is a separate
  concern, so distant foliage may thin before reconstruction.
- Phong materials are approximated as PBR; unimplemented glass/water transmission and source
  height maps are not simulated. The report compares the same imported content across all modes.
- CSS pixel magnification is shared across columns; operating-system/browser scaling still affects
  physical screen pixels. The PNGs and metadata remain the authoritative exported evidence.
