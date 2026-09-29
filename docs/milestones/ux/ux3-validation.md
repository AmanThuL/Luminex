# UX3 validation

**Status**: Implemented — owner authorized integration on 2026-09-29; image gates failed as measured

This record accompanies [UX3](ux3.md). On 2026-09-28 the owner asked for execution without manual
stops, so owner checkpoints were replaced by recorded verification; on 2026-09-29 the owner
reviewed the result and authorized squash integration. A failed gate stays failed and no
tolerance or threshold was approved. [ADR 0028](../../decisions/0028-scene-document-contract.md)
stays Proposed. The [final validation](ux3-final-validation.md) holds the integrated run, and the
[editor validation](ux3-editor-validation.md) holds the native-gesture ledger.

## Evidence and environment

Evidence lives outside source under `../Luminex-evidence/ux3/`, relative to the repository root.
The frozen parent is `f181d8e`. Parent and candidate use release builds on Apple M3 Max, macOS
26.7 (25G229), Xcode 26.6 (17F113), with the unchanged RojoRHI pin
`8da2a79e82ef66ed67d9642f0f3c5a74d20c33a8`. GPU checks and captures enable `MTL_DEBUG_LAYER=1`;
screenshot runs leave `LMX_SCREENSHOT_NO_BLOOM` unset. No performance conclusion is drawn.

`environment.json` records local setup. `original-reference.json` preserves the original 15-case
reference and `original-documents/` the eight exported document/buffer pairs from tag
`ux3-exporter`, with hashes and provenance. Retiring the fixture scenes does not overwrite this
evidence or the original gate result.

## Build and contract gates

Each implementation step passed build, the full `Tests/unit` group, format and the six direct root
checkers before commit; Engine, Render and shader changes also passed `Tests/gpu` under Metal
validation. Independent review preceded each dependent step, and per-step `*-gates.json` files
retain commands and results. Corrected failures included URI decoding and save rollback
boundaries, cumulative local-light capacity, generated versus authored light ownership,
selected shadow-caster semantics, a test that read scene tables before preparation, headless
auto-exposure seeding, a parity subprocess crash counted as complete, and missing zero-light flags
for a schema-4 parent. None waives an image gate.

## UX3.1 — original catalog

Measured on 2026-09-28, before the helmet/truck retirement and lab changes:

| Gate | Result | Evidence |
|---|---|---|
| Temporal-off BMP SHA-256, eight scenes at frame 1 plus Sponza/San Miguel at 600 and 3600 | **FAIL: 1/12 exact**; MaterialLab frame 1 passes | `task10/candidate-off/summary.json`, `parent-off/` |
| First-compiled-frame graph dumps for those twelve runs | **PASS: 12/12** | Same summary, paired `.graph.txt` |
| Eight alternating rounds per binary, original fifteen cases | **FAIL: 5/15**; all five MaterialLab modes pass | `task10/parity-rounds/summary.json` |
| Document reader/writer, canonical and failure-path tests | **PASS: 82,068 assertions / 22 cases** | `task10/document-tests.log` |
| Read, save, reload, save to separate directories | **PASS: 8/8 pairs equal each other and the originals** | `task10/roundtrip/summary.json` |
| Khronos validation | **PASS: 8 catalog + 46 writer outputs** | `task10/validator.log` |
| Full candidate Metal-validation suite | **PASS** | `task10/metal-validation.json` |

The eleven mismatches are Sponza and San Miguel at frames 1/600/3600, and Damaged Helmet, Milk
Truck, TemporalLab, VisibilityLab and LightLab at frame 1. `LMX_GRAPH_DUMP` exports the first
compiled frame, so the dumps say nothing about frame-600/3600 topology, and graph equality does not
establish image equality. Raw images, hashes and commands are retained.

The alternating run completed all 240 captures. Every Sponza and Helmet case has candidate hashes
absent from all eight parent rounds. Native TAA at full scale produced two Helmet hashes in each
binary and two Sponza hashes in the candidate. All sixteen batches also score 0/15 against the
historical reference, the parent included. Every Helmet capture log in both binaries carries the
same missing baked-mip fallback and normal-map renormalization warning; its contribution to image
variation was not isolated.

### Orientation migration

The one-time exporter checked 10,845 orientations: 10,349 matched exactly and **496 did not**
(24 directional values, 472 rail keys). All eight initial cameras and Sponza's eight spot
directions matched. Misses per scene: Sponza 457, Damaged Helmet 3, Milk Truck 3, MaterialLab 3,
TemporalLab 18, San Miguel 4, VisibilityLab 5, LightLab 3. The search tried 6,561 neighbouring
candidates per unmatched key; the largest angle delta is `4.76837158203125e-7` radians. For the
misses the exporter wrote deterministic seed quaternions, returned failure and recorded each
original and decoded value, bit delta and fallback; the sixteen final document/buffer hashes were
identical across three exporter runs (`exporter-report.txt`, `task6/orientation-summary.json`).

### Parity root cause

Evidence: `../Luminex-evidence/ux3/review-2026-09-29/parity-root-cause/`, with `out/results.json`
and the difference masks `out/*-mask.png`.

The parent's neutral key/fill/rim rig used non-unit directions, `{±0.577, −0.577, 0.577}` (length
0.999393) and `{0, −0.707, −0.707}` (length 0.999849). They reach shading unnormalized in N·L,
the half vector and V_Smith; only the shadow fit normalized them. A glTF rotation always decodes a
unit vector, so documents deliver normalized directions, with two effects:

1. Direct light is 0.061% and 0.015% brighter than the parent for the two affected lights.
2. The decoded components differ from the parent's by 1–2 ulp, which changes the shadow view
   matrix.

Scaling directional intensity by the parent's vector length in the candidate binary cut Sponza's
changed pixels from 344 to 92 (maximum difference one code value). With both corrections
VisibilityLab drops from 25,267 to 16,546 changed pixels. The residuals sit on soft-shadow
penumbrae and glossy highlights; Helmet's maximum delta is 23. MaterialLab passes because its rig
is disabled on both sides. Look defaults, sky colour, strengths, spot directions, cameras and
light order were audited and match.

Exact parity through glTF quaternions is therefore unattainable. UX3 accepts the normalized rig as
a one-time, explained change. Storing raw vectors in `LMX_scene` was rejected as a duplicate
definition of orientation. The design's statement that directional orientation is exact in both
directions was wrong for directions and is corrected in [UX3](ux3.md).

`task10/diagnosis/` separately measured decoded RGB deltas. Sponza changes 344/674/1,226 pixels at
frames 1/600/3600, each by at most one code value; the other failed scenes change 1,241 to 96,397
pixels. All initial cameras and the modelled long-frame interpolation brackets have exact
orientation evidence, so the rail misses do not explain these endpoint failures.

### Other deviations and limits

- `SceneLook` and its shadow enum are Asset-owned CPU vocabulary with an explicit Render
  conversion; the units table was amended before implementation to preserve dependencies.
- Asset clips retain source hierarchy, rest transforms and per-clip local channels. CUBICSPLINE
  was added because the required InterpolationTest exercises it.
- Model URI values are decoded Assets-relative paths: serialization encodes once, loading decodes
  once. One exact direct test-header exception permits injected rename failures. Two-file Save
  rolls back ordinary reported failures; crash atomicity is not claimed.
- Runtime preflight accepts selected-camera LINEAR translation/rotation channels, referenced asset
  clips and generator-owned motion. Other document animation targets, camera STEP/scale and
  nonidentity generator-root or ancestor transforms fail with field pointers before GPU creation.
  Reader validity does not imply every model can be instantiated.
- The LightLab pile control edits the first explicit generator population; other generators and
  authored lights keep their identities and values.
- Manifest v3 and measurement schema 5 hash the loaded document and buffer snapshot, not unsaved
  live edits. Path-opened reports keep the caller's spelling. PNG `lmx:frame` metadata is unchanged.

## UX3.2 — fixture integration

MaterialLab includes the fetched Helmet beside its sphere grid and RGB axis station; TemporalLab
includes the fetched Truck beside its generated movers. Cameras and asset URI/hash pairs are
unchanged. The two standalone IDs and their document pairs retire. The generator axis parameter
defaults off, so explicit-path replay of the archived lab still works. Opening captures in
`task11/` show both stations in frame.

Asset playback keeps source-local channels and independent clip periods on unwrapped elapsed time;
Reset uses the same pose evaluator, and stable instance handles keep removal or slot reuse from
retargeting animation. CPU tests pass 822 cases / 1,491,957 assertions; the paused-Reset and
global-wrap Truck check passes 14 assertions under Metal validation; Khronos passes 57 documents
(`task11-integrated-gates.json`).

The independent-loop case was an open defect at first: an ancestor with STEP scale `(0,1,1)` on
`[0.5,1)` (period 2 s) and a child with `(1,0,1)` on `[2,3)` (period 4 s) combine at 2.5 s into
`(0,0,1)`, which the playback decomposition rejects. It is closed by the
[review corrections](ux3-final-validation.md#review-corrections): STEP scale keys are validated
and an indecomposable sampled pose holds the previous pose with one warning.

### Reference re-baseline

`Tools/Screenshots/reference.json` uses schema 2: Sponza, MaterialLab and TemporalLab, each with
Off, Native TAA at 1 and 0.5, and MetalFX at 1 and 0.5. Helmet's five rows retire and
MaterialLab's former five are superseded. The ten lab values come from candidate captures, listed
with both App hashes, document hashes and the unchanged asset hashes in
`task12/reference-transition-inventory.json`. The five Sponza rows carried over from schema 1 are
historical values that no current binary, the parent included, reproduces on the validation
machine; they are re-captured from the integrated head under the file's recorded device provenance (see
[Reference re-capture](ux3-final-validation.md#reference-re-capture)).

Schema 2 follows App's nearest-first catalog search from its working directory and checks the
document JSON and buffer it will load before the first image and after each capture; a drifted,
missing or malformed document names the scene. Schema 1 stays readable when callers pass the
original reference and document directory. Image thresholds are unchanged.

The parent has ten complete lab captures in `parent-lab-modes/captures.json`. The candidate
captured all fifteen modes with Metal validation in `task12/candidate-matrix-final/`. The
replay against the historical Sponza rows gave 10/15 exact: both labs pass all five modes and all
five Sponza rows fail (`task12/parity-current/parity.json`). The strict parent/candidate
comparison gave 1/15 within the existing threshold (`task12/matrix-strict.json`). The ten changed
lab images are the requested fixture differences. A visual check of all ten parent/candidate lab
pairs found the helmet, RGB axes and truck present in every mode and no gross corruption
(`task12/visual-inspection.json`); still frames at frame 32 do not prove animation.

## UX3.3 — disabled instance rows

Disabled identities keep their rows but produce no scene, shadow, outline or occlusion-reference
contribution. All five submission/classification paths restore the exact prior image after
re-enabling. The observing-camera test changes 2,739 pixels when disabled; its subject is
Sponza's `column_a` primitive group, not a separately authored pillar node. Persisted source-node
edits fan out to every primitive of that node.

The positive-occluder test proves history invalidation: holding the old coverage epoch yields one
false rejection, and the production epoch passes 148 assertions across both GPU layouts
(`task13-final-gates.json`). Session enablement, save/dirty behavior and GUI checks are in the
[editor validation](ux3-editor-validation.md).
