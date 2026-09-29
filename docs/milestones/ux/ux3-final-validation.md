# UX3 final integrated validation

**Status**: Implemented — owner authorized integration on 2026-09-29; image gates failed as measured

This closes the executor plan for [UX3](ux3.md). The final automated runs completed on 2026-09-29.
Build, contract, Metal, document and catalog-smoke checks pass; the three image gates remain
failed. The [original validation](ux3-validation.md) and
[editor and icon validation](ux3-editor-validation.md) retain earlier failures and limits. On
2026-09-29 the owner reviewed the result and authorized squash integration; no tolerance,
threshold or rendering claim was approved, and
[ADR 0028](../../decisions/0028-scene-document-contract.md) stays Proposed.

## Tested source and evidence

The annotated tag `ux3-validation` names implementation revision
`7875edd3add62e2b26eb40b26c398e59e8128310`, the frozen run recorded below. Tag `ux3-exporter`
stays at `d5f440abe9f7f6f81548b2c3d1f675a7acaac086` and retains the migration implementation.
Evidence follows the [archive convention](../../guides/evidence-archive.md) under
`../Luminex-evidence/ux3/`, relative to the repository root. `task20-final-identity.json`,
`task20/common-manifest.json` and `task20/independent-final-review.md` record source, binary,
shader, fixture and reference identities.

| Identity | Value |
|---|---|
| Frozen parent source | `f181d8e50a3448938fec51bdd8bfc3ef44297a65` |
| Candidate App SHA-256 (frozen run) | `1b2e655d1b696d129eaa7132fb30beed27fe1410c3adbc0d5ce502ef47ed481a` |
| Parent App SHA-256 | `3b41d4b8401b1cd99887f66cc86d6da01f1addbb48550e5578af1f9d9de8487f` |
| Common manifest SHA-256 (frozen run) | `deb57a8e019fb38a46fe4f07dd233180d9ade133fef8a9852865ebf95a054673` |
| Common manifest SHA-256 (head rerun) | `253cab4f39980c6498937d98a4a58de2fde9d606477aba72bb6a4c240db6aaa6` |
| Unchanged RojoRHI pin | `8da2a79e82ef66ed67d9642f0f3c5a74d20c33a8` |

Release builds ran on Apple M3 Max, macOS 26.7 (25G229), Xcode 26.6 (17F113). GPU work held the
shared lock with `MTL_DEBUG_LAYER=1`; screenshot runs left `LMX_SCREENSHOT_NO_BLOOM` unset. The
source stayed frozen through the core, CPU and GPU phases; rehashing covered 927 tracked entries,
319 frozen inputs and 129 core build products before and after the extras, with no mismatches.
Both extras phases reference the same manifest and candidate App. No performance conclusion
follows from these runs.

### Head rerun

The image gates were rerun on the final head of the branch, because 52 of the 129 metallibs
differed between builds. The cause is unisolated; dependency timestamps re-ran shader compilation.
The rerun manifest is listed above. Its results are identical to the frozen run: 1/12 original off
images, 5/15 original union cases and 10/15 current hashes. Both runs agree, so the tables below
hold for either.

## Completed gates

The core command list is in `task20-final-gates.json`. All eleven commands pass: `xmake -P . -j 6`,
`xmake test -P . Tests/unit`, `xmake format -P . --check`, compile-database generation, six direct
root checkers and `MTL_DEBUG_LAYER=1 xmake test -P . Tests/gpu`; the source-freeze assertion also
passes. Direct root checkers avoid the nested-worktree `xmake policy` root-resolution issue.

| Gate | Result | Evidence under the evidence root |
|---|---|---|
| Core build, CPU suite, format, six root checkers, Metal GPU suite | PASS: 11 commands plus source freeze | `task20-final-gates.json` |
| CPU extras | PASS: 10 commands and 2 byte audits | `task20/cpu/summary.json` |
| Python tools | PASS: 246 tests | `task20/cpu/python-suite.log` |
| TemporalCompare | PASS: 13 tests, 1 skipped (reason not logged) | `task20/cpu/temporal-compare-suite.log` |
| Khronos validation | PASS: 84 documents (6 catalog + 78 writer outputs) | `task20/cpu/document-validator.log` |
| Original documents read/save/read/save | PASS: 8/8 pairs equal the original bytes and both saved generations | `task20/cpu/roundtrip-original-summary.json` |
| Current catalog read/save/read/save | PASS: 6/6 pairs equal the original bytes and both saved generations | `task20/cpu/roundtrip-current-summary.json` |
| Six current scenes, temporal Off, frame 1 | PASS: 6 valid 1280×720 BMPs and graph dumps | `task20/gpu/current-six/summary.json` |
| Original eight scenes plus Sponza/San Miguel at frames 600/3600 | FAIL: 1/12 exact images; PASS: 12/12 graph dumps | `task20/gpu/original-off/summary.json` |
| Original fifteen-case parent-union comparison | FAIL: 5/15 over 8 alternating rounds, 16 batches, 240 captures | `task20/gpu/original-matrix-rounds/summary.json` |
| Current schema-2 reference matrix | FAIL: 10/15 exact; both labs pass all modes, Sponza fails all five | `task20/gpu/current-matrix/parity.json` |

The core commands also cover shader imports, module link dependencies, RojoRHI unit tests and the
frozen checkpoint inventory (`task20/cpu/commands.json`). The roundtrip helper links the frozen
Asset/Core archives and calls the real APIs; all 28 JSON/bin triples agree across original, first
save and second save.

## Image results

All renderer capture subprocesses exit 0. `task20/gpu/summary.json` has fifteen rows: six smoke
commands and six smoke verifications pass, and three image-comparison gates exit 1.

Only MaterialLab frame 1 has identical temporal-off BMP bytes; the eleven other captures differ.
All twelve first-frame graph dumps match, which does not establish graph equality at frames
600 and 3600. In the alternating matrix only MaterialLab's five modes have every candidate hash in
the parent union; the ten Sponza and Helmet cases fail. Each of the sixteen batches scores 0/15
against the historical reference, the parent included. The cause of the differences is the
normalized directional rig documented in
[Parity root cause](ux3-validation.md#parity-root-cause). The current matrix's five Sponza
failures are against historical rows that no current binary reproduces; see below.

The retained captures were rehashed, and 292 GPU evidence logs plus the core unit and GPU logs
contain no Metal or validation errors, error-level lines, assertion failures or crash signatures.

## Reference re-capture

No current build, including the parent, reproduces the five historical Sponza rows of
`Tools/Screenshots/reference.json`. They were re-captured from the corrected head on 2026-09-29
on the device, macOS and Xcode already recorded in the file's provenance block, so one block now
describes all fifteen rows. Two rounds gave identical hashes for all fifteen cases; evidence is in
`../Luminex-evidence/ux3/review-2026-09-29/reference-recapture/`.

## Retained limits

- The one-time exporter missed 496 of 10,845 orientations. Ordinary Save uses the nearest
  quaternion when no exact one exists (see below).
- The original image gates stay failed despite the lab re-baseline, and the historical Sponza
  hashes are superseded by the re-capture rather than reproduced.
- Runtime document-animation and generator-transform restrictions remain as recorded in the
  original validation. Ordinary Save rollback is not crash atomic, and a post-write verification
  failure can leave a completed disk save without in-memory adoption.
- Native verification stands at 62/74 gestures across maximized and 1280×720 windows, with
  twelve unverified rows; the full workflow was not replayed on one build. Dock and switcher
  appearance are unverified, and the FACET artwork is provisional. The QA app adapter is not
  product packaging.
- The MilkTruck baked-key digest test is specific to release builds; debug floating-point code
  generation differs.

## Review corrections

The owner's review on 2026-09-29 produced these behavior changes.

- Ordinary Save falls back to the nearest quaternion when no exact one exists and adopts the
  decoded value, so the document is clean after saving. Migration strictness applies only to the
  one-time export.
- Set Scene Camera wraps yaw into [−π, π].
- An indecomposable sampled asset pose holds the previous pose with one warning instead of
  aborting. STEP scale keys are validated. Pose overrides on mesh-less nodes are rejected, and
  non-finite look values are rejected at export.
- A document without animations writes no companion `.bin`; the empty `material-lab.scene.bin`
  was removed. Save refuses to overwrite a `.bin` the target document does not name, and Save As
  drops stale path metadata when it rekeys.
- Hierarchy caches its tree and clips rows, so VisibilityLab and LightLab scale; tooltips are built
  on hover. Node labels in Inspector fall back to "Node N". Title and tree root both show `Name*`
  when dirty.
- Quit queues behind a ready Save or Revert. Revert's confirmation offers only Discard and
  Cancel. File dialogs open in the active file's folder.
- Only the icon PNG is staged beside the binary.
- Screenshot `compare.py --sequence` accepts manifest v3. The paired bench tools require equal
  document hash and starting population across schema-5 pairs and refuse dirty scored runs.
  Measurement schema 5 records `sceneDocument.dirty`. `parity.py` requires `--documents` for
  schema-1 replays.
- GPU tests write scene documents outside the validated writer-output folder.

Verification of the corrected head is recorded below.

## Corrected head verification

Run on 2026-09-29 against the release build of the corrected head, on the same device.

| Check | Result |
|---|---|
| Build, format, compile database, six root checkers, module link check, shader imports | PASS |
| Python tool suites and TemporalCompare; `parity.py` and `compare.py` selftests in the unit suite | PASS |
| `Tests/unit`, `RojoRHITests/unit`, `MTL_DEBUG_LAYER=1` `Tests/gpu` | PASS |
| Khronos validation: 6 catalog documents and 84 writer outputs | PASS |
| Current reference matrix: three runs with identical hashes, the last against the updated file | PASS 15/15 |

The ten lab rows reproduce unchanged, so the corrections did not alter catalog rendering. The
original-matrix and temporal-off parent gates above keep their failed results.
