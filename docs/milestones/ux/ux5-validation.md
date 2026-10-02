# UX5 validation

**Status**: Implemented — pending owner review

No owner acceptance or integration claim.

Execution uses `feat/ux5-session` in `../Luminex-ux5`, based on `docs/ux5-session`. The frozen parent is main at `3259457` in `../Luminex-ux5-parent`. Evidence is retained in
`../Luminex-evidence/ux5/`; the executor plan stays in place.

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
| 10 | `f323a10` | Final code gates PASS, 10/10; original rejection gate FAIL under exception | Precision and draw-time parity corrected; no final findings |
| 11 | `4c42900` | Final code gates PASS, 10/10; named effective settings round-trip | No actionable findings; dormant values remain editor-only |
| 12 | `0a7f2c0` | Final code gates PASS, 10/10; focused 311 assertions | Bounded-envelope P2 corrected; no final findings |
| 13 | `003e342` | Final code gates PASS, 10/10; focused 69 assertions | Retried stronger review completed; no actionable findings |
| 14 | `9c2a4c6` | Final code gates PASS, 10/10; focused 713 assertions | No final source findings; reviewed hashes retained |
| 15 | `3bed162` | Final code gates PASS, 10/10; focused 373 assertions | Both P2 findings corrected; no final source findings |
| 16 | `b99446b` | Final code gates PASS, 10/10; final guards pass | Three P2 findings corrected; final 25 reviewed hashes match |
| 17 | `3ded3aa` | Final code gates PASS, 10/10; focused 56 assertions | Seven findings and root companion collision corrected; final hashes match |
| 18 | `1b99fd3` | Final code gates PASS, 11/11; final App child/direct PNG exact | Status P2 corrected; eight reviewed hashes match |
| 19 | `319c309` | Final code gates PASS, 11/11; seven required native gestures PASS | Five findings corrected; final review PASS and three Tools hashes match |
| 20 | `2a4ac8b` | Final code gates PASS, 10/10; focused 510 assertions | Two evidence findings corrected; final 12 hashes match |
| 21 | `1f967a4` | Final code gates PASS, 10/10; 22 CLI examples parse | Two documentation findings corrected; fresh stronger review PASS, 12 hashes match |
| 22 | `b31baf2` | Final full code gates PASS, 11/11; pinned validator 93 PASS | Fresh whole-branch source and publication documentation/evidence reviews PASS |

The gate JSON files retain every command, exit and log. Test-first red results and failed correction attempts are retained in each task's evidence ledger; a deliberate red test is
not reported as a failed final gate. Initial Debug unit execution was interrupted for costly CPU IBL generation; full Release gates passed. That Debug run remains incomplete.

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
| 18 | Initial checked-in shell probe violated module ownership; layout read a removed probe file | Wiring removed without policy changes; external production API probes and final gates PASS, 11/11 |
| 17 | Initial App include/string conversion error; three layout gate failures; public-API comments gate failure | Corrected; final code gates PASS, 10/10 |
| 16 | Span fixture compile error; 19 initially-dirty fixture assertions; exact input cone-angle assertion | Fixture baseline corrected; exact stored-value readback used without tolerance; final gates PASS, 10/10 |
| 15 | Fixture accessor/compiler errors; tier log omitted client/resulting ceiling; hidden Console query stayed stale | Red-to-green corrections; final gates PASS, 10/10 |
| 14 | SIGPIPE; comment/layout failures; bounded queues, close retention, closure saturation, weak EOF assertion and drain race | All corrected; final gates PASS, 10/10 |
| 13 | Initial stronger-review attempt failed at model capacity before reviewing | Retried on the same stronger model; completed with no actionable findings |
| 12 | Comment gate reported 24 undocumented enum values; review found quadratic root-key validation | Corrected; final gates PASS, 10/10; no timing measurement claimed |
| 11 | Exact usage-text assertion failed after adding the session flag | Expected usage updated for the new flag; final gates PASS, 10/10 |
| 10 | Initial layout check exited 1 on two missing test function separators; review found typed-scale rounding | Both corrected; final gates PASS, 10/10; original rejection failures retained below |
| 9 | Corrected-candidate project policy exited 1 on this draft's Status field | Status corrected; final full gates PASS, 11/11 |
| 20 | Warning enum typo; tooltip policy failure; production-probe missing spdlog include; incomplete trace/failed-spawn hashes | Corrected test-first; final full gates PASS, 10/10 and fresh stronger review PASS |

Per-task evidence retains exact errors and test-first red results separately. Task 6's earlier scroll-access stop and missing capture coverage were superseded by the retained final
native comparison; the failed exact-image measurements were not converted into passes.

## Failed and incomplete native gates

Task 6 Gallery exact comparison failed at 100%: Light 0/16, repeated in three additional retries with the same per-image differing-pixel counts; Dark 0/16 with mismatched parent
views and partial black areas. No exact parity or causal explanation is claimed. The owner requested continuation under an exception after the three retries. No tolerance or test
was changed. `task06/native/final-capture-manifest.json` retains captures and hashes.

Task 7's native schema 5 first opening and tab close passed. Session appeared with Console and Performance; closing Session preserved Console. `task07/native/validation.json`
records the observations and capture hashes. Pending/Error rows, evidence Copy path/Reveal, live/floating Console relocation and schema 6 relaunch were not exercised at that
checkpoint.

## UX5.1 checkpoint

The initial Task 9 native attempt stopped after three locked-Mac errors. Those failures remain in `task09/native/access-attempts.json`. On resumption the Mac was accessible; the
corrected checkpoint App was frozen again in `task09/resume01/native/` on Apple M3 Max with Metal 4.

The real TemporalLab Inspector Loaded pair hash was `4eda44f54b3000353e2cb783bb49892af67760f878b7204b130500e3ef884114`, exactly matching the client's raw glTF-plus-buffer hash. The
test pins this observed value. `temporal-inspector-hash.png` and its JSON transcription retain the evidence.

A Sponza copy was opened through the native File workflow and edited in TextEdit. The first 45 → 60 edit reached review before its sidecar; it correctly appeared as Unknown
external change. The repeat 60 → 65 edit used an external file-watching client to invoke `sidecar` immediately after TextEdit saved, producing one attributed Lighting client card.
This helper only writes a sidecar; every Accept and Reject was a real operator click. Show displayed the loaded 45 → proposed 65 row and the Hierarchy ring. The native File menu
disabled Save and Save As while pending. Accept produced queued/applied log rows; selection, editor camera and Stopped playback remained unchanged.

The accepted live scene was exported with Save As, then the edited source was opened through File > Open and exported independently. Both `--temporal off --frames 1 --screenshot`
BMPs have SHA-256 `c5f7b1b0b1e9940bec58cf027b353cdac93acefc636c72ba02a992e8a6aa378f`. `accept-open-comparison.json` retains commands, exits and exact equality. A second TextEdit 65
→ 80 proposal was rejected, then native Save restored both glTF and buffer byte-for-byte to the pre-edit saved pair, including intensity 65 (`reject-save-comparison.json`). No
self-save proposal appeared during the subsequent checkpoint operations.

Native menu accessibility stayed in a tracking state after Escape; its documented Cancel action restored the save-dialog accessibility tree. An initial paste timed out and one
TextEdit observation returned an invalid capture size; later reads/actions completed. These are retained interaction failures, not passed attempts.

Unverified: the disabled Save/Save As explanatory tooltip text; Accept with dirty Discard/Cancel; evidence Copy path/Reveal; activity navigation; missing-evidence and Error card
gestures; live/floating Console relocation and schema 6 relaunch. Task 7's pending proposal rows and Show are now exercised by this checkpoint. Gallery parity remains failed.

## Deviations and remaining work

Task 2 retains the existing exact Gallery lifecycle label `Agent · working` alongside the visible `Agent` label; it does not allow arbitrary prefixed strings. Tasks 3, 6, 7 and 8
needed supporting APIs/tests beyond their file lists; their attempt ledgers record the files and reasons. Task 8 adds reload validation before the old scene is discarded, retains
the editor camera and semantic selection, and refuses removal/remapping of the selected subject. These preconditions preserve the Global selection/camera/playback constraint. Task
9 also corrects Task 8 encoded-buffer observation and malformed-JSON shape handling, using a public Engine helper backed by its private URI decoder. This is an integration fix; no
rendering pass, shader, RojoRHI, capture format, manifest or dependency change is authorized.

Tasks 1–22 are implemented; required native checkpoints 9 and 19 remain passing. Task 22 source/GPU/evidence checks and the final source review are recorded below with failed
and incomplete image/native gates. Fresh publication review passes; root records publication-policy, tag/push/PR outcomes in the external `task22/` publication ledger. The record is Implemented pending owner review; no owner
acceptance is implied. Both worktrees and all evidence are retained.

## Task 10 binding conflict and authorized exception

Execution stopped before violating the Global passing-commit gate. The record's UX5.2 exit gate and Task 10 both require unchanged panel cascades and rejection of CLI-invalid pairs
through the same single-key API. Existing CPU/Direct → classify GPU succeeds by selecting Indirect; GPU/Indirect → submission Direct succeeds by selecting CPU; Direct lighting →
light-check on succeeds by selecting Clustered. The CLI rejects each requested conflicting pair. Fresh implementer and stronger reviewer independently confirmed no shared operation
can both succeed with that cascade and reject unchanged. Returning an error after mutation is not a valid rejection.

At the initial stop, the absent-header red build exited 255 (`task10/red-build.log`) and three CLI probes exited 1 with existing conflict reasons
(`task10/binding-conflict-cli.json`). No production implementation or final gates had run; the owner was asked to resolve the conflict.

On 2026-10-02 the owner authorized the recommendation to preserve shared panel cascades and document the literal CLI-rejection exception, then continue all remaining work. The
original rejection gate remains unmet for those cascades; implementation and review resume against the explicit scoped exception in the record and plan. No milestone acceptance is
implied.

Task 10 replayed the original five-pair rejection draft against the implemented cascades: four pairs failed with eight assertions (exit 42, `task10/original-conflict-red.log`),
while temporal-off/render-scale correctly refused the unavailable edit. These original-gate failures remain failed under the owner-authorized exception; the preserved cascades
include clearing occlusion when CPU classification or visibility off is selected.

## Headless settings representation

Task 11 serializes effective rendering settings for a headless child. Temporal off emits `--temporal off --render-scale 1`, matching the full-resolution frame; the editor retains
its inactive reconstruction and scale requests. Those dormant values cannot be represented by the existing CLI while temporal is off and are not claimed to round-trip. Explicit
startup lab overrides are preserved; absent overrides do not replace authored document generator defaults.

Task 11 sequencing deviation: the root created a local commit while the implementer was adding the final latent-state regression, before its hold message arrived. Task 12 and push
were held. The complete final source then passed all ten `task11-attempt2` gates and fresh stronger re-review; the local commit was amended only after that verification.

Task 13 adds a terminal error field to distinguish denial, failure and cancellation within Error state. Awaiting requests are cancelled on disconnect; an already approved immutable
plan retains its approved remaining steps. Terminal entries remain available for response dispatch. The UI must enable Approve/Deny only for the active awaiting request and copy a
borrowed step before a later submission can invalidate it. Stronger review confirmed this interpretation against the binding record.

Task 14 adds bounded response closure signals and one mailbox-owned deferred disconnect slot beyond the 64 visible inbox entries. `pushInbound` still returns false when full.
Retention appends or defers under the mutex; this preserves disconnect cancellation across listener shutdown and the failed-push/drain race. Process-lifetime connection IDs prevent
stale results reaching a new peer. Focused EOF assertions distinguish actual socket closure from read timeout. Default socket path length errors are returned by
`SessionListener::start`; the path helper itself returns a path.

Task 15 adds an envelope decoder preserving unknown command names for an `invalid` response, while the earlier strict known-command decoder remains available. Status/readings
encoders and a pure tier-action builder support tests. Its real-App read-only check covered ten queries, hello-first, unknown-command and tier errors, reconnect at ReadOnly and
socket removal. Task 19 later verified Graph and raised-ceiling reconnect reset. Large query replies return an explicit `unavailable` error; retained Console rows are read without
altering the frozen Console view.

Task 16 adds authored-camera and persistence helpers, Inspector units for field attribution and read-only saved-camera values, shared subject IDs, and linear JSON array traversal
in Asset. Exposure/bloom values are partial objects using document key names; review shows full resulting objects. Color and cone inputs follow Inspector sRGB/degrees, camera
yaw/pitch use radians. Generated and animation-owned unsavable edits are refused. Empty change sets are refused and only changed keys are attributed. Stronger review required the
existing exposure-feedback and temporal-reset side effects plus normalized yaw previews; final gates pass and all 25 reviewed Source/Tests hashes match. Foreign light identities,
animated object transforms and inactive camera subjects are refused without mutation. Task 19 verified saved-camera provenance; other Inspector field gestures remain unverified.

Task 17 uses shared document replacement directly for session scene opens instead of the planned DocumentWorkflow Open call, whose ordinary path resets selection and view. It
refuses dirty, playing or busy documents and object/light/node selections; None, Camera and Environment retain semantic selection and the full editor camera. Session measurement
samples the stopped current view, with cameraTrack false, instead of the operator measurement path that starts playback and rewinds. These adjustments preserve the Global
view/playback constraint. Rendering provenance is separate from scene provenance so Save/Revert do not clear it.

Task 17 review corrected cancellation ownership, static value/count validation, repeated GPU output names, Stop between plan steps, delayed directory-symlink handling,
document/Quit guards and capture-result ownership. A root audit also found GPU schema/temp collisions; the added regression checks all three existing capture outputs without
changing RojoRHI or its format. These are source/test corrections; adversarial native gestures were unverified at that checkpoint (Task 22 observations follow below).

Task 18 adds private ChildRun ownership and job guards in Measurement/Transport. Module policy refused linking a private shell source into Tests; standalone probes compile the
unchanged production source instead, preserving the one-source/one-target rule. Fresh stronger review corrected query.status for headless jobs. Stop/Listen-off/shutdown kill and
reap by source and process probes; Task 19 verified live Stop, while active-child Listen-off/Quit were unverified then; Task 22 exercises both below. Final App SHA-256 is
`afccbc79e94a165cba6dc99866ddd303f6828e459643a8ec9e6e8761a3a65c4a`. The production ChildRun API locates a test-only stub which execs that App, with the same CWD, arguments and
output path as the direct run. Both complete PNGs have SHA-256 `c841052ba9c058444bfc5f8185ec75cde0d7532b1e09559e5970c308ab799bd1`. The output checks reject existing/symlink leaves;
unchanged headless pathname writers do not provide isolation from concurrent path replacement by another process with the same privileges.

## UX5.2 checkpoint

Task 19's real App passed ceiling raise, settings approval, approved three-step plan, denial, bridge proposal Accept, child Stop and disconnect mid-approval.
`task19/native/validation.json` retains observations, requests/responses, screenshot hashes and failed interactions. Accept changed only the saved scene camera, showing attribution
and dirty state; editor camera, selection and Stopped time 0 stayed unchanged. Operator Revert > Discard removed the test edit. The long capture child PID 2802 existed before
toolbar Stop and was absent afterward; its request returned `cancelled`, status returned idle. Disconnect cancelled the waiting settings request without applying it; the next
connection reset to ReadOnly. A real Window > Render Graph action made its query available. Native Cmd+Q exited 0 and removed the socket.

The initial checkpoint run reached its configured 100000-frame limit before Accept and was relaunched without that limit. A stale-window screenshot failed, then the current window
captured successfully. Three optional dock-resize attempts failed; resize and floating-tab undock remain unverified. The checkpoint bundle logged missing Fonts/Icons and used
fallback typography/system icon; no typography or icon parity is claimed. A measure request during the child was queued and then cancelled, so its unavailable execution remains
unverified. Listen-off/Quit with an active child, evidence navigation, dirty file-proposal confirmation, missing-evidence/Error gestures, floating Console and schema 6 relaunch
remain unverified.

Fresh Task 19 review found an unusable higher-tier CLI connection, client-name collision, per-receive timeout, stale live-PID socket discovery and hello-error exit classification.
Test-first corrections add optional `--wait-tier` read-only polling on the same connection; the operator still raises the ceiling and approves. Default immediate tier refusal
remains. All original red probes and the intermediate fake-server fixture failure are retained.

The final CLI `--wait-tier` workflow also passed in the real App: its named connection waited for the operator ceiling choice, then awaited real Approve; JSON returned `ok` with
exit 0, and a fresh query read temporal off (`task19/native/cli-wait-tier.json`).

## Console and session export

Task 20 adds actor chips/filter and schema 1 export of ordered actions and the displayed Console snapshot, with current search/severity and Operator/Agent selected. Multiline
payloads remain one string per entry. Live views refresh; frozen views stay held. Export records an operator request before the snapshot and the actual write result afterward; a
later export includes it. The last authenticated client is retained after Listen off. Export safely replaces its reserved `session.json`; ASCII case aliases cannot be used for
bridge outputs.

Evidence uses actual file-byte SHA-256: graph, measure, PNG and logs; sequences explicitly hash their manifest; GPU traces hash every regular payload file plus the schema
companion. Required missing/unsafe/unreadable outputs fail certification and the approval step. Failed/cancelled jobs omit unwritten outputs, while existing unsafe outputs produce
an explicit certification error. Production-linked ENOENT/ENOEXEC probes verify logs left before failed child launch are hashed and attached. One consumed terminal result fixes
duplicate cancellation records observed in Task 19. These supporting APIs extend the task's file list to approvals and output validation. Native Export/Console/evidence gestures
were deferred to Task 22; its results follow below. No GPU payload production was claimed at Task 20.

Task 21 documentation passes the external link/anchor/budget/CLI checker and ten code gates. Fresh Astra review corrected authored-camera IDs and rejected-hash lifetime, then
verified all twelve corrected document hashes. Built-in role dispatch reached its thread limit; fresh ephemeral CLI roles preserved model separation. The installed CLI rejected
Astra as too old; the already bundled CLI completed both stronger reviews. No environment upgrade was performed.

## Task 22 integration corrections and final evidence

The final source is frozen in `task22/fixes2/`; App SHA-256 is `8fdfd9ea902fcf1b8f5c2771f3d0ec61dbc6bf6bf5ee87826b2f97c58a1f4eee`. Fresh Astra whole-branch source review
`task22/whole-review-final2.md` passes with no actionable findings. Its source-target inventory preceding this final documentation update independently matches 797/797 hashes, 643/643 Source/Tests and 120/120 changed hashes (90 Source,
24 Tests, 6 earlier branch Tools changes), with exact membership and diffs. Whole source diff SHA-256 is `8b06aae3737d144de5695e8077fb699867c8e8cabc088a5cb32f943471faa367`;
acceptance fixes are `1c48916db8fbd5188ac51277cef5a90c6d27e13a2ded29b35c723a3a70cdf12e`. The reviewer inspected implementer results without rerunning tests/UI/GPU.
Root prepublication `task22/publication-source-runtime-audit.json` matches all 797 source and 136 frozen runtime hashes; no extra RojoRHI or shader changes are present.
Fresh publication review `task22/publication-review.md` passes with no actionable findings, verifying all seven documents, source/runtime hashes, gates and retained failures before these closing metadata updates.

Corrections resolve all five findings from two whole-branch reviews plus the root's malformed-fixture finding: camera queries encode positive infinite `farZ` as `"infinite"`
and test a real omitted-`zfar` document; complete proposal/approval values wrap with measured heights; completed writer receipts/path/hash/coherent before/after stamps govern
save-watch suppression on both outcomes. Pre-write failures preserve external changes; successful uncertified adoption invalidates the baseline. Canonical-read A/external-hash B
fails before model adoption, including calls without receipt output. Invalid sidecar UTF-8 is rejected before retention; result/error encoders return safe failures. A real
listener survives failed proposal/log responses and then answers status on the same connection. Malformed outputs use a separate negative-fixture directory with scoped cleanup.
These supporting model/save/layout APIs extend the task file lists for integration correctness; no negative tests or validator were disabled.

| Retained attempt (`task22/` unless stated) | Result and correction |
|---|---|
| `fixes/camera-red.log` | Exit 134 finite-number assertion; explicit infinite encoding corrected it. |
| `fixes/negative-fixture-red.log`, `fixes/validator-red.log` | Exit 42 / validator exit 1 INVALID_JSON among 82 writers; old bytes archived as `malformed-candidate-before.gltf`, then isolated outputs cleaned. |
| `fixes/save-layout-red-build.log`, `fixes/save-red-build.log`, `fixes/save-stamp-race-red-build.log` | Exit 255 absent measured-layout, writer-receipt and coherent-stamp APIs; tests preceded implementation. |
| `fixes/green-build.log` → `green-build-attempt2.log` | Catch2 logical-OR decomposition failed; parenthesized expression passed. |
| `task22-fixes-gates.json` → `task22-fixes-attempt2-gates.json` (evidence root) | First 9/10: task-number test tags failed narration policy; semantic tags corrected it, second 10/10. |
| `fixes/validator-green.log`, `validator-final-green.log`, `full-unit-final.log`, `validator-complete-writer-green.log` | Complete 89 passed; later focused save cleanup reduced writers to 64 and passed 70. Full unit regenerated every fixture; complete 89 passed again. Partial 70 is retained. |
| `fixes2/sidecar-red.log`, `result-red.log`, `error-red.log`, `listener-red.log` | Four distinct RED cases exited 134 on disk metadata/result/error/listener invalid UTF-8; admission/serialization corrected them; malformed sidecar retained. |
| `fixes2/save-success-red-build.log`, `verification-race-red-build.log` | Missing adopted flag exited 255; signature-only build passed with unused-parameter warning before behavior was corrected. |
| `fixes2/save-success-red.log`, `verification-race-red.log` | Both exited 42 for Save and Save As: external B suppressed after success, or read A/hash B adopted. Ownership and writer-hash checks corrected both. |
| `fixes2/green-build.log`, `focused-green.log`, `task22-fixes2-gates.json` (evidence root) | First implementation build passed; 314 assertions / 11 cases PASS; final source gates 10/10 PASS. Existing signed-send/aggregate warnings retained. |
| `fixes2/validator-complete-writer-green.log` | All 93 valid: 6 catalog + 87 full-unit-generated writers. Corpus remains complete; no later focused reset. |

Final source gates cover build, full unit (9.063 s), format, compile commands, project policy, modules, headers, API comments, layout (3,135 definitions / 389 files) and submodule
pin. These fixes2 results precede this documentation update. Final `task22-final-gates.json` (evidence root) passes all 11 code gates, including Python 325 (6.368 s) and full unit (9.707 s).
The post-unit `task22/validator-publication.json` passes 93 documents (6 catalog + 87 writers), exit 0, 5.647 s. Fresh Astra publication review `task22/publication-review.md` passes; exact reports/logs remain retained. Root records publication-policy/tag/push/PR outcomes in the external `task22/` publication ledger. Root preflight commit policy passes over 1,036 files; root reruns it after this commit so the final subject/body are covered before push.

Sandbox attempts were stopped and retained, then repeated on the real host (`root-sandbox-correction.md`): GPU invocation and verbose retry both xmake 255/Catch2 42, no Metal,
0/215 cases and 108/323 assertions passed; read-only exit 1, SDL no displays/no socket/zero queries. Python exit 1: 325 tests, 33 failures with Unix socket bind denied. Pinned validator
exit 1: CPU-info unreachable-code aborts yielded 83 invalid reports (6+77). Standing exit 1 produced incomplete reports across eight rounds. Host Python passed 325; host read-only and
GPU passed. Host validator then exposed the actual candidate INVALID_JSON (6+82), fixed above. First host standing invocation omitted `--documents` and produced no parity files;
explicit paths corrected it. External Off comparator had absent-module RED exit 1 then four tests PASS; ledger RED exit 1 correctly refused missing/incomplete evidence entries.

Initial App `4e3053569f37b3f41ab01711c0daa651905a76cca4424624cd7ab5e679922b17` and first corrected App
`4e23a60067a150676321aa08ff0d0b20aeac80f5edf8518d55bd76f9f248034b` had host GPU/read-only, standing15/15 across eight rounds and Off120/120 passes; these cover earlier source.
Final frozen GPU `gpu-accepted.result.json` passes 215 cases /1,167,008 assertions, 46.349 s inner runner (46.791 s outer). Read-only's external frozen-copy adaptation changes only
source/build paths; it preserves query/refusal/approval logic. Final read-only passes 11 queries and 11 tier refusals, App exit 0/socket removed, 8.134 s outer (`read-only-accepted.log`); Graph is unavailable because no publication exists in that run. Native published Graph remains separately passing. Final Python passes all 325 tests in 6.368 s. Prior host Python and sandbox failures remain recorded separately.

The unchanged standing tool's first final-App run (`standing-accepted/summary.json`) is COMPLETE: 8 rounds, 240 captures, 14/15 FAIL. MaterialLab TAA scale 1 candidate
`e1123952034d523b3a05debb3a7a05fb10ef8d99ba1d90b2c229f244791f14fa` is absent from all eight parent observations. No cause or tolerance change is claimed. Standing checks
membership in the eight-parent hash set, not same-round equality. All three additional eight-round attempts (`standing-retry-1.json` through `standing-retry-3.json`) are each COMPLETE, 240 captures, 15/15 PASS, exit 0 on the same final App; the initial 14/15 FAIL remains unchanged. Historical-reference measurements are retained separately.
The binding five-case literal-Off gate has undefined case identities and is INCOMPLETE. Supplemental choices are CPU/cull/indirect, CPU/off/indirect, CPU/cull/direct,
CPU/cull/batched and GPU/cull/indirect × Sponza/MaterialLab/TemporalLab, literal Off, scale 1, 32 frames, eight rounds, strict same-round hashes; final supplemental matrix passes 120/120 exact same-round pairs, 240 captures, complete/allMatched/runtimeUnchanged true, exit 0, 239.472 s outer (`off-accepted/summary.json`, `off-accepted.json`). The undefined literal gate remains incomplete.

Root's real native App PID 70398 matches the final hash with Fonts/Icons staged (`native/bundle-freeze.json`). Omitted-zfar LightLab query returned `"infinite"`; node 1 selection,
editor camera and Stopped time 0 remained unchanged. `native/final-native-result.json` retains outcomes and interaction failures. Real operator Apply ceiling and Approve ran immutable six-step rows, all fully readable at full width. Settings Off,
graph dump and measure 8 passed; GPU certification failed and stopped the plan before its child steps. A separately approved two-step screenshot/sequence plan passed. `native/direct-shot-result.json` confirms the final LightLab-derived document at Off/1 frame matches the direct CLI run with identical arguments except output path, exit 0, complete PNG SHA-256 `3c4629cb54d99907b30c7c3786bad91f3271c139a4959a48c153754bfcfaf954`. This is a final LightLab integration check; Task 18's earlier TemporalLab gate remains historical, not relabeled.
Native unsafe names `../bad`, `.hidden`, empty and reserved `SESSION.JSON` returned invalid before approval.
GPU capture original plus three real approved retries all FAIL (0/4 passed): Metal traces contain internal relative symlinks `MTLBuffer-269315-0`, then `MTLBuffer-4185-0`; certification
refuses unsafe GPU entries. The owner authorized continuation after three further failures. No check, tolerance, RojoRHI or trace format was weakened; certification remains failed.

Real Export clicks wrote schema 1/protocol 1 `session.json`. Initial audit passed 6/6 evidence paths, 40 ordered actions / 40 Console entries; after Listen off, `native/export-audit-final.json`
passes 8/8 actual-byte hashes, 47 ordered actions / 47 Console entries, including the cancelled child's log and existing manifest. Exactly one request 21 cancelled approval.result
occurs at sequence 46; `review\nmultiline` remains one Console entry 33. Native Info/Operator+Agent/System-off Copy visible reported 39 matching and preserved multiline display.
Clipboard bytes remain UNVERIFIED (`consoleExact:null`): three TextEdit attempts failed in total: New Document/plain-text/paste/save returned `noWindowsAvailable`, then one
bundle GetApp and one name GetApp returned `timeoutReached` (two timeout calls). Initial Open-panel GetApp succeeded; unsupported `listWindows` was separate. A CUA App-changed guard required refreshing the binding before action. Desktop accessibility/screenshots worked; this was not a locked Mac. Initial Reveal was clicked but Finder showed Desktop only; that failed observation is retained and superseded only by the successful relaunch navigation below.

Actual child 70901 was live before real Stop Listening and absent afterward (ps exit 1); socket absent and client disconnected. Real Listen reopened and the next client was ReadOnly.
`native/malformed-native.json` passes all four actual cases: omitted hello, invalid JSON and invalid UTF-8 each one error line; 2 MiB BrokenPipe clean close; next hello succeeds
for each. New real Apply/Approve started child 70991 (2,000 frames/10,000 warmup); native Cmd+Q removed parent 70398 and child 70991, removed socket, returned one cancelled ID 3
response and launcher exit 0. Before/after process records and `requests.jsonl`/`quit-requests.jsonl` retain evidence; existing cancelled capture outputs are not claimed absent.

Native schema 6 relaunch PID 77717 uses the same final App/Fonts/Icons and existing accepted-frozen INI: Session is visibly selected, docking restores at 100%, client None/ReadOnly (`native/schema6-relaunch.png`). Real Export then Reveal selects the exact `session.json` in Finder (`native/reveal-native-ax.txt`); real Copy path, Finder Cmd+Shift+G and Cmd+V expose the exact path in its text field (`native/copy-path-native-ax.txt`). This verifies path bytes, not Console clipboard bytes. Real Cmd+Q exits 0, removes socket and leaves no relaunch process. Separate relaunch artifacts preserve the original run.

Remaining unverified: Listen off or Quit with a still-awaiting approval (Task 22 covered running children with connected clients; Task 19 disconnect mid-approval remains PASS);
exact Console clipboard equality; narrow/full-value panel layout and floating Console;
dirty file Accept Discard/Cancel, Save tooltip, missing-evidence/Error gestures, activity navigation and unavailable measurement execution during a child. Earlier Task 9/19
required passes remain; earlier lock failures remain historical only. Fresh publication review passes; root records publication-policy/tag/push/PR outcomes in the external ledger. Owner acceptance remains pending. All Global authority/view/playback,
module/comment/color, no-RHI/dependency/renderer/shader/capture-format/manifest, tests-first and retained-failure constraints remain; exceptions do not authorize acceptance.

## Post-publication documentation reconciliation

Task 22 was published as `b31baf2`, pushed with `ux5-integration-chain`, and opened as [PR #66](https://github.com/AmanThuL/Luminex/pull/66); owner acceptance and merge remain pending.
Main advanced to docs-only `bbb108d`, replacing M9 with G1–G3. Root's no-commit merge exited 1 with five documentation conflicts: AGENTS, UX5 record/plan, roadmap and neural roadmap.
Reconciliation keeps every upstream geometry decision and UX5's reviewed implementation, exceptions, validation and In progress plan. The published tag stays at `b31baf2`;
it is not force-moved. Its original 11/11 gates (full unit 9.707 s), 93-document validator and frozen source/runtime evidence remain historical exact results.
This documentation reconciliation syncs main. Root retains actual follow-up `task22-main-sync-gates.json`, distinct validator, fresh Astra review and merge-commit/publication
command outcomes in the external `task22/merge-main/` ledger. Source/runtime bytes remain unchanged; no new source, runtime, owner-acceptance or scored-gate claim follows from the sync.
Closing measured metadata after fresh content review: initial main-sync gates passed 10/11, exit 1, because the unmerged index listed the single active plan twice (`task22-main-sync-04-python3.log`, evidence root). Root staged the five resolved conflicts; `task22/merge-main/policy-staged-retry.json` passes 1,050 files without checker/status changes.
Auxiliary diff check against published HEAD exited 2 on 13 upstream Frozen research Markdown hard-break lines; against incoming `origin/main` it exited 0 (`task22/merge-main/review.md` and retained logs). Research bytes and tolerance remain unchanged.
Fresh Astra content review passes before this closing outcome metadata. Root's `task22/merge-main/final-gates.json` retains the first-run failure and explicit retry provenance, final 11/11 PASS: unit 10.162 s (10.163 total), Python 325/6.636 s, layout 3,135 definitions/389 files. Distinct `merge-main/validator.json` passes 93 = 6+87, 6.402641 s; original Task 22 results remain historical.
