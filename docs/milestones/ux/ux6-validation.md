# UX6 — Execution validation

**Status:** In progress
Owner authorized resuming Task 8 after its three-attempt CPU stop.
**Date:** 2026-10-03. The milestone remains `Accepted`, not `Implemented` or owner-accepted.

Tasks 1–8 are complete through the exporter tag. Task 8's original exactness and three-attempt
CPU failures remain below; owner-authorized repairs passed the renewed gates. Task 17 is running
in an isolated sibling checkout after the owner requested parallel work and approved reusing
an idle agent because the agent interface refused a fresh thread. Separate review still applies.
Tasks 9–16 and 18–22 have not started. No image reference, integration tag, push or PR exists.

## Source and evidence

- Candidate: sibling `Luminex-ux6`, branch `feat/ux6-scene-authoring`, started from the local
  `docs/ux6-design` at `21a704fc6bc5f07e63e832320458dcd7ef12fcb1`.
- Exporter candidate: tag `ux6-exporter` (Task 8); Task 7 is `11264c552068605f2dc7903cdce6efe5785b1a01`.
- Frozen parent: sibling `Luminex-ux6-parent`, `80fa7162aa0ff9b2f97d220927a9a4aca02c4544`.
  Its App SHA-256 is `4fa704c3029c4c1e97ac76509fbba91471dbe8eac4eeba8cf2450b36b63ec352`.
  Tasks 7 and 8 rechecked the App, 126 runtime shader files and 63 source shader files against the
  original `parent-freeze.json`; all 190 hashes matched. The parent was not rebuilt.
- Raw evidence is in `../Luminex-evidence/ux6/`, relative to the candidate checkout. The execution
  ledger is `execution-state.json`. Each task retains RED/GREEN logs, review evidence and gates.
- Task 8's frozen source, test binary, original catalog files, exporter output and measurements
  are under `task8-exact-1/`. `pre-run.patch` and `source-and-test-sha256.json` identify the
  measured implementation. Its Tests binary SHA-256 is
  `ca23b89c3be26d1ecbd8f0d441a1d3aa377cf32dc9e0e69653267e0f9d107faf`.

## Completed per-commit gates

The retained final gate records below all have exit code zero for every command. These are
per-commit checks, not the milestone's image, complete GPU or native gesture gates. Every xmake
command used `-P .`; root policy checkers ran directly from the sibling worktree. The checks
include build, `Tests/unit`, format, regenerated compile commands, submodule pin, project policy,
shader imports, literal colors, module dependencies, source headers, public API comments and
C++ layout. The thirteenth command, where applicable, is the Tools Python suite.

| Task | Commit | Final checks | Evidence |
|---|---|---|---|
| 1 | `7953f4d` | 12/12 passed | `task1-gates.json` |
| 2 | `42cdc696` | 13/13 passed | `task2-gates.json` |
| 3 | `621aebbd` | 13/13 passed | `task3-final-gates.json` |
| 4 | `781539ab` | 13/13 passed | `task4-final-gates.json` |
| 5 | `5e87a1d6` | 12/12 passed | `task5-gates.json` |
| 6 | `3dc475da` | 13/13 passed | `task6-gates.json` |
| 7 | `11264c55` | 12/12 passed | `task7-final-gates.json` |
| 8 | `ux6-exporter` | 13/13 passed | `task8-sidecar-final-gates.json` |

Task 5's hand-authored and actual writer animation-pointer documents passed the pinned
`2.0.0-dev.3.10` glTF validator with zero errors. The writer report retains one warning and
three informational messages, including incomplete extension support; this does not certify
the animation-pointer extension's full semantics. The unchanged document validation run passed
69 documents: six catalog documents and 63 writer fixtures. This predates Task 8's lab exports.

The renewed Task 6 repair passed 927 assertions in 12 CPU cases and 1,236 assertions in seven
GPU cases with `MTL_DEBUG_LAYER=1`. Task 7 reran both successfully. Task 7's final focused tests
passed 208 assertions in seven cases with Metal validation. Its Inspector-style mesh Enabled
route first failed 14 assertions, then passed after the model routing repair; the live/reloaded
enabled equality assertion remains present. Root and separate reviewers approved Tasks 6 and 7.

## Task 8 — First actual exactness run

Both root and a separate strongest-model reviewer inspected the capture and comparison harness
before the run. A static coverage gap was repaired before measurement: material comparisons
now include the presence and slot of each texture binding, alongside material GPU rows and
actual GPU texture pixels. No measured inequality was repaired or omitted.

The command ran from `build/macosx/arm64/release/test/`; `UX6_EVIDENCE` below denotes the
absolute path to the sibling evidence directory:

```sh
MTL_DEBUG_LAYER=1 \
LMX_UX6_EXPORT_ROOT="${UX6_EVIDENCE}/task8-exact-1" \
./Tests '[ux6-equivalence]' --abort
```

`measurement.log` records one failing test case: 551,235 assertions, of which 551,234 passed
and one failed. The process exit code was 42. The log contains an ordinary Catch failure at
`REQUIRE(captured)`; no cause is assigned to the numeric exit value. Aggregate wall duration
was not recorded. This was the first actual run; it was not retried after the failure.

| Lab | Measured result | Scope |
|---|---|---|
| MaterialLab | PASS | Full comparison after removing only the six axis objects |
| TemporalLab | FAIL | Exact pose capture refused; no candidate scene was produced |
| LightLab | NOT RUN | The run stopped before reaching it |

### MaterialLab measurements

The full legacy generator path and captured/read/instantiated document path were compared
independently in the candidate test executable. Both include the imported Helmet and sky.
This is not the frozen-parent App image matrix, which has not run.

The parent-path snapshot has 44 objects; the candidate has 38. All retained objects preserve
order and names. All original geometry and material slots, including unused axis materials,
remain. Snapshot file sizes are equal on both paths:

| Data | Bytes per scene | Exact comparison |
|---|---|---|
| Merged vertices | 768,672 | Passed |
| Merged indices | 218,544 | Passed |
| Material GPU rows | 4,368 (39 rows) | Passed |
| Texture level-zero pixels | 83,919,872 | Passed for all eight textures |

Actual GPU texture dimensions and mip counts were:

| Slot | Dimensions | Levels | Level-zero bytes |
|---|---|---|---|
| 0 | 256 × 1 | 1 | 1,024 |
| 1 | 64 × 64 | 1 | 16,384 |
| 2 | 64 × 64 | 7 | 16,384 |
| 3–7, each | 2048 × 2048 | 12 | 16,777,216 |

Mesh rows, texture formats and bindings, object position/Euler/scale/model/enabled/motion/
emissive strength, mesh/material slots, authored bounds, bounding sphere, shadow role,
directional/local lighting, initial camera and camera/animation fields passed their exact
checks. Object poses and emissive values passed at all 1,441 samples from 0 through 24 seconds
at 1/60-second spacing. MaterialLab has no rigid object tracks, so its raw rigid-key comparison
is empty; this run provides no TemporalLab key equivalence evidence.

Both MaterialLab paths logged that the Helmet's offline baked mip chain was unavailable and
that they computed mips during loading instead. The warning notes that the normal-map fallback
omits offline per-level renormalization. The passing comparison applies to this shared observed
fallback configuration. It does not establish parity with a different offline-bake configuration.

### TemporalLab failure and magnitude

Actual generator output after `animate(0)` has object 1, `temporal-lab rotating cube`, with:

| Field | Measured source | IEEE-754 bits |
|---|---|---|
| `eulerDegrees.x` | `-0` degrees | `0x80000000` |
| `eulerDegrees.y` | `+0` degrees | `0x00000000` |
| `eulerDegrees.z` | `+0` degrees | `0x00000000` |

The exact quaternion encoder returned no value and capture was refused. The source negative
zero has numerical magnitude zero; its sign bit differs from positive zero. There is no
completed TemporalLab candidate, so no candidate pose, pixel delta or full-scene inequality is
claimed. The measured object decoder explicitly canonicalized decoded zero to positive zero,
which prevents this signed-zero source pose from passing its bitwise inverse predicate.

`temporal-lab/capture-refusal.txt` and `temporal-lab/generated/objects.txt` retain the reason
and component bits. The generated vertices, indices, material rows and seven raw rigid-track
snapshots also remain. No zero-sign normalization, seed substitution, tolerance, re-baseline,
decoder correction or retry followed the failure. This exact-capture failure triggers the
owner checkpoint; it is not the axis-station exception.

The separate reviewer verified all eight frozen source/test/binary hashes and all eleven
original catalog snapshot/current catalog hashes after the stop. Catalog files and
`Tools/Screenshots/reference.json` remain unchanged. Task 8's full per-commit gates, canonical
installed-catalog run, commit and `ux6-exporter` tag have not run.

## Execution deviations and limits to retain

- Task 3 added a PNG byte-span reader and tests so verified bytes are decoded without reopening
  a potentially changed file. Task 4 extracted private writer/save helpers for source budgets.
- Task 4 accepts complete canonical geometry-buffer layout, without unused geometry/padding;
  image files must use unique safe names matching direct texture-folder filename stems. These
  are additional schema 2 representation restrictions.
- Task 6's original overflow/factorization issue failed three versions and stopped execution.
  After the owner requested another attempt, the successful repair added a strict authored
  animated-mesh scale domain of absolute component value at most 100. It rejects rather than
  clamps larger scales. Historical failures and interval counterexamples remain in evidence;
  passing sampled rotations are not a universal factorization proof. Core decomposition and
  its existing tolerance were not changed.
- Task 7 added an exact object Euler inverse and double-precision decoder intermediates,
  saved-mesh selection identity support and document Enabled routing. The bounded encoder
  explicitly refuses unrepresentable Euler edits. The original quaternion is retained when
  rotation bits are unchanged. Camera/light decoders and Core were not changed.
- Task 8 temporarily grants Scenes access to the existing stb PNG encoder, without a new xrepo
  dependency or duplicate encoder implementation; this allowance is scheduled for removal with
  the exporter in Task 9. Its LightLab harness models the planned lights-only generator through
  existing light factories, not the still-unretired production field path. That harness did
  not run in the first measurement; production LightLab parity is unverified.
- Task 8 also groups directional lights in Sponza, San Miguel and VisibilityLab and migrates
  those three documents to schema 2 through the canonical writer. This expands its three-lab
  file list to satisfy the record's all-document grouping. Existing nodes, references, light
  roles, animation bytes and retained root order are preserved; mobility remains Task 13.
- Task 8 moves screenshot-tool selftests to immutable pre-export scene/reference fixtures so
  their original exact pin assertions survive live catalog changes. Production parity preflight
  and `reference.json` are unchanged. This expands the Tools file scope; it does not certify
  the current catalog against the historical pins, whose update awaits Task 12.
- Task 8 preserves schema 1 compatibility assertions against all six immutable original
  catalog documents in `Tests/Golden/ux6-schema1-catalog`, adding an explicit six-document
  count. Twelve historical inputs, including the reference, match Task 7's source bytes.

## Gates and gestures still unverified

Tasks 12, 16 and 22 have not run. The eight-round standing App image matrix, five-mode LightLab
images, complete current GPU suite, completion-gate tasks, final integration review and scene-only gizmo screenshot/GPU capture checks are unverified.
No image acceptance or reference update occurred.

No real App authoring gesture was exercised during this execution: former generated object pose
edit/Save/relaunch/reload, generic glTF viewer opening, static fields/reason/Enabled persistence,
every gizmo tool and space, Inspector/gizmo synchronization, Escape, mid-drag state transitions,
point/spot light operations, shortcuts, RMB flight and final Save/relaunch remain unverified.
Mobility and the gizmo have not been implemented yet.

The owner subsequently requested "Please continue". Task 8 resumed to investigate and repair
the known signed-zero capture failure while preserving the first run and its exact assertions.
This authorizes repair of that failure, not an image acceptance or relaxed comparison. Any new
actual exactness failure still stops for the owner.

## Task 8 — Resumed exactness run

The repair preserves signed zero through the object quaternion decoder's algebraically
equivalent pitch expression and adds the opposite signed zero to the object encoder's bounded
search. Camera and direction search neighborhoods are unchanged. No generator, Core transform,
animation sampler, tolerance or exactness assertion changed. Focused tests first failed the
negative-zero bit check (195 passed assertions, one failed), then passed 80,236 assertions in
six cases. Tests now require negative-zero encoding success and exact quaternion signs after
JSON save/read, while retaining positive-zero, cardinal, compound and unsupported-angle checks.

Root and separate reviewers approved the repair and independently verified all eleven frozen
source/test/binary hashes before the renewed measurement. Evidence is `task8-exact-2/`, with
Tests SHA-256 `cad77bf9afd57fb44f4404ff6094c99f56141bb5dff26da60032a3caf99f02cf`.
The same filter, `--abort` and Metal validation ran against the new export directory. The run
passed 4,405,255 assertions in one case, exit zero; all three labs passed. The original failing
run remains unchanged. No aggregate wall duration was recorded.

MaterialLab again passed with only the six axis objects omitted. TemporalLab passed exact rest
poses including negative zero, seven raw rigid tracks, geometry, materials, texture bindings,
actual GPU level-zero pixels and mip counts, bounds, cameras, lights and all 1,441 sampled poses
and emissive values from 0 through 24 seconds. LightLab passed the same exact scope using the
temporary lights-only generator test route; production retirement remains unverified. Both
MaterialLab paths again used the recorded Helmet mip fallback. This is candidate generator
versus captured-document equivalence, not frozen-parent App image parity or image acceptance.

Both reviewers approved installation after stronger immutable-source grouping checks passed
64 assertions and six-document staging checks passed 63 assertions. All eighteen staged file
hashes were verified independently; the three lab sets equal the successful exactness exports.
The catalog now includes the required identity `Lights` group in all six documents, preserving
existing indices and retained root order. Installed canonical/content/export/orientation checks
passed 82,623 assertions in 33 cases with Metal validation. The first full gate built successfully
but the CPU command failed (exit 255): parity selftests copied the changed live catalog while
expecting the unchanged historical reference hashes. No image was rendered. The direct diagnostic
retains 28,039 passing assertions and one failure before abort.

## Task 8 — Three-attempt CPU integration stop

`task8-final`, `task8-final-2` and `task8-final-3` each built successfully, then failed
`xmake test -P . Tests/unit` with exit 255. Later per-commit commands were not run. These failures
concern tests assuming the old live catalog, rather than a new lab exactness comparison.

The first attempt failed the parity selftests' historical document pins. Frozen historical
fixtures preserved every pin and refusal assertion; the repaired selftests passed 10/10 and
the hash suite 8/8. The second attempt found Task 3's schema 1 compatibility section still
reading the migrated catalog. Its unchanged assertions now use all six historical originals,
with a new exact count; the targeted test passed 89 assertions. Both fixes are uncommitted.

The third attempt's direct CPU diagnostic, seed `2175313249`, records:

| Case | Observed failure |
|---|---|
| `AppSceneEnabledTests.cpp:304` | Expected two fixture-generated objects; loaded 19, including the 17 saved LightLab objects |
| `SceneDocumentCompositionTests.cpp:53` | Dereferenced node zero's absent generator; fatal `SIGSEGV` |

`task8-final-3-cpu-diagnostic.log` ends with 723 cases (721 passed, two failed) and 1,215,768
assertions (1,215,766 passed, two failed), then process exit 139. The suite did not complete.
Earlier child-process death-test `SIGABRT` summaries are retained separately and are not counted
as additional parent failures. The third failure reached the mandatory attempt limit. No repair,
build or retry followed it. Task 8 has no commit or exporter tag; Tasks 9–22 remain unstarted.
Root verified all 43 hashes in `task8-cpu-stop-3/source-test-and-catalog-sha256.json`; the separate
reviewer confirmed installed catalog and frozen exactness sources are intact. Live reference
SHA-256 remains `b37bb9b89919b35a354c99945e0603547d6240a260823f707862b6296d2fe20e`.
The owner subsequently instructed: "Let's note this somewhere and continue all remainings."
This authorizes a new repair attempt after the recorded stop and continuation through the
remaining tasks, with verification. Historical failures stay recorded; no comparison, tolerance
or reference changes are authorized by this instruction. Other explicit stop conditions remain.

The renewed repair replaces the enabled-state test's catalog-derived setup with an independent
camera/generator fixture that still creates exactly two objects and two lights. Composition
and reload tests resolve the unique LightLab generator and Truck asset by identity, require
their presence before access and retain the existing capacity/ownership/asset assertions.
Root and separate reviewers approved these four test-file changes. Focused CPU checks passed
4,365 assertions in 15 cases; affected GPU checks passed 292 assertions in six cases with
Metal validation. Historical failures remain intact.

The renewed full run passed twelve commands, then the Python suite passed 338 of 339 tests.
The Session sidecar's historical TemporalLab pin still read the migrated catalog. Review also
found its production hash rejected two buffers and included geometry bytes for geometry-only
documents. Task 8 therefore adds a tests-first Session hash compatibility repair: schema 2
hashes JSON plus animation only, preserving legacy refusal checks and historical pins, with
independent current-catalog tests. This expands the planned file scope. All 31 focused Session
tests passed after 13 failing RED subtests; the fresh full run passed all 13 commands, including
348 Python tests. Root verified all 54 final source/catalog hashes. The unchanged
pinned validator passed 107 documents (six installed catalog documents and 101 writer fixtures).
