# Visibility image validation from invariants

**Status**: Proposed — experimental replacement gate failed; not adopted

This experiment evaluated a replacement for the final-SDR cull/off comparison in
[M7.2](../milestones/m7.2.md). The owner requested a first-principles reassessment after the
original exact comparison failed. Its 13/15 result remains historical evidence. This procedure
does not change the renderer, reference images, indirect/direct gate or parent/candidate profiles.

## Start with the invariant

An AABB rejected by a camera half-space contains no point inside that guarded plane. Geometry
contained by that AABB therefore contributes no camera raster samples. Removing its draw must
preserve camera color, depth, motion and reactive coverage. The plane must use the same jittered
view-projection as rasterization; unreliable bounds must bypass rejection. Shadows use a different
view and remain unculled. These are correctness requirements with zero image tolerance.

Use exact CPU/GPU fixtures for bounds, visible rows, draw arguments and raster attachments,
including near-boundary retained geometry, jitter, mask/two-sided materials and render scales.
Final screenshots alone cannot establish those properties: the scene output passes through
temporal history, exposure, bloom, tone mapping and 8-bit SDR storage. An unchanged-input repeat
can expose a repeatability limit in that complete path. It does not identify its underlying cause.

Neither the 1e-3 world-space culling guard nor an observed three-code-value image difference
implies an SDR error allowance. Units differ, and nonlinear filtering/history prevents that
deduction. Even one storage code value is not automatically harmless.

## Freeze an empirical null envelope

The null control uses the same frozen binary with culling off. For each case and pixel/channel
sample `s`, compute `L(s) = min(control(s))` and `U(s) = max(control(s))`. Accept a held-out image
only when **every** sample satisfies `L(s) <= image(s) <= U(s)`. No extra code value, spatial
dilation, changed-pixel budget outside the envelope, or candidate-derived calibration is allowed.
Where controls agree, this is exact RGB equality. A value one code outside the range fails.

The envelope span `U-L` must itself satisfy an independent engineering ceiling. Native/Off use
unchanged M7.1 strict: at most 1% of pixels vary, and at most 0.05% vary by more than eight RGB code
values. All six MetalFX cases use the existing `vendor-quantization-v1` constants: at most 1% of
pixels vary by more than one code, at most 0.05% by more than eight, and RGB mean absolute span
at most 0.1 code. Every held-out mixed pair must satisfy its same ceiling. Preserve strict metrics
even when a vendor ceiling applies. Temporal Off also requires a zero-width envelope and
byte-exact paired BMPs. These ceilings cannot license any sample outside the measured envelope.

The numerical ceilings are inherited engineering budgets, **not** first-principles error bounds
or a perceptual guarantee. First principles determine the exact geometric invariant, the null
experiment, units and zero extra slack. They do not derive an acceptable 0.1 or 1% from hardware
arithmetic. The ceilings stop a wildly unstable reference from accepting arbitrary differences.

The first attempt froze the following collection schedule before new captures:

1. Keep all fifteen cases/settings in `Tools/Screenshots/reference.json`, 1280x720, 32 frames,
   BMP, default bloom/exposure, indirect submission, Metal validation enabled, fresh processes.
2. Calibrate each case from its one existing `candidate-unculled` capture plus **eight** fresh
   unculled captures. Include the legacy anchor uniformly for all cases; earlier exploratory
   cull captures and the selected-case repeat diagnostic do not enter calibration. The protocol
   was designed after those exploratory results were known, not blindly before the original run.
3. Freeze all raw hashes, min/max images, reference/protocol/verifier/collector hashes and runtime
   provenance before collecting any holdout. A failed calibration ends the attempt.
4. Collect four fresh processes per case, ABBA for even case indices and BAAB for odd indices,
   where A=off, B=cull. Pair adjacent runs. Test both off controls and cull outputs against the
   frozen envelope. This is 120 fresh calibration plus 60 fresh holdout captures, with no retries
   or discarded outputs. Keep process return codes, logs and exact comparisons as well.
5. An out-of-envelope off control invalidates the repeatability model; an out-of-envelope cull
   output fails the gate. Neither permits expanding this attempt's envelope from holdout data.
   Any later revised experiment gets a new name/directory and retains the failed attempt.

That first attempt (`visibility-null-envelope-v1`) used strict ceilings for every mode. Its 120
fresh controls completed, but four MetalFX calibration spans exceeded strict; no holdout ran.
The next attempt (`visibility-null-envelope-v2`) explicitly reuses those 135 control images as
training data and applies the mode-scoped ceilings above, uniformly to all six MetalFX cases.
It freezes a new protocol/tool/envelope digest before collecting the planned **60 new holdouts**.
The prior failure is retained. No numeric constant or per-sample range is enlarged. The policy
revision is informed by exploratory controls; only the new holdout is independent evaluation.
Any held-out off or cull violation ends this follow-up without another adaptive widening.

Use the supported tracing-isolation workaround only when required for MetalFX validation, with
the same validation layer still enabled. Record that instrumentation separately from any timing
claim. Freeze fetched assets/runtime dependencies and check that they remain unchanged.

## Check the verifier can detect a regression

Run selftests and retained negative controls before trusting a pass. A one-code change at a stable
pixel, a value just outside a variable pixel's interval, a spatially shifted image, a large bad
block and a whole-image brightness shift must fail. Also reject excessive calibration drift,
missing cases, wrong settings/extents, nonopaque inputs and altered frozen files. Positive controls
must accept the observed lower/upper bounds inclusively. Do not modify renderer output to satisfy
the verifier.

Report the number of variable pixels/channels, maximum span, exact matches, held-out violations
and all failures. The evidence must expose the empirical blind region, not just a PASS label.

## Scope and limits

This is a bounded empirical admissible range, **not** a confidence interval, equivalence test of
output distributions or proof that culling causes no numerical change. Independent channel
intervals can admit combinations never observed in a control; a mode-correlated defect contained
inside the observed ranges can pass. Exact semantic fixtures and the applicable span ceiling remain
necessary. A fresh off control outside the envelope is a useful failure, not grounds to ignore it.

Calibration is specific to the binary, assets, device/OS, camera/time, reconstruction, dimensions
and settings. Do not reuse this envelope for another scene, sequence or milestone. Broader
changes need their own predeclared invariants and null controls. No finite screenshot set proves
absence of every possible missing sample, temporal flicker or driver defect.

## Tool and custody

[`repeatability.py`](../../Tools/Screenshots/repeatability.py) consumes retained capture manifests;
it does not render. Its JSON lists exactly fifteen named cases, nine absolute calibration paths
per case, and four `{mode, path}` holdout records per case. Include canonical `settings` in both
manifests. The collector must attest actual arguments, fresh processes, validation and runtime
identity; an image verifier cannot recover these facts from pixels or filenames.

```sh
python3 Tools/Screenshots/repeatability.py --selftest
python3 Tools/Screenshots/repeatability.py --calibration /absolute/calibration-inputs.json \
  --freeze-dir /absolute/new-envelope
python3 Tools/Screenshots/repeatability.py --frozen /absolute/new-envelope/envelope.json \
  --holdout /absolute/holdout-inputs.json --output /absolute/new-verification.json
```

Preserve the freeze digest in a separate pre-holdout record. The verifier rechecks source/input
hashes, derives the interval again from originals and refuses changed bounds. The final custody
audit must also check that the freeze digest still matches the pre-holdout record and that all
120/60 scheduled fresh captures completed once with matching commands. Never reuse raw paths
across calibration and holdout; identical image hashes are expected for successful exact repeats.

The [follow-up record](../milestones/m7.2-visibility-followup.md) owns results: exact attachment
fixtures passed, but the independent image gate passed only 9/15 cases, including five off
controls outside calibration. This method is not an adopted acceptance rule in AGENTS.md.
