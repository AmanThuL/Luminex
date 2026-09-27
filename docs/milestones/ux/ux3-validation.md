# UX3 validation

**Status**: In progress — owner acceptance pending.

This record accompanies [UX3](ux3.md). The owner requested uninterrupted execution, replacing
the intermediate owner checkpoints with measured agent verification. A failed gate stays failed;
the lab re-baseline, provisional artwork and final behavior still require owner acceptance.
ADR 0028 remains Proposed. Nothing in this record authorizes a merge.

## Evidence and environment

Evidence is retained outside source under `../Luminex-evidence/ux3/`, relative to the repository
root. The frozen parent is `f181d8e`; the UX3.1 candidate is `b9fcc65`. Both use release builds on
Apple M3 Max, macOS 26.7 (25G229), Xcode 26.6 (17F113), with the unchanged RojoRHI pin
`8da2a79e82ef66ed67d9642f0f3c5a74d20c33a8`. GPU checks and image captures enable
`MTL_DEBUG_LAYER=1`; screenshot runs leave `LMX_SCREENSHOT_NO_BLOOM` unset unless explicitly
testing that override. No performance conclusion is drawn.

`environment.json` records local setup. `original-reference.json` preserves the original
15-case reference, while `original-documents/` preserves all eight exported document/buffer
pairs from tag `ux3-exporter`, with hashes and provenance. Later fixture retirement must not
overwrite this evidence or erase the original gate result.

## Build and contract gates

Tasks 1–9 each passed build, full `Tests/unit`, format and the six direct root checkers before
commit. Engine, Render and shader commits also passed the full `Tests/gpu` group with Metal
validation. Every implementation received an independent review; review findings were fixed
and re-reviewed before the next dependent task began. Per-task logs and `*-gates.json` files
retain the commands and results. Task 4 and Task 9 have explicit corrected-check records after
their scoped fixes; unchanged source retains its earlier gate evidence. Task 7's passing run is
`task7-fixed-gates.json`; the earlier `task7-final-gates.json` retains failed checks and is not
the accepted corrected run.

Notable corrected failures were URI decoding and save rollback boundaries, cumulative local-light
capacity, generated versus authored light ownership, selected shadow-caster semantics, a test
that read scene tables before preparation, headless auto-exposure seeding, a parity subprocess
crash incorrectly counted as complete, and missing zero-light flags for a schema-4 parent.
These corrections do not waive any image gate.

## UX3.1 — original catalog

Measured on 2026-09-28, before the helmet/truck retirement and lab changes:

| Gate | Result | Evidence |
|---|---|---|
| Temporal-off BMP SHA-256, eight scenes at frame 1 plus Sponza/San Miguel at 600 and 3600 | **FAIL: 1/12 exact**; MaterialLab frame 1 passes | `task10/candidate-off/summary.json`, parent captures in `parent-off/` |
| Exact first-compiled-frame graph dumps for those twelve runs | **PASS: 12/12** | Same summary and paired `.graph.txt` files |
| Eight alternating rounds per binary, original fifteen cases | **FAIL: 5/15 cases**; all five MaterialLab modes pass | `task10/parity-rounds/summary.json` |
| Document reader/writer, canonical and failure-path tests | **PASS: 82,068 assertions / 22 cases** | `task10/document-tests.log` |
| Current-head read, save, reload, save to separate directories | **PASS: 8/8 pairs equal each other and the originals** | `task10/roundtrip/summary.json`, `commands.json` |
| Khronos validation | **PASS: 8 catalog + 46 writer outputs** | `task10/validator.log` |
| Full candidate Metal-validation suite at this gate | **PASS** | `task10/metal-validation.json`, `.log` |

All twelve temporal-off captures completed successfully. The eleven mismatches comprise Sponza
at frames 1/600/3600, San Miguel at 1/600/3600, and Damaged Helmet, Milk Truck, TemporalLab,
VisibilityLab and LightLab at frame 1. `LMX_GRAPH_DUMP` exports the first compiled frame; these
dumps do not measure frame-600/3600 endpoint topology. Graph equality does not establish image equality.
Raw images, hashes and commands are retained; no threshold or reference was changed.

The alternating run completed all 240 captures. Every Sponza and Damaged Helmet case has
candidate hashes absent from all eight corresponding parent rounds. Native TAA at full scale
produced two Helmet hashes in each binary and two Sponza hashes in the candidate; the union
comparison still fails those cases. All sixteen batches also score **0/15 against the historical
reference**, including the frozen parent. That separate historical failure does not turn the
parent-versus-candidate result into a pass or permit replacing Sponza's reference hashes.
All eighty Helmet capture logs retain the same missing baked-mip/runtime fallback and normal-map
renormalization warning in both binaries. This is a shared input-path limitation, not a new
candidate-only failure; its contribution to image variation has not been isolated.

### Orientation migration failure

The one-time exporter checked 10,845 orientations: 10,349 matched exactly and **496 did not**.
The failures are 24 directional values and 472 rail keys. All eight initial cameras and Sponza's
eight spot directions matched. Per-scene failures were Sponza 457, Damaged Helmet 3, Milk Truck 3,
MaterialLab 3, TemporalLab 18, San Miguel 4, VisibilityLab 5 and LightLab 3.

The parent neutral rig uses non-unit approximations such as 0.577/0.707 directly in shading;
the normalized quaternion decoder cannot reproduce those raw vector bits. Camera searches
exhausted the prescribed 6,561 neighboring candidates per unmatched key. That establishes a
failure of this search, not a proof that no quaternion could ever match. The largest reported
angle delta is `4.76837158203125e-7` radians. Rail conversion carries the previous decoded yaw.

To keep the remaining requested work executable, the exporter wrote deterministic seed
quaternions only for unmatched values, returned failure and recorded each original/decoded
value, bit delta and fallback. This is an explicit migration deviation from exact export;
ordinary interactive Save must still fail visibly when its exact orientation search fails.
Three exporter runs were retained; the final sixteen document/buffer hashes did not change.
`exporter-report.txt` and `task6/orientation-summary.json` retain the full evidence. The
orientation failure is a plausible image-difference source, not yet a causal explanation of
every mismatched pixel.

`task10/diagnosis/` independently checks decoded RGB deltas, source assets, look defaults and
sampled camera brackets. Sponza changes 344/674/1,226 pixels at 1/600/3600, each by at most one
code value. The other failed scenes range from 1,241 to 96,397 changed pixels; Helmet has a
localized maximum delta of 23. These measurements describe the failed gate, not a tolerance.
All initial cameras and the modeled long-frame interpolation brackets have exact exporter
orientation evidence, so the rail misses do not automatically explain these endpoint failures.
The active directional-vector change is the strongest supported mechanism. No additional defect
was established by the focused source audit; runtime uniform equality was not measured, and
code-generation effects from the new selected-shadow-caster expressions remain unisolated.

### Other implementation deviations and limits

- `SceneLook` and its shadow enum are Asset-owned CPU vocabulary, with an explicit Render
  conversion. The record's units table was amended before implementation to preserve dependencies.
- Asset clips retain source hierarchy, rest transforms and per-clip local channels; CUBICSPLINE
  was added because the required InterpolationTest exercises it. Independent runtime loops are
  the following slice's work, not claimed here.
- Model URI values are decoded Assets-relative paths; serialization encodes once and loading
  decodes once. Extra private URI/save helpers and one exact direct test-header exception permit
  shared validation and injected ordinary rename failures. No broader private-header access is
  allowed. Two-file Save rolls back ordinary reported failures; crash atomicity is not claimed.
- Runtime preflight currently accepts selected-camera LINEAR translation/rotation document
  channels, referenced asset clips and generator-owned motion. Other document animation targets,
  camera STEP/scale and nonidentity generator-root/ancestor transforms fail with field pointers
  before GPU creation. Static asset-root transforms are supported. Reader validity therefore
  does not imply that every model can be instantiated; this limits the specification's broader
  one-clock wording and is not an owner-approved amendment.
- The existing single LightLab pile control edits the first explicit generator population;
  other generators and authored lights keep their identities and values.
- Manifest v3 and measurement schema 5 hash the loaded document and buffer snapshot. The hash
  does not describe unsaved live edits. Path-opened reports retain the caller's spelling.
  PNG `lmx:frame` metadata remains unchanged.

## UX3.2 — fixture integration

MaterialLab now includes the fetched Helmet beside its sphere grid and RGB axis station;
TemporalLab includes the fetched Truck beside its generated movers. Their original cameras and
asset URI/hash pairs remain unchanged. The two standalone IDs and document/buffer pairs retire.
The generator axis parameter defaults off, preserving explicit-path replay of the archived lab.
Opening captures in `task11/` show both stations within the frame; five-mode re-baselining is pending.

Asset playback keeps source-local channels and independent clip periods on unwrapped elapsed
time. Reset uses the same pose evaluator, and stable instance handles prevent removal or slot
reuse from retargeting animation. CPU tests pass 822 cases / 1,491,957 assertions; the real Truck
paused Reset and global-wrap check passes 14 assertions under Metal validation. Khronos passes
57 documents. The combined Task 11/13 tree passes all eleven required checks, including full
Metal validation (`task11-integrated-gates.json`). The first full run's canonical/layout failures
remain in `task11-final-*`; the
catalog JSON was rewritten canonically without changing its companion buffers or source hashes.

**Review failure retained: P2.** After three repairs to animation preflight, independently looping
ancestor and child STEP scale clips can still combine into an indecomposable pose and assert.
The source-level counterexample uses a parent period of 2 seconds with scale `(0,1,1)` during
`[0.5,1)`, and a child period of 4 seconds with `(1,0,1)` during `[2,3)`; each otherwise uses
identity. Shared-clock validation sees at most one collapsed axis. Independent phases at 2.5
seconds produce `(0,0,1)`, which the playback decomposition rejects. This counterexample was
reviewed from source, not executed as part of the passing suite. The catalog Truck does not use
this scale pattern. Preflight rejects the tested shear/linear-collapse cases but is incomplete;
no general fail-closed claim is made.

`task11-review-final-against-d568570.patch`, the final report and retained RED/GREEN logs describe
the repair boundary. Execution deviates from the review-pass dependency rule here: the requested
three-attempt limit and continuation instruction were applied, so the remaining review failure
is carried forward without approval. Re-baselining does not accept or hide this defect.

## UX3.3 — disabled instance rows

Task 13 is implemented at `e3a6b3f`; independent review and all eleven required gate commands pass
(`task13-final-gates.json`). Disabled identities retain their rows but produce no scene, shadow,
outline or independent occlusion-reference contribution. All five submission/classification
paths restore the exact prior image after re-enabling. The observing-camera test changes 2,739
pixels when disabled; its subject is Sponza's `column_a` material primitive group, not a separately
authored pillar node. Persisted source-node edits fan out to every primitive of that node.

The positive-occluder test proves history invalidation: holding the old coverage epoch yields one
false rejection; restoring the production epoch passes 148 assertions across both GPU layouts.
The temporary negative control is absent from committed code. Independent review remained separate
from implementation; a reviewer was reused after two fresh-reviewer creation attempts hit the
runtime thread limit. Session enablement, save/dirty behavior and GUI checks remain later work.

## Remaining slices

Lab re-baselining, session enablement, persistent export/dirty state, editor workflow, both-size
gesture verification, icon verification and integrated head gates are pending. No later slice
is marked passed by the work above.
