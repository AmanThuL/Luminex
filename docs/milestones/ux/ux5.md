# UX5 — Agent Session

**Status**: Implemented — pending owner review; failed and incomplete gates retained

Written on 2026-10-01 from the merged UX4 editor and a survey of its document, settings, console
and menu code. UX5 lets work done by an external agent reach the operator inside the editor: a
changed scene file becomes a proposal to review, a local socket carries commands under permission
tiers and operator approval, and every action is logged with its evidence. No agent runs inside the
editor. [Part IV](../../roadmap/editor-experience.md#ux5--agent-session) owns the outcome and
gates; this record keeps the design, and later its evidence and limits.

**Placement:** [UX4](ux4.md) → **UX5** → N1. On 2026-10-01 the owner placed all three slices before
N1, replacing the earlier proposal that split them around it.

**Why this shape.** Agents already change Luminex through documents, the CLI and pull requests.
The operator sees the result only after the fact, and the editor cannot tell an agent's change from
its own state. UX4 reserved the vocabulary (the agent actor, proposed and agent-applied marks, the
proposal card and the attention ring); UX5 gives it real consumers and keeps the operator's
approval between an agent and every change.

## Observed state before UX5

- Nothing watches the open document. An external edit is invisible until Revert or Open, and Save
  does not compare the file on disk with the loaded hash, so it overwrites an external edit.
- `documentDirty` compares canonical JSON and buffer bytes and returns a boolean. No function
  reports which nodes or fields differ.
- The Rendering panel writes `EditorRenderSettings` fields directly and applies dependent changes
  inline. `AppOptions` holds a second encoding of the same conflicts for the CLI.
- `MenuCommand` covers 23 commands. Measure, the transport, graph dump and every rendering setting
  are outside it.
- The proposal card, attention ring, proposed-value row and Console actor chip exist only as
  private specimens in the Style Gallery. `actorMark`, `provenanceMark` and `activityStrip` are
  shared. No builder produces the agent actor or the proposed and agent-applied marks.
- `ConsoleEntry` and `log::Message` carry no actor. The activity strip's Stop is wired to playback.
- `Source/` has no thread, socket or child process. `asset::JsonTokens` parses JSON and
  `JsonWriter` emits it.
- The windowed renderer has no CPU readback; only the headless modes write images.
- `Tools/check_project_policy.py` rejects the word "agent" under `Source/` and `Tests/` outside a
  hardcoded list of sites.

## Decisions taken with the owner

| Topic | Decision |
|---|---|
| Scope | UX5.1, UX5.2 and UX5.3 deliver together before N1, in one plan and one squash merge |
| Bridge jobs | Inspect the editor, propose scene edits, change rendering settings and produce evidence are all in the exit gate |
| Client | A Unix domain socket carrying newline-delimited JSON, with a checked-in Python client. No MCP layer |
| Image evidence | A bridge command runs the App's headless screenshot or sequence mode as a child process. The live viewport is not read back |
| Threading | A listener thread owns the socket and exchanges lines with the main thread through a mailbox. Commands execute on the main thread |
| Approval | Three tiers (read-only, propose, apply with approval), one approval per command or per submitted plan |
| Enabling | The bridge is off unless the operator turns it on for the run. Nothing about it persists |

## Principles

1. **The operator approves; the client never does.** The tier ceiling, Accept, Reject, Approve and
   Deny exist only in the editor. No command, flag or environment variable approves on the
   operator's behalf.
2. **One command path.** A bridge command runs the function the editor's own control runs, with
   the same preconditions and the same disabled reason as its error message.
3. **Attribution is data.** Every action carries an actor, a client name and a time, and every
   evidence file is named with its hash.
4. **Selection is the operator's.** A session never changes selection, the editor camera or
   playback.
5. **The renderer is untouched.** UX5 changes no pass, shader, capture format or manifest.

## Units

| Unit | Home | Owns |
|---|---|---|
| `SceneDocumentDiff` | `Engine/Asset/Document` | `diffSceneDocuments(a, b)`: one row per changed property, total over the canonical form |
| `SessionProposal`, `ProposalQueue` | `App/Model/Session` | Proposal identity, source (file or bridge), lifecycle state, sidecar reader, rejected hashes |
| `SessionProtocol` | `App/Model/Session` | Request and response framing, decoding through `JsonTokens`, encoding through `JsonWriter` |
| `SessionCommands` | `App/Model/Session` | The command table: name, tier, argument schema, precondition |
| `SessionApprovals` | `App/Model/Session` | Pending approvals, plans and their step cursor |
| `SessionLog` | `App/Model/Session` | Action records and the exported session record |
| `SessionMailbox` | `App/Model/Session` | Mutex-guarded inbox and outbox; the only state both threads touch |
| `RenderSettingCommands` | `App/Model/Rendering/Settings` | `applyRenderSetting` with cascades and reasons; `settingsToArguments` for a headless run |
| `SessionListener` | `App/Model/Session` | The socket, its thread, peer check and line limits |
| `DocumentWatch` | `App/Model/Session` | Stable-poll decisions and verified save ownership |
| `ChildRun` | `App/Shell` | Spawning and reaping a headless run |
| `EditorSession` | `App/Shell` | Draining the mailbox at the frame's safe point and executing commands |
| `SessionPanel` | `App/Panels/Session` | Connection, tier ceiling, cards, log and Export |
| `EditorStyle` growth | `App/Panels/Shared` | Proposal card, attention ring, proposed-value row and actor chip, which the Gallery then reuses |
| `lmx_session.py` | `Tools/Session` | The client: one subcommand per bridge command, `sidecar`, and `--selftest` |

Commands execute after native menu commands and before document work, the point where the frame
loop already runs scene switches outside both the ImGui frame and the device frame. The listener
thread frames lines and nothing else: it reads no editor state and calls no editor function.

**Naming.** The subsystem is "Session" in code, comments and documents. The policy checker's list
grows to allow the identifiers UX4 introduced (`Actor::Agent`, `AgentApplied`, `AccentAgent*`,
`ActorAgent`) under `Source/App` and `Tests/App`, and the visible label "Agent".
Comments say "session client".

## Proposals

A proposal has an identifier, a source, an actor name, a summary, evidence paths, change rows and
a state: idle, working, awaiting, proposed, applied, error or stale, the seven states the Gallery
already draws.

**Diff.** `diffSceneDocuments` compares two CPU documents. A row names its owner (document, node,
camera, light, look or animation), the property, and the old and new values as the writer prints
them. A property is the first key below its owner in the canonical JSON; a vector or a quaternion
is one property. Animation sample data is one row per channel. Nodes match by index, so an inserted
node shows as many rows; the result is still complete. The diff is empty exactly when
`documentDirty` is false.

**Detection.** The shell polls the open pair's size, modification time, inode and device twice a
second, except during Measure. After a change, it waits for one further identical poll, and up to
5 s for a writer's `.lmx-save-*.tmp` directory to clear, then hashes the pair. A hash other than the
loaded one produces a file proposal, unless no canonical value changed: then the hash is adopted.
The editor's own Save suppresses a proposal only for its verified writer bytes and coherent stamps.

**Sidecar.** `<name>.scene.proposal.json` beside `<name>.scene.gltf` holds `schema` (1), `actor`,
`summary`, `evidence` (paths relative to the sidecar) and `documentSha256` (the pair's hash, as
`sceneDocumentHash` computes it). A sidecar whose hash does not match is given four seconds to
catch up. After that, or with no sidecar, the proposal is attributed to "Unknown external change"
with the system mark.

**Review.** The Session panel draws the card: actor, summary, change count, evidence with Copy
path and Reveal, missing evidence flagged, then Show, Accept and Reject.

- Show lists the rows as old and new values and rings the changed nodes in Hierarchy.
- Accept on a file proposal reloads through the Revert path. It needs Stopped and no measurement,
  and asks Discard or Cancel when the operator has unsaved edits.
- Accept on a bridge proposal re-runs the preview, stales the card when its rows differ, and
  otherwise applies its edits to the live scene, which becomes dirty. The edited fields carry the
  agent-applied mark until Save or Revert.
- Reject leaves the scene untouched. For a file proposal the editor remembers the rejected hash so
  the card does not return; a later Save overwrites the file.
- While a file proposal is pending, Save and Save As are disabled with the reason "Review the
  pending proposal first". Save also hashes the pair on disk and refuses an unreviewed change.
- A file the reader rejects becomes a card in the error state showing the reader's message, with
  Reject only.
- A file proposal goes stale when its file changes again or the scene is replaced; a bridge one
  when the scene is replaced, its rows change or the ceiling drops to read-only. Stale is final.

**Surfacing.** The activity strip shows the agent mark with the state and opens the panel on click;
the Hierarchy root carries the proposed mark. The panel never opens itself. It is a persisted
docked panel, closed by default, tabbed with Console. Workspace schema 6 adds its visibility;
schema 5 restores unchanged with the panel hidden.

## Bridge

**Transport.** `--session` (windowed only; rejected with screenshot, sequence and measure) or the
panel's Listen toggle opens `$TMPDIR/luminex-session-<pid>.sock` with mode 0600 and logs the path.
One client connects at a time, and its user must be the editor's. A line is at most 1 MiB and the
inbox holds 64 requests; beyond either the client gets an error. The socket file is removed on
close; Listen replaces a stale one the same user owns (`/tmp` when TMPDIR is unset). The client
finds it by `--socket`, `LMX_SESSION_SOCKET`, or the newest live match.

**Protocol.** A request is `{"id", "command", "args"}`. Each request gets exactly one response,
`{"id", "ok": true, "result"}` or `{"id", "ok": false, "error": {"code", "message"}}`, sent when
the command finishes; a command waiting for approval or for a job answers later. The first request
is `hello` with a client name and `protocol` 1. Error codes are `protocol`, `tier`, `denied`,
`unavailable`, `invalid`, `busy`, `failed` and `cancelled`. Unknown or repeated argument members,
objects over 64 members and nesting over 32 answer `invalid`.

| Tier | Commands |
|---|---|
| Read-only | `query.status`, `query.hierarchy`, `query.selection`, `query.camera`, `query.settings`, `query.readings`, `query.performance`, `query.graph`, `query.console`, `query.proposals`, `query.log` |
| Propose | `propose.edits`, `propose.withdraw` |
| Apply | `settings.set`, `debugview.set`, `scene.open`, `measure.run`, `capture.gpu`, `capture.screenshot`, `capture.sequence`, `graph.dump`, `plan.submit` |

**Tiers.** The operator sets the ceiling in the panel. It starts at read-only for every
connection. A command above it is refused with `tier` and logged; nothing is queued.

**Edits.** `propose.edits` takes a summary, evidence and edits to what the Inspector edits and the
exporter saves: node enabled, object transform, local-light fields, the look (exposure, bloom,
shadows) and the scene camera. Subjects are named by the identifiers `query.hierarchy` returns.
Look values use the reader's ranges. Pending proposals are never evicted; the 65th answers `busy`.

**Approval.** Every apply command shows an approval card with the command, its arguments and its
output location, and Approve and Deny. `plan.submit` carries a summary and an ordered list of apply
commands; one approval runs them in order inside the editor and stops at the first failure. A plan
cannot grow after approval. Disconnecting, lowering the ceiling or replacing the scene cancels
pending approvals; a running job finishes. At most 8 await or run; a new card ignores clicks for 0.5 s.

**Settings.** `settings.set` uses the CLI's flag names (`temporal`, `render-scale`, `visibility`,
`classify`, `classify-check`, `occlusion`, `occlusion-check`, `submission`, `local-lights`,
`light-check`, `local-light-rig`). `applyRenderSetting` owns the cascades and the reasons, and the
Rendering panel calls it too. A changed setting carries the agent-applied mark until the operator
edits or resets it. `debugview.set` goes through `selectDebugView`.

**Evidence.** Output goes only under `session/<UTC start>-<pid>/` in the build directory; a client
supplies a file name, never a path. `measure.run` and `capture.gpu` use the interactive paths.
`graph.dump` exports the displayed frame. `capture.screenshot` and `capture.sequence` spawn the
App's headless mode with the loaded document, the arguments `settingsToArguments` derives from the
current settings and the run's lab overrides; they refuse a dirty document. One job runs at a
time. It shows on the activity strip with the agent mark, and the operator's Stop cancels it.

## Session log

- `ConsoleEntry` gains an actor. Engine and editor log rows are system; session requests and
  results are agent; Accept, Reject, Approve, Deny and tier changes are operator. The Console gains
  the actor chip and an actor filter beside severity.
- `SessionLog` records each action: sequence, UTC time, actor, client name, command, arguments,
  tier, approval (time and plan), result, and evidence paths with SHA-256; command, arguments
  and history are bounded at 128 bytes, 4,096 bytes and 10,000 actions, with a dropped count.
- Export writes `session.json` into the session directory: document path and hash, client,
  protocol, the actions, and a `console` array equal to the Console's Copy visible text under the
  agent and operator filter.

## UX5.1 — File-based proposals

**Deliver:** the document diff; the watch and the sidecar; the proposal queue and lifecycle; the
shared proposal card, attention ring, proposed-value row and actor chip; the Session panel with
workspace schema 6; the Save guard; the session log model and Console actor; the client's `sidecar`
subcommand.

**Exit gate:** for a corpus of changed documents the diff rows equal an independent comparison of
the writer's canonical output, and the diff is empty exactly when the documents are not dirty;
Accept yields the scene that opening the file yields; Reject leaves the exported scene unchanged;
a pending proposal blocks Save with its reason; schema 5 workspaces restore unchanged.

## UX5.2 — Command bridge

**Deliver:** `applyRenderSetting` behind the Rendering panel; the protocol, command table, tiers,
approvals and plans; the mailbox and listener; bridge proposals and agent-applied marks; session
jobs on the activity strip with Stop; the headless child run; `lmx_session.py`.

**Exit gate:** every command is refused below its tier and the refusal is logged; every setting
combination the CLI parser rejects is rejected by `applyRenderSetting`, and the panel's behavior
is unchanged case by case; arguments from `settingsToArguments` parse back to the same settings; a
plan runs only its approved steps; a child screenshot is byte-identical to the same CLI run; the
listener leaves no socket file and no thread behind.

**Owner-authorized exception (2026-10-02):** preserve the existing panel cascades. The original
CLI-rejection requirement above is not passed where a cascade resolves the requested conflict:
classify GPU from CPU/Direct selects Indirect, submission Direct from GPU/Indirect selects CPU,
light-check on selects Clustered, and CPU classification or visibility off clears occlusion.
Unavailable operations reject without mutation; effective
post-cascade states satisfy applicable CLI constraints. The owner authorized this recommendation
and continuation; this is a scoped gate exception, not milestone acceptance.

## UX5.3 — Session log and evidence export

**Deliver:** the Console actor filter; Export; evidence hashes; the architecture page, the guide,
`AGENTS.md`, an ADR for the session protocol and trust contract; whole-application acceptance.

**Exit gate:** the exported `console` array equals Copy visible under the same filter; every logged
evidence path exists with its recorded hash; scene-only screenshots for the standing matrix are
byte-identical to the parent; all earlier slice gates hold together; gestures not exercised are
recorded as unverified.

## Verification

- AppModel and Asset unit tests cover the diff, sidecar, queue, protocol, command table, tiers,
  approvals, plans, log, settings commands and the schema 6 migration.
- A listener test connects a real client socket in a temporary directory.
- `lmx_session.py --selftest` runs against a fake server; an integration script drives a running
  editor at the read-only ceiling and checks every refusal.
- Approval, Accept and Reject need an operator's click. They are exercised by hand or through the
  input helper retained with the UX4 evidence, and anything not exercised is recorded as such.
- The image matrix is Sponza, MaterialLab and TemporalLab by five cases at `--temporal off`, with
  the App and shader binaries frozen by hash.

## Boundaries and deferrals

No agent inside the process; no automatic approval; no Undo/Redo; one client; no MCP or network
transport; no live viewport readback; no create, delete, duplicate or reparent in a bridge
proposal; no change to selection, camera or playback by a session; no renderer, shader, capture
format or manifest change; no Windows support; nothing UX1–UX4 already defer.

## Risks and open points

- **First thread in `Source/`.** The listener touches only the socket and the mailbox; a test
  starts and stops it repeatedly. A stalled frame loop delays responses and cannot corrupt state.
- **Two writers, one file.** A client may write the glTF and its buffer in two steps. The stable
  poll, the pair hash and the sidecar hash together decide when a change is complete; a document
  caught half-written reads as an error card and is replaced when the file settles.
- **Settings refactor.** Moving the panel's cascades behind one function can change behavior.
  Tests record the current behavior case by case before the move.
- **A second device.** A headless child shares the GPU with the editor. Its images are compared
  with the same CLI run; measurement never runs while a child is alive.
- **Output confinement.** Evidence names are checked against path separators and `..`.
- **Gestures.** Operator approval cannot be automated without contradicting the first principle,
  so part of the gate depends on manual or input-helper runs.

## Implementation and review limits

All three slices are implemented; [validation](ux5-validation.md) retains every failed attempt,
scoped exception and unverified gesture. The frozen final App of the original chain passed 215 GPU
cases / 1,167,008 assertions and the pinned validator 93 documents. An independent
[review](ux5-review-validation.md) on 2026-10-02 found defects that six fix commits corrected.
Afterwards the unit group passes 1,096 cases and the Python suite 331 tests. Fixes under
`Source/App/Shell` and the Session panel have model-level unit tests only, and no native gesture
was repeated. The owner has not accepted integration; the executor plan remains In progress.

Failed and incomplete gates, as measured, with no tolerance or default changed:

- Gallery exact comparison fails in both themes: Light 0/16 and Dark 0/16.
- Four of the five original CLI rejection pairs fail under the scoped cascade exception.
- The initial final-App standing matrix fails 14/15; three additional eight-round attempts each
  pass 15/15.
- Native approved GPU capture certification failed in all four attempts (0/4 passed): the traces
  contain internal symlinks that the evidence checks refuse. Checks and RojoRHI remain unchanged.
- The literal five-case Off gate lacks case definitions and is incomplete; the separately defined
  supplemental Off matrix passes 120/120 strict same-round pairs.
- Console Copy visible byte equality and the remaining gestures stay unverified.
