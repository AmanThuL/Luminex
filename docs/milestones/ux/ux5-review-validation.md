# UX5 independent review and fix validation

**Status**: Implemented — pending owner review; failed and incomplete gates retained as measured

An independent stronger review examined the UX5 branch on 2026-10-02. Six fix commits followed.
This page records what the review verified, the gates it re-ran, each defect with its fix, and what
remains unverified. It changes no gate result: every failure in the
[validation record](ux5-validation.md) stays recorded as measured,
[ADR 0030](../../decisions/0030-session-protocol-and-trust.md) stays Proposed, the executor plan
stays In progress and no owner acceptance is claimed.

## Scope and identity

| Item | Value |
|---|---|
| Reviewed revision | `09485e5` on `feat/ux5-session` |
| Fix range | `09485e5..6df16f7`, six commits |
| Final revision | `6df16f7` |
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

The review re-ran the recorded gates on the reviewed revision, and the unit and Python suites
again after the fixes. No tolerance, threshold or default changed.

| Gate | Reviewed revision `09485e5` | After the fixes `6df16f7` |
|---|---|---|
| Unit group | PASS, 1,071 cases | PASS, 1,096 cases / 1,683,434 assertions |
| GPU group, Metal debug layer | PASS, 215 cases | Not recorded |
| Format check | PASS | Not recorded |
| Project checkers | PASS | Not recorded |
| Python suite | PASS, 325 tests | PASS, 331 tests |
| Pinned document validator | PASS, 93 documents | Not recorded |
| Read-only check against a windowed editor | Not recorded | PASS, 11 queries / 11 tier refusals |

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

## Limits

- **Shell and panel fixes.** The changes under `Source/App/Shell` and in the Session panel are
  compiled and covered only by model-level unit tests. No test drives `EditorShell`.
- **No native re-run.** No native gesture was exercised after the fixes. The approval click guard,
  the Save refusal notice, tier-drop cancellation, stale-socket recovery in the real App and an
  approved capture child run are unverified natively. The read-only check above is the only run
  against a windowed editor.
- **File Accept.** Accepting a file proposal reloads the document and stales bridge proposals; it
  does not cancel awaiting approvals. Open, catalog, Revert and `scene.open` do.
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

Owner review remains pending.
