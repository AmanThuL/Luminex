# App Session

**Status**: Implemented

Session connects external file changes and a local client to operator review. The editor hosts no
client runtime. [ADR 0030](../decisions/0030-session-protocol-and-trust.md) remains Proposed;
[UX5](../milestones/ux/ux5.md) retains its scoped failed gate and acceptance limits; the
[review record](../milestones/ux/ux5-review-validation.md) lists the fixes no native run has
exercised. The [guide](../guides/agent-session.md) owns command syntax and recovery.

## Units and ownership

| Unit / location | Responsibility |
|---|---|
| Asset `Document/SceneDocumentDiff` | Canonical CPU document property rows, including animation buffer changes |
| AppModel `Session/SessionProposal`, `DocumentWatch` | Sidecar schema, bounded lifecycle queue, rejected hashes, `DocumentProbe` stamps, stable-poll and Save-overwrite decisions |
| AppModel `Session/SessionProtocol`, `SessionCommands` | Protocol 1 envelopes, error encoding, argument-member validation, 22 command names and tiers, child environment filter |
| AppModel `Session/SessionQueries`, `SessionEdits` | Pure query encoders, subject identity, validated persistent edit previews/application, the `SessionAttribution` field-mark class |
| AppModel `Session/SessionApply`, `SessionApprovals` | Apply argument validation, immutable plans, approval/step state, the approval click guard |
| AppModel `Session/SessionLog` | Bounded ordered attributed records, evidence certification and export |
| AppModel `Session/SessionMailbox`, `SessionListener` | Bounded thread exchange and Unix socket lifetime |
| AppModel `Rendering/Settings/RenderSettingCommands` | Shared panel/session setting cascades and effective headless arguments |
| App shell `EditorSession`, `EditorDocuments` | Safe-point dispatch, polling, review, shared document replacement/save guards |
| App shell `ChildRun` | `posix_spawn` with close-on-exec default descriptors and no `LMX_*` variable, redirected child log, polling, termination and reaping |
| App `Panels/Session`, `Panels/Shared/EditorStyle` | Listen/ceiling/review/log/Export, shared proposal/attention/actor widgets |
| App `Panels/Console` | Severity/search/actor filtering and held view |
| `Tools/Session/lmx_session.py` | Sidecar authoring, socket discovery, hello, one command per connection |

These folders add no module units. Asset remains CPU-only. AppModel has no ImGui/SDL/Metal types;
its listener uses POSIX sockets, a wake pipe and one thread. Shell/panel headers stay private in the
[module contract](../conventions/modules.md); Tests links AppModel and tests its public models.
Renderer passes, shaders, capture formats and existing manifests keep their contracts.

## Threads and frame boundary

The listener `poll`s the listening socket, one client and a wake pipe. `start` refuses an existing
path, except that it replaces a socket the same user owns whose connection is refused. An accepted
client is marked close-on-exec first. The listener checks the peer effective
UID, frames lines and moves them through the mailbox. It never reads editor state, calls editor
functions or logs through spdlog. The mutex protects only inbox/outbox exchange; connection IDs
prevent queued lines from a departed client reaching the next connection.

The windowed loop consumes native menu commands, calls `pumpSession`, then `pumpDocuments` before
drawable acquisition. Neither an ImGui frame nor a device frame is active. Session drains requests,
executes approved steps and polls file/job state there. Panel actions become intents for this pump;
queries serialize main-thread state. Invalid result JSON/UTF-8 and invalid error UTF-8 produce safe
protocol failures rather than assertions. A missing drawable therefore cannot skip session work.
The complete scene tree is built at most once per drain, and only when a command reads it.

After the tier check, `validateSessionArguments` checks each argument object before any member is
read: unknown members, members repeated at any depth, objects over 64 members and nesting over 32
answer `invalid`. `settings.set` names its single member by the setting; `plan.submit` applies the
rule to each step and its `args`. Non-finite floats in query replies and change rows are written as
the strings `"infinite"`, `"-infinite"` or `"nan"`.

Each connection resets hello/client/tier to ReadOnly. Hello records the last client for later Export.
Disconnect cancels awaiting approvals while a running step finishes. Lowering the ceiling cancels
awaiting approvals with an `approval.cancel` row (`cancelled: ceiling lowered`), and a ceiling of
ReadOnly also stales pending bridge proposals. Open, the catalog, Revert and `scene.open` cancel
awaiting approvals (`cancelled: scene replaced`); an approved plan, including the one running
`scene.open`, continues. At most 8 approvals await or run and the ninth submit answers `busy`. The
panel ignores Approve and Deny for 0.5 s after the shown approval changes. Listen off and shell shutdown
cancel work, release capture/measurement reservations, kill/reap children, wake/join the listener
and remove the socket. The panel's visibility persists; listening, ceiling and connection do not.

## Document proposals and state

Polling runs at most twice a second and is skipped during a measurement. `DocumentProbe` stamps
size, modification time, inode and device for the glTF and its buffer; it re-reads the glTF for its
buffer URI and re-scans the directory only when their own stamps change, so an idle poll is three
`stat` calls. The watch waits for two equal changed stamps. A writer `.lmx-save-*.tmp` directory
defers it for ten polls (5 s); a leftover one then logs one Console warning and is ignored. The
shell hashes the glTF/buffer pair. A pair whose canonical document equals the loaded one adopts the
new hash with one Console info line and no card. Otherwise the watch waits up to four seconds for a
matching sidecar. Invalid UTF-8 sidecars are rejected before metadata retention. A matching sidecar
attributes Agent; otherwise the card says Unknown external change with System attribution. Reader
errors produce an Error card; later changes stale it. The diff compares canonical writer JSON and
animation bytes. Resolved proposals beyond 64 retained are evicted oldest first; a pending proposal
is never evicted, and `propose.edits` answers `busy` at 64 pending bridge proposals. Rejected file
hashes are remembered until document replacement.

Pending file proposals block Save/Save As. Save, not Save As, also hashes the pair on disk before
writing: `saveOverwriteReason` refuses unless that hash is the loaded hash or one the operator
rejected, or, for an unhashable pair, unless its stamp is still the loaded baseline. File Accept
rechecks the hash and uses Revert's Stopped/no-measurement and Discard/Cancel workflow. Bridge
edits are range-checked with the reader's `sceneLookRangeError`. Bridge Accept re-runs the preview
(`reviewedEditsCurrent`) and stales the card when its rows differ from the ones shown, then
validates the whole batch before applying persistent edits and marking fields; Save/Revert clears
document attribution. Bridge staleness does not follow the file hash, so Save and Save As keep
pending bridge cards. `propose.withdraw` works only from the submitting connection. Replacement
stales both sources and resets the watch. Save adoption first verifies the observed pair hash
against canonical writer bytes. Watch suppression requires that completed-write receipt, matching
path/hash and coherent stamps around hashing on both successful and failed save outcomes. Pre-write
failures preserve pending revisions; uncertified successful adoption invalidates the watch baseline.

`scene.open` shares `replaceSessionDocument` with the operator workflow, but requires a clean,
stopped, idle document, no measurement or pending file proposal, and selection None, Camera or
Environment. It preserves that semantic selection and the current stopped editor view. Failure
keeps the old scene. Generated/unsavable subjects and animated object transforms are refused in
bridge proposals. Saved scene camera edits never move the editor camera.

## Settings, jobs and records

`applyRenderSetting` is the Rendering panel and session's shared command layer. Existing cascades
remain; the original CLI-rejection gate fails for resolved conflicts under the
[owner-authorized scoped exception](../milestones/ux/ux5.md#ux52--command-bridge), which is not
milestone acceptance. Unavailable operations reject without mutation. `settingsToArguments`
serializes effective state: temporal off emits scale 1 rather than the retained dormant scale;
startup lab overrides and the current light rig are included.

Apply work waits for operator approval; plans contain 1–32 fixed Apply steps and stop on failure.
Measurement and GPU capture use interactive paths. A session measurement carries its own warmup,
frames and output and leaves the Performance panel's fields and mouse look untouched. Graph dump
reads the displayed publication, including Freeze. Child screenshots/sequences use the same
executable and working directory, a clean loaded pair, effective settings and lab overrides, with
only the child log and `/dev/null` as descriptors. One job runs at a time; measurement
cannot overlap a child. The activity strip exposes progress and routes Stop to active session work.

Outputs live under build-local `session/<UTC yyyyMMdd-HHmmss>-<pid>/`. Names and directory/target
checks reject traversal, existing outputs and symlinks, with `session.json` reserved for Export.
These are confinement checks, not a same-UID sandbox. Failed spawn logs remain available. Successful
regular outputs use SHA-256; GPU directory payloads and schema sidecar are each hashed; sequence
runs attach `manifest.json` and the child log. Certification failures are explicit failed outcomes.

`SessionLog` records Agent requests/results and Operator tier/review/export actions and mirrors
those records into Console. It keeps each command to 128 bytes and its arguments to 4,096 bytes
with a `…[truncated N bytes]` suffix, retains the newest 10,000 actions and counts the dropped
ones. `query.log` takes `afterSequence` and returns `nextSequence`, `dropped`, `omitted` and the
newest `actions` that fit just under 1 MiB. Actor values are `"Operator"`, `"System"` and `"Agent"`.
Export writes schema 1/protocol 1, loaded path/hash, last client, `dropped`, ordered
actions and a Console array. It keeps the held snapshot, search and severity but selects Operator
and Agent; one array item is one formatted entry, preserving embedded newlines. `export.request`
is recorded before serialization. The actual-write `export.result` is recorded afterward and is
available in the next Export. Listen off preserves the last client for this record.
