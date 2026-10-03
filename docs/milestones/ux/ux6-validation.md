# UX6 — Execution validation

**Status:** In progress
**Date:** 2026-10-03. The milestone remains `Accepted`; no owner acceptance or merge occurred.

Tasks 1–9 are complete; Task 8 is `c888541` and tag `ux6-exporter`.
Task 17 passed its isolated gates and separate review; integration is pending. Task 11 is running
in its own isolated checkout. Other remaining tasks
are unstarted. The owner requested parallel work and explicitly approved reusing the idle
Task 1 reviewer as Task 17's implementer after the agent interface refused a fresh thread.
Task 9 and Task 17 have independent checkouts and reviewers. Other tasks use fresh agents.

## Source and evidence

- Candidate: sibling `Luminex-ux6`, branch `feat/ux6-scene-authoring`, from local design commit
  `21a704fc6bc5f07e63e832320458dcd7ef12fcb1`.
- Frozen parent: sibling `Luminex-ux6-parent`, `80fa7162aa0ff9b2f97d220927a9a4aca02c4544`.
  App SHA-256: `4fa704c3029c4c1e97ac76509fbba91471dbe8eac4eeba8cf2450b36b63ec352`.
  Task 8 rechecked all 190 App/runtime/source shader hashes against `parent-freeze.json`;
  all matched, and the parent was not rebuilt.
- Raw evidence and ledger: `../Luminex-evidence/ux6/`, relative to the candidate checkout.
- [Exporter validation](ux6-validation-exporter.md) preserves the full Task 1–8 measurements,
  failures, causal evidence, representation limits and owner-authorized resumptions. Its
  original text is also retained in commit `c888541`.

## Per-commit checks

These check build, CPU tests, formatting, regenerated compilation commands and the direct root
policy checkers. Tools changes also run the Python suite. Every xmake command uses `-P .`.
They do not certify App image parity, the complete GPU suite or native gestures. The outside
runner now overlaps independent read-only checkers after build/test/format/database generation;
commands and assertions are unchanged, with distinct logs and no previous-evidence overwrite.

| Task | Commit | Final checks | Evidence |
|---|---|---|---|
| 1 | `7953f4d` | 12/12 passed | `task1-gates.json` |
| 2 | `42cdc696` | 13/13 passed | `task2-gates.json` |
| 3 | `621aebbd` | 13/13 passed | `task3-final-gates.json` |
| 4 | `781539ab` | 13/13 passed | `task4-final-gates.json` |
| 5 | `5e87a1d6` | 12/12 passed | `task5-gates.json` |
| 6 | `3dc475da` | 13/13 passed | `task6-gates.json` |
| 7 | `11264c55` | 12/12 passed | `task7-final-gates.json` |
| 8 | `ux6-exporter` | 13/13 passed; 348 Python tests | `task8-sidecar-final-gates.json` |
| 9 | Retirement commit | 13/13 passed; 348 Python tests | `task9-final2-gates.json` |

## Retained failures and resumptions

Task 8 stopped immediately on its first actual exactness run: TemporalLab's rotating cube
could not encode Euler `[-0, +0, +0]` (bits `80000000,00000000,00000000`) exactly. Numeric
magnitude was zero with a different sign bit. MaterialLab passed with only six axis objects
omitted; LightLab was not run. The owner authorized repair. The signed-zero repair passed the
renewed run: **4,405,255 assertions, zero failures** across all three labs and 1,441 sample times.
LightLab used the disclosed temporary lights-only test route; production retirement is Task 9.
Both MaterialLab paths used the same unavailable-Helmet-mips fallback. This is generator versus
captured-document equality, not parent App image parity. First and renewed logs remain intact.

Three subsequent full CPU gate attempts failed on historical catalog assumptions and triggered
the required stop. The third diagnostic ended at 723 cases (721 passed, two failed), 1,215,768
assertions (1,215,766 passed, two failed), process exit 139, and did not complete. The failures
were a 19-versus-two object fixture and dereferencing node zero's absent generator. The owner
then instructed: "Let's note this somewhere and continue all remainings." Reviewed repairs
preserved historical schema 1/pin assertions and used independent fixtures or unique identities.
The renewed full run found a Session sidecar hash incompatibility; strict schema 2 identity
support was added with current-catalog tests. All historical failures stay recorded. No test,
tolerance, comparison or image reference was loosened.

## Scope additions

Task 8 includes the object-only signed-zero encoder/decoder repair, immutable historical catalog
fixtures, all-six-document directional grouping, and screenshot/Session selftest integration.
The Session client now matches JSON plus animation identity; geometry is excluded as in C++.
These expand the Task 8 file list and are separately reviewed. [Exporter validation](ux6-validation-exporter.md)
records earlier helper-file additions and schema 2 representation limits. No RojoRHI, pass,
shader, instance ABI, MotionClass or workspace/capture/measurement schema changed.

## Gates and gestures still unverified

The unchanged pinned glTF validator passed 107 documents after export (six installed catalog
and 101 writer fixtures). This does not certify full animation-pointer extension semantics.
Tasks 12, 16 and 22 have not run. The standing eight-round App image matrix, five-mode LightLab
images, full current GPU suite, completion-gate tasks and gizmo screenshot/GPU capture checks
remain unverified. No image reference update or image acceptance occurred.

No real App authoring gesture has been exercised: former generated object pose edit, Save,
relaunch/reload, generic glTF viewer opening, static fields/reasons/Enabled persistence, each
gizmo tool and space, Inspector synchronization, Escape and drag-state transitions, point/spot
light operations, shortcuts, RMB flight and final persistence all remain unverified.
Mobility and the gizmo are not implemented yet. No integration tag, push or PR exists.

## Task 9 — Generator retirement

MaterialLab and TemporalLab generators, the one-time exporter and its temporary build permissions
are removed. LightLab now appends lights, pile and orbits around its 17 saved objects; VisibilityLab
keeps its populations. Retired generator/axisStation documents fail with the named schema 2 reason.
Focused Metal checks passed 13,371 assertions in three retirement cases, 142,864 in 81 mixed cases,
and 6,425 in ten actual population/Temporal cases; the initial `[visibility-lab]` filter matched
no case and the correct filter was run separately. These are not the complete GPU gate.

Early test-fixture errors (nonexistent members/types, missing mesh identity and finalized-scene
append guards) and the first GREEN's unsampled orbit expectation are retained in `task9-notes.json`.
The finalized-scene append issue failed twice, not three times; root and separate reviewer audited
the chronology and actual assertion sites. The first full CPU run failed (exit 255); its direct
diagnostic exited 139 after two fixtures assumed LightLab still generated objects. Reviewed
VisibilityLab spawners preserved LightLab light/pile and imported indices plus every assertion;
the two affected tests then passed 78 assertions. The fresh full run passed all 13 commands.
Extra test routes, temporary-module cleanup and obsolete source-comment corrections are recorded
in `task9-notes.json`. Detailed Task 1–8 history was split into the linked exporter page to keep
each documentation page within 300 lines. Image, full GPU and native gates remain Task 12.
