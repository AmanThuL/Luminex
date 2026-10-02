# UX5 validation

**Status**: In progress

No owner acceptance or integration claim.

Execution uses `feat/ux5-session` in `../Luminex-ux5`, based on `docs/ux5-session`.
The frozen parent is main at `3259457` in `../Luminex-ux5-parent`.
Evidence is retained in `../Luminex-evidence/ux5/`; the executor plan stays in place.

## Commit chain and code gates

| Task | Commit | Final per-commit gates | Stronger review |
|---|---|---|---|
| 1 | `83f25a4` | PASS, Release | No actionable findings |
| 2 | `5631315` | PASS | Lexical cases restored; no final findings |
| 3 | `b434e3d` | PASS | Independent diff oracle corrected |
| 4 | `8b82ddb` | PASS | No actionable findings |
| 5 | `1527f4b` | PASS | Queue overflow correction verified |
| 6 | `5f076ee` | PASS | Error-card detail correction verified |
| 7 | `71b78a7` | PASS, 11/11 | Docking and review-detail corrections verified |
| 8 | `f33c3de` | PASS, 10/10 | Six findings corrected; no final findings |
| 9 | `7f48945` | Final code gates PASS, 11/11; required native checkpoint PASS | Parser and watcher corrections verified; no final findings |
| 10 | This change | Final code gates PASS, 10/10; original rejection gate FAIL under exception | Precision and draw-time parity corrected; no final findings |
| 11–22 | Not started | Not run | Not run |

The gate JSON files retain every command, exit and log. Test-first red results and failed
correction attempts are retained in each task's evidence ledger; a deliberate red test is
not reported as a failed final gate. Initial Debug unit execution was interrupted for costly
CPU IBL generation; full Release gates passed. That Debug run remains incomplete.

## Retained corrected failures

| Task | Failed check or correction | Retained outcome |
|---|---|---|
| 2 | Initial policy tests: 17 failures and 1 error; lexical restoration: 4 failures | Restored adversarial cases; final gates pass |
| 3 | Initial format gate exited 255; diff oracle lacked independent member ordering coverage | Formatting and oracle corrected; final gates pass |
| 4 | Initial project policy found four narration matches | Source comments corrected; final gates pass |
| 5 | Initial build failed on AssetError concatenation; queue overflow correction failed its focused test | Both corrected; final gates pass |
| 6 | Probe include/link/viewport failures and an App null-pointer compile failure; final layout run interrupted with exit 130 | Corrected probe and all code gates pass; native failure remains below |
| 7 | Initial project policy exited 1; two docking implementations failed review/tests | Third docking implementation passed review and gates |
| 8 | Buffer URI expected/optional mismatch; unqualified test namespace each failed to compile | Each corrected on attempt 2; final gates pass |
| 10 | Initial layout check exited 1 on two missing test function separators; review found typed-scale rounding | Both corrected; final gates PASS, 10/10; original rejection failures retained below |
| 9 | Corrected-candidate project policy exited 1 on this draft's Status field | Status corrected; final full gates PASS, 11/11 |

Per-task evidence retains exact errors and test-first red results separately. Task 6's earlier
scroll-access stop and missing capture coverage were superseded by the retained final native
comparison; the failed exact-image measurements were not converted into passes.

## Failed and incomplete native gates

Task 6 Gallery exact comparison failed at 100%: Light 0/16, repeated in three additional
retries with the same per-image differing-pixel counts; Dark 0/16 with mismatched parent views
and partial black areas. No exact parity or causal explanation is claimed. The owner requested
continuation under an exception after the three retries. No tolerance or test was changed.
`task06/native/final-capture-manifest.json` retains captures and hashes.

Task 7's native schema 5 first opening and tab close passed. Session appeared with Console and
Performance; closing Session preserved Console. `task07/native/validation.json` records the
observations and capture hashes. Pending/Error rows, evidence Copy path/Reveal, live/floating
Console relocation and schema 6 relaunch were not exercised at that checkpoint.

## UX5.1 checkpoint

The initial Task 9 native attempt stopped after three locked-Mac errors. Those failures remain
in `task09/native/access-attempts.json`. On resumption the Mac was accessible; the corrected
checkpoint App was frozen again in `task09/resume01/native/` on Apple M3 Max with Metal 4.

The real TemporalLab Inspector Loaded pair hash was
`4eda44f54b3000353e2cb783bb49892af67760f878b7204b130500e3ef884114`, exactly matching the
client's raw glTF-plus-buffer hash. The test pins this observed value.
`temporal-inspector-hash.png` and its JSON transcription retain the evidence.

A Sponza copy was opened through the native File workflow and edited in TextEdit. The first
45 → 60 edit reached review before its sidecar; it correctly appeared as Unknown external
change. The repeat 60 → 65 edit used an external file-watching client to invoke `sidecar`
immediately after TextEdit saved, producing one attributed Lighting client card. This helper
only writes a sidecar; every Accept and Reject was a real operator click. Show displayed the
loaded 45 → proposed 65 row and the Hierarchy ring. The native File menu disabled Save and
Save As while pending. Accept produced queued/applied log rows; selection, editor camera and
Stopped playback remained unchanged.

The accepted live scene was exported with Save As, then the edited source was opened through
File > Open and exported independently. Both `--temporal off --frames 1 --screenshot` BMPs
have SHA-256 `c5f7b1b0b1e9940bec58cf027b353cdac93acefc636c72ba02a992e8a6aa378f`.
`accept-open-comparison.json` retains commands, exits and exact equality. A second TextEdit
65 → 80 proposal was rejected, then native Save restored both glTF and buffer byte-for-byte
to the pre-edit saved pair, including intensity 65 (`reject-save-comparison.json`). No
self-save proposal appeared during the subsequent checkpoint operations.

Native menu accessibility stayed in a tracking state after Escape; its documented Cancel
action restored the save-dialog accessibility tree. An initial paste timed out and one
TextEdit observation returned an invalid capture size; later reads/actions completed.
These are retained interaction failures, not passed attempts.

Unverified: the disabled Save/Save As explanatory tooltip text; Accept with dirty
Discard/Cancel; evidence Copy path/Reveal; activity navigation; missing-evidence and Error
card gestures; live/floating Console relocation and schema 6 relaunch. Task 7's pending
proposal rows and Show are now exercised by this checkpoint. Gallery parity remains failed.

## Deviations and remaining work

Task 2 retains the existing exact Gallery lifecycle label `Agent · working` alongside the
visible `Agent` label; it does not allow arbitrary prefixed strings. Tasks 3, 6, 7 and 8 needed
supporting APIs/tests beyond their file lists; their attempt ledgers
record the files and reasons. Task 8 adds reload validation before the old scene is discarded,
retains the editor camera and semantic selection, and refuses removal/remapping of the selected
subject. These preconditions preserve the Global selection/camera/playback constraint.
Task 9 also corrects Task 8 encoded-buffer observation and malformed-JSON shape handling,
using a public Engine helper backed by its private URI decoder. This is an integration fix;
no rendering pass, shader, RojoRHI, capture format, manifest or dependency change is authorized.

Task 9 resumed code gates pass (11/11), including the Inspector hash pin. Its required
real-App checkpoint passes as described above. Task 9 is committed;
Task 10 code gates and final source review now pass under the recorded exception.
Tasks 11–22, GPU validation, whole-branch review, final image matrix, evidence audit, integration
tag, push and pull request remain incomplete. The milestone record remains Accepted.

All Task 19 gestures remain unverified: ceiling change, settings approval, three-step plan,
denial, bridge proposal Accept, child Stop and disconnect mid-approval. Task 22 native
checks, GPU suite, image matrix and evidence audit were not run. No integration tag, push
or pull request was created. Both worktrees and all evidence are retained.

## Task 10 binding conflict and authorized exception

Execution stopped before violating the Global passing-commit gate. The record's UX5.2 exit
gate and Task 10 both require unchanged panel cascades and rejection of CLI-invalid pairs
through the same single-key API. Existing CPU/Direct → classify GPU succeeds by selecting
Indirect; GPU/Indirect → submission Direct succeeds by selecting CPU; Direct lighting →
light-check on succeeds by selecting Clustered. The CLI rejects each requested conflicting
pair. Fresh implementer and stronger reviewer independently confirmed no shared operation
can both succeed with that cascade and reject unchanged. Returning an error after mutation
is not a valid rejection.

The test-first build exited 255 on the absent new header (`task10/red-build.log`); this is
expected red evidence, not a failed implementation attempt. Three measured CLI probes each
exit 1 with the existing conflict reason (`task10/binding-conflict-cli.json`). No production
Task 10 code changed, no tests were disabled, and no final Task 10 gates were run. The owner
was asked to choose either a documented rejection-gate exception retaining panel cascades
or strict rejection with an authorized panel behavior change. Worktrees and evidence remain.

On 2026-10-02 the owner authorized the recommendation to preserve shared panel cascades and
document the literal CLI-rejection exception, then continue all remaining work. The original
rejection gate remains unmet for those cascades; implementation and review resume against the
explicit scoped exception in the record and plan. No milestone acceptance is implied.

Task 10 replayed the original five-pair rejection draft against the implemented cascades:
four pairs failed with eight assertions (exit 42, `task10/original-conflict-red.log`), while
temporal-off/render-scale correctly refused the unavailable edit. These original-gate failures
remain failed under the owner-authorized exception; the preserved cascades include clearing
occlusion when CPU classification or visibility off is selected.
