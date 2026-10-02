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
| 20 | This task commit | Final code gates PASS, 10/10; focused 510 assertions | Two evidence findings corrected; final 12 hashes match |
| 21–22 | Not started | Not run | Not run |

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

Tasks 1–20 are implemented with passing final code gates and stronger reviews. Required native
checkpoints 9 and 19 pass. Tasks 21–22, final GPU/image/evidence gates, whole-branch review,
tag, push and pull request remain incomplete. The record remains Accepted; no owner acceptance
is implied. Both worktrees and all evidence are retained.

## Task 10 binding conflict and authorized exception

Execution stopped before violating the Global passing-commit gate. The record's UX5.2 exit
gate and Task 10 both require unchanged panel cascades and rejection of CLI-invalid pairs
through the same single-key API. Existing CPU/Direct → classify GPU succeeds by selecting
Indirect; GPU/Indirect → submission Direct succeeds by selecting CPU; Direct lighting →
light-check on succeeds by selecting Clustered. The CLI rejects each requested conflicting
pair. Fresh implementer and stronger reviewer independently confirmed no shared operation
can both succeed with that cascade and reject unchanged. Returning an error after mutation
is not a valid rejection.

At the initial stop, the absent-header red build exited 255 (`task10/red-build.log`) and three
CLI probes exited 1 with existing conflict reasons (`task10/binding-conflict-cli.json`). No
production implementation or final gates had run; the owner was asked to resolve the conflict.

On 2026-10-02 the owner authorized the recommendation to preserve shared panel cascades and
document the literal CLI-rejection exception, then continue all remaining work. The original
rejection gate remains unmet for those cascades; implementation and review resume against the
explicit scoped exception in the record and plan. No milestone acceptance is implied.

Task 10 replayed the original five-pair rejection draft against the implemented cascades:
four pairs failed with eight assertions (exit 42, `task10/original-conflict-red.log`), while
temporal-off/render-scale correctly refused the unavailable edit. These original-gate failures
remain failed under the owner-authorized exception; the preserved cascades include clearing
occlusion when CPU classification or visibility off is selected.

## Headless settings representation

Task 11 serializes effective rendering settings for a headless child. Temporal off emits
`--temporal off --render-scale 1`, matching the full-resolution frame; the editor retains its
inactive reconstruction and scale requests. Those dormant values cannot be represented by the
existing CLI while temporal is off and are not claimed to round-trip. Explicit startup lab
overrides are preserved; absent overrides do not replace authored document generator defaults.

Task 11 sequencing deviation: the root created a local commit while the implementer was
adding the final latent-state regression, before its hold message arrived. Task 12 and push
were held. The complete final source then passed all ten `task11-attempt2` gates and fresh
stronger re-review; the local commit was amended only after that verification.

Task 13 adds a terminal error field to distinguish denial, failure and cancellation within
Error state. Awaiting requests are cancelled on disconnect; an already approved immutable
plan retains its approved remaining steps. Terminal entries remain available for response
dispatch. The UI must enable Approve/Deny only for the active awaiting request and copy a
borrowed step before a later submission can invalidate it. Stronger review confirmed this
interpretation against the binding record.

Task 14 adds bounded response closure signals and one mailbox-owned deferred disconnect slot beyond
the 64 visible inbox entries. `pushInbound` still returns false when full. Retention appends or
defers under the mutex; this preserves disconnect cancellation across listener shutdown and the
failed-push/drain race. Process-lifetime connection IDs prevent stale results reaching a new peer.
Focused EOF assertions distinguish actual socket closure from read timeout. Default socket path
length errors are returned by `SessionListener::start`; the path helper itself returns a path.

Task 15 adds an envelope decoder preserving unknown command names for an `invalid` response,
while the earlier strict known-command decoder remains available. Status/readings encoders and
a pure tier-action builder support tests. Its real-App read-only check covered ten queries,
hello-first, unknown-command and tier errors, reconnect at ReadOnly and socket removal. Task 19
later verified Graph and raised-ceiling reconnect reset. Large query replies return an explicit
`unavailable` error; retained Console rows are read without altering the frozen Console view.

Task 16 adds authored-camera and persistence helpers, Inspector units for field attribution and
read-only saved-camera values, shared subject IDs, and linear JSON array traversal in Asset.
Exposure/bloom values are partial objects using document key names; review shows full resulting
objects. Color and cone inputs follow Inspector sRGB/degrees, camera yaw/pitch use radians.
Generated and animation-owned unsavable edits are refused. Empty change sets are refused and
only changed keys are attributed. Stronger review required the existing exposure-feedback and
temporal-reset side effects plus normalized yaw previews; final gates pass and all 25 reviewed
Source/Tests hashes match. Foreign light identities, animated object transforms and inactive
camera subjects are refused without mutation. Task 19 verified saved-camera provenance;
other Inspector field gestures remain unverified.

Task 17 uses shared document replacement directly for session scene opens instead of the
planned DocumentWorkflow Open call, whose ordinary path resets selection and view. It refuses
dirty, playing or busy documents and object/light/node selections; None, Camera and Environment
retain semantic selection and the full editor camera. Session measurement samples the stopped
current view, with cameraTrack false, instead of the operator measurement path that starts
playback and rewinds. These adjustments preserve the Global view/playback constraint.
Rendering provenance is separate from scene provenance so Save/Revert do not clear it.

Task 17 review corrected cancellation ownership, static value/count validation, repeated GPU
output names, Stop between plan steps, delayed directory-symlink handling, document/Quit guards
and capture-result ownership. A root audit also found GPU schema/temp collisions; the added
regression checks all three existing capture outputs without changing RojoRHI or its format.
These are source/test corrections; adversarial native gestures remain unverified.

Task 18 adds private ChildRun ownership and job guards in Measurement/Transport. Module policy
refused linking a private shell source into Tests; standalone probes compile the unchanged
production source instead, preserving the one-source/one-target rule. Fresh stronger review
corrected query.status for headless jobs. Stop/Listen-off/shutdown kill and reap by source and
process probes; Task 19 verified live Stop, while active-child Listen-off/Quit remain unverified.
Final App SHA-256 is
`afccbc79e94a165cba6dc99866ddd303f6828e459643a8ec9e6e8761a3a65c4a`.
The production ChildRun API locates a test-only stub which execs that App, with the same CWD,
arguments and output path as the direct run. Both complete PNGs have SHA-256
`c841052ba9c058444bfc5f8185ec75cde0d7532b1e09559e5970c308ab799bd1`.
The output checks reject existing/symlink leaves; unchanged headless pathname writers do not
provide isolation from concurrent path replacement by another process with the same privileges.

## UX5.2 checkpoint

Task 19's real App passed ceiling raise, settings approval, approved three-step plan, denial,
bridge proposal Accept, child Stop and disconnect mid-approval. `task19/native/validation.json`
retains observations, requests/responses, screenshot hashes and failed interactions. Accept
changed only the saved scene camera, showing attribution and dirty state; editor camera,
selection and Stopped time 0 stayed unchanged. Operator Revert > Discard removed the test edit.
The long capture child PID 2802 existed before toolbar Stop and was absent afterward; its
request returned `cancelled`, status returned idle. Disconnect cancelled the waiting settings
request without applying it; the next connection reset to ReadOnly. A real Window > Render
Graph action made its query available. Native Cmd+Q exited 0 and removed the socket.

The initial checkpoint run reached its configured 100000-frame limit before Accept and was
relaunched without that limit. A stale-window screenshot failed, then the current window
captured successfully. Three optional dock-resize attempts failed; resize and floating-tab
undock remain unverified. The checkpoint bundle logged missing Fonts/Icons and used fallback
typography/system icon; no typography or icon parity is claimed. A measure request during the
child was queued and then cancelled, so its unavailable execution remains unverified.
Listen-off/Quit with an active child, evidence navigation, dirty file-proposal confirmation,
missing-evidence/Error gestures, floating Console and schema 6 relaunch remain unverified.

Fresh Task 19 review found an unusable higher-tier CLI connection, client-name collision,
per-receive timeout, stale live-PID socket discovery and hello-error exit classification.
Test-first corrections add optional `--wait-tier` read-only polling on the same connection;
the operator still raises the ceiling and approves. Default immediate tier refusal remains.
All original red probes and the intermediate fake-server fixture failure are retained.

The final CLI `--wait-tier` workflow also passed in the real App: its named connection waited
for the operator ceiling choice, then awaited real Approve; JSON returned `ok` with exit 0,
and a fresh query read temporal off (`task19/native/cli-wait-tier.json`).

## Console and session export

Task 20 adds actor chips/filter and schema 1 export of ordered actions and the displayed Console
snapshot, with current search/severity and Operator/Agent selected. Multiline payloads remain
one string per entry. Live views refresh; frozen views stay held. Export records an operator
request before the snapshot and the actual write result afterward; a later export includes it.
The last authenticated client is retained after Listen off. Export safely replaces its reserved
`session.json`; ASCII case aliases cannot be used for bridge outputs.

Evidence uses actual file-byte SHA-256: graph, measure, PNG and logs; sequences explicitly hash
their manifest; GPU traces hash every regular payload file plus the schema companion. Required
missing/unsafe/unreadable outputs fail certification and the approval step. Failed/cancelled
jobs omit unwritten outputs, while existing unsafe outputs produce an explicit certification
error. Production-linked ENOENT/ENOEXEC probes verify logs left before failed child launch are
hashed and attached. One consumed terminal result fixes duplicate cancellation records observed
in Task 19. These supporting APIs extend the task's file list to approvals and output validation.
Native Export/Console/evidence gestures remain for Task 22; no GPU payload production is claimed.
