# UX5 independent review and fix validation

**Status**: Implemented — owner authorized integration on 2026-10-02; failed and incomplete gates retained as measured

An independent stronger review examined the UX5 branch on 2026-10-02 in two rounds. Six fix commits
followed the first round and two the second. This page records what the review verified, the gates
it re-ran, each defect with its fix, and what remains unverified. It changes no gate result: every
failure in the [validation record](ux5-validation.md) stays recorded as measured, [ADR
0030](../../decisions/0030-session-protocol-and-trust.md) stays Proposed. The owner's later
[authorization](#owner-authorization-and-integration) of integration turns no result into a pass.

## Scope and identity

| Item | Value |
|---|---|
| Reviewed revision | `09485e5` on `feat/ux5-session` |
| First fix range | `09485e5..6df16f7`, six commits |
| Second fix range | `a27b970..7f5d832`, two commits |
| Final code revision | `7f5d832` |
| Last revision holding the executor plan | `a9150d9`, documentation only after `7f5d832` |
| Parent | main at `3259457` |

## What the review verified

By reading the code, the review confirmed four properties of the reviewed revision:

- **Tier enforcement.** A command above the connection's ceiling is refused with `tier` and logged
  before any argument is acted on; nothing is queued.
- **Operator-only approval.** Accept, Reject, Approve, Deny and the ceiling are panel intents. No
  command, flag or environment variable produces one.
- **Drain point.** Requests are drained and approved steps run after native menu commands and
  before document work, outside the ImGui frame and the device frame.
- **Operator state.** A session changes neither selection, the editor camera nor playback.

One finding, that two subjects could share one imported node, was examined and does not hold.

## Gate re-runs

The review re-ran the recorded gates on the reviewed revision, and every gate below on the final
code revision. After the first round, at `6df16f7`, the unit group passed 1,096 cases / 1,683,434
assertions and the Python suite 331 tests. No tolerance, threshold or default changed.

| Gate | Reviewed revision `09485e5` | After the fixes `7f5d832` |
|---|---|---|
| Build, all default targets and Tests | Not recorded | PASS |
| Unit group | PASS, 1,071 cases | PASS, 1,105 cases / 1,683,695 assertions |
| GPU group, Metal debug layer | PASS, 215 cases | PASS, 215 cases / 1,167,008 assertions |
| Format check | PASS | PASS |
| Project checkers | PASS | PASS: project policy, module dependencies with and without `--link`, 210 standalone headers, comments in 424 files, layout of 3,200 definitions in 389 files, submodule pin, literal colors, theme tokens |
| Python suite | PASS, 325 tests | PASS, 331 tests |
| Pinned document validator | PASS, 93 documents | PASS, 93 documents |
| Client self-test | Not recorded | PASS |
| Read-only check against a windowed editor | Not recorded | PASS, 11 queries / 11 tier refusals |
| Offscreen MaterialLab screenshot | Not recorded | PASS, 1280×720 PNG written |

One run of the unit group before `7f5d832` ended with SIGPIPE in the listener's stale-socket test;
F30 records the cause, and the same test order passes three times with the fix.

These re-runs do not replace the failed and incomplete gates listed under
[Unchanged status](#unchanged-status).

## Findings and fixes

"Unit" means a test under `Tests/` or `Tools/tests/` covers the model function the fix added.
"Compiled only" means the change builds and no test or native run exercises it.

| ID | Observed defect | Fix | Verified |
|---|---|---|---|
| F1 | An approval awaiting review outlived a lowered ceiling or a replaced scene | `43d4732`: lowering the ceiling and Open, catalog, Revert or `scene.open` cancel awaiting approvals with `approval.cancel` rows; an approved plan keeps running | Unit for `cancelPending`; shell wiring compiled only |
| F2 | Unknown or repeated argument members were accepted, so a first-match lookup could read a value other than the one on the approval card | `43d4732`: one validator refuses unknown members, repeats at any depth, objects over 64 members and nesting over 32 with `invalid` | Unit |
| F3 | The approval queue was unbounded and kept terminal entries | `43d4732`: a ninth unresolved request answers `busy`; terminal entries leave once reported | Unit |
| F4 | A pending proposal could be evicted once 64 were retained | `43d4732`: only resolved history is evicted; `propose.edits` answers `busy` at 64 pending bridge proposals | Unit |
| F5 | `propose.withdraw` matched the client name, which a later client can reuse | `43d4732`: it matches the submitting connection | Compiled only |
| F6 | Approve or Deny could act on a card that had just replaced another under the pointer; the card showed a zero change count and a screenshot name without `.png` | `43d4732`: clicks are ignored for 0.5 s after the shown approval changes; state-only status line; output named with its suffix | Unit for the guard and both labels; panel compiled only |
| F7 | The client reported a busy editor as a transport failure and accepted an invalid `--timeout` | `cf8c8d1`: an id-0 `busy` answer exits 2; discovery skips a busy editor when another candidate exists; an invalid timeout exits 2 before connecting; `/tmp` when `TMPDIR` is unset | Python suite |
| F8 | Bridge look edits passed only a finiteness check, so a value the reader refuses dirtied the document and broke every later Save | `321f8af`: the reader and the edit batch share one range check; a refused value changes nothing | Unit |
| F9 | Save could replace an external edit before its proposal card existed | `321f8af`: Save hashes the pair on disk before writing and refuses an unreviewed change | Unit for the decision; shell wiring and notice compiled only |
| F10 | Bridge Accept could apply rows other than the ones shown, since exposure and bloom edits merge onto the current look | `321f8af`: Accept re-runs the preview and stales the card when rows differ; the file hash no longer stales bridge cards | Unit for the comparison; shell wiring compiled only |
| F11 | A leftover `.lmx-save-*.tmp` directory deferred the watch forever | `321f8af`: it defers ten polls, warns once and is then ignored | Unit |
| F12 | A replacement preserving size and time was not seen; every poll re-read the glTF and scanned its directory | `321f8af`: the stamp adds inode and device; an idle poll is three `stat` calls; the poll is skipped during a measurement | Unit for the stamp and probe; measurement skip compiled only |
| F13 | A rewrite changing no canonical value produced a zero-row card that blocked Save | `321f8af`: the new hash is adopted with one Console line and no card | Unit for adoption; shell wiring compiled only |
| F14 | The action history was unbounded and `query.log` answered `unavailable` once it passed one response line | `7a711c4`: command kept to 128 bytes and arguments to 4,096 with a truncation note; 10,000 retained actions with a dropped count; `query.log` takes `afterSequence` and returns the newest rows that fit | Unit and Python suite |
| F15 | A non-finite camera or timing value aborted the editor through the JSON writer | `7a711c4`: such a value is written as `"infinite"`, `"-infinite"` or `"nan"` | Unit |
| F16 | Actor names in replies and the export were mixed case | `7a711c4`: `"Operator"`, `"System"`, `"Agent"` | Unit |
| F17 | The headless child inherited every editor descriptor and the launch environment | `e269f19`: close-on-exec is the spawn default and every `LMX_` variable is removed; an accepted client is marked close-on-exec first | Unit for the environment filter; spawn compiled only |
| F18 | A socket file left by a crashed editor blocked Listen | `e269f19`: Listen replaces a socket the same user owns whose connection is refused | Unit with a real socket |
| F19 | Every request built the complete scene tree | `6df16f7`: at most one tree per drain, built only when a command reads it | Compiled only |
| F20 | An approved `measure.run` overwrote the Performance panel's Warmup, Frames and export path and ended mouse look | `6df16f7`: the run carries its own values and leaves camera input alone | Compiled only |
| F21 | An approved, running plan survived the operator's Open, catalog open or Revert, and its remaining steps ran against the new scene | `a9ba4e6`: the three actions, and with Revert a file Accept, are refused with a notice while a plan is Working; the toolbar Stop ends the plan | Unit for `runningPlanRefusal`; shell wiring and notice compiled only |
| F22 | Accepting a file proposal reloaded the scene without cancelling awaiting approvals | `a9ba4e6`: it cancels them with `cancelled: scene replaced`, like Open, catalog, Revert and `scene.open` | Compiled only |
| F23 | `propose.withdraw` matched only the submitting connection, so a reconnected client could not withdraw its own proposals, which still counted against the 64 pending | `a9ba4e6`: it also accepts the same client name once the submitting connection is no longer the live one | Unit for `proposalWithdrawable`; dispatch compiled only |
| F24 | Rejecting a proposal card moved the next card's Accept and Reject under the pointer | `a9ba4e6`: both are ignored on every proposal card for 0.5 s after the list of drawn cards, led by the awaiting approval, changes | Unit for the guard; panel compiled only |
| F25 | A cancelled approval answered only "Approval cancelled", and a disconnect cancelled without `approval.cancel` rows | `a9ba4e6`: the reply carries the cancellation reason; a disconnect records the rows with `cancelled: client disconnected` | Unit for the retained reason; reply and log wiring compiled only |
| F26 | Save refused with "review its proposal first" before any card existed and when the pair was unreadable | `a9ba4e6`: separate messages for a pending card, a card not yet shown and an unreadable pair | Unit |
| F27 | The staging-directory scan was cached on the directory's modification time, so a directory created and removed within one tick stayed reported and raised the 5 s warning | `a9ba4e6`: a positive answer is rescanned on every poll | Unit with a restored directory time |
| F28 | A bridge proposal made during playback that set an orbiting light's position could never be accepted: its before value was the sampled orbit position and Stop restores the captured one | `a9ba4e6`: `propose.edits` answers `unavailable` for that edit while playback is not Stopped; other edits stay proposable | Unit for `animationOwnedEditRefusal`; dispatch compiled only |
| F29 | No test walked every command's argument names, or placed a symlink at the socket path | `a9ba4e6`, `7f5d832`: one test checks each command's documented names and an unknown one; another refuses a regular file, a dangling symlink and a symlink to a stale socket and leaves each intact | Unit |
| F30 | Found while running the unit group: answering a second client that had already left raised SIGPIPE and ended the process, because macOS refuses `SO_NOSIGPIPE` on a socket whose peer closed; the stale-socket probe of another Listen is such a client | `7f5d832`: an arrival whose option cannot be set is closed unanswered | Unit with a real socket; fails without the fix |

## Limits

- **Shell and panel fixes.** The changes under `Source/App/Shell` and in the Session panel are
  compiled and covered only by model-level unit tests. No test drives `EditorShell`.
- **No native re-run.** No native gesture was exercised after the fixes. The approval and
  proposal-card click guards, the Save refusal notices, the running-plan refusal of Open and
  Revert, tier-drop, disconnect and file-Accept cancellation, withdrawal after a reconnect, the
  orbiting-light refusal, stale-socket recovery in the real App and an approved capture child run
  are unverified natively. The read-only check above is the only run against a windowed editor.
- **Withdraw by name.** Once the submitting connection is gone, any later client that reports the
  same name can withdraw its pending proposals. A withdrawal only stales a card.
- **Playback proposals.** The refusal covers the position of a light an orbit track drives.
  Animated object transforms were already refused; no other proposable field is animation-owned.
- **`query.log` window.** A reply holds the newest rows that fit. `omitted` counts older matching
  rows, and no cursor fetches them; a client that needs every row polls before the reply fills.
- **Stale-socket test.** Any same-user socket whose connection is refused is reclaimed, including
  one that is bound and not listening.
- **Disconnect.** An approved plan still runs its remaining steps after its client disconnects, as
  the [validation record](ux5-validation.md#headless-settings-representation) documents.
- **Shared temporary directory.** Between the path check and `bind`, another process can place a
  symlink at the socket path. The same-user evidence replacement race also remains: output checks
  are confinement checks, not a sandbox.

## Unchanged status

Every measured failure stays as the [validation record](ux5-validation.md) states it:

- Gallery exact comparison: Light 0/16 and Dark 0/16.
- Four of the five original CLI rejection pairs failed under the owner-authorized cascade
  exception.
- The initial standing matrix failed 14/15; three additional eight-round attempts each passed
  15/15.
- Native GPU capture certification failed in all four attempts (0/4 passed).
- The original five-case Off gate has undefined case identities and is incomplete; the
  supplemental matrix passes 120/120.
- The unverified gestures are those the validation record enumerates, plus the checks under
  [Limits](#limits).

## Owner authorization and integration

On 2026-10-02 the repository owner authorized integrating UX5 by squash merge of
[pull request #66](https://github.com/AmanThuL/Luminex/pull/66) once CI is green, after this review
and with both rounds of corrections on the branch. The owner did not re-run native gestures, so the
authorization is not a manual verification: the milestone is Implemented, and every failed gate,
incomplete gate and unverified gesture stays as recorded. No tolerance, threshold or default
changed, and ADR 0030 stays Proposed.

- **Retained failures.** The results under [Unchanged status](#unchanged-status) and in the
  [validation record](ux5-validation.md) stand as measured.
- **Retained limits.** The checks under [Limits](#limits) stay unverified. Neither round of fix
  commits has a native re-run; the post-fix gates in [Gate re-runs](#gate-re-runs) were measured
  at `7f5d832`, and later commits change documentation only.
- **Executor plan.** Closed and removed from the published tree. It stays in the history of the
  tag `ux5-review-gates`, placed on `a9150d9`, the last revision that contains it; that tag also
  preserves both fix ranges and the revision the post-fix gates measured.
- **Original chain.** The tag `ux5-integration-chain` stays on `b31baf2` and preserves the
  development chain whose frozen source and runtime the validation record measured.
- **Squash commit.** After the merge, the squash commit's file tree is to be verified against
  the validated branch tip and its identifier recorded; squashing turns no failed gate or scoped
  exception into a pass.
