# App Session

**Status**: Implemented

Session connects external file changes and a local client to operator review. The editor hosts no
client runtime. [ADR 0030](../decisions/0030-session-protocol-and-trust.md) remains Proposed;
[UX5](../milestones/ux/ux5.md) retains its scoped failed gate and acceptance limits. The
[guide](../guides/agent-session.md) owns command syntax and recovery.

## Units and ownership

| Unit / location | Responsibility |
|---|---|
| Asset `Document/SceneDocumentDiff` | Canonical CPU document property rows, including animation buffer changes |
| AppModel `Session/SessionProposal`, `DocumentWatch` | Sidecar schema, bounded lifecycle queue, rejected hashes, stable-poll decisions |
| AppModel `Session/SessionProtocol`, `SessionCommands` | Protocol 1 envelopes, error encoding, 22 command names and tiers |
| AppModel `Session/SessionQueries`, `SessionEdits` | Pure query encoders, subject identity, validated persistent edit previews/application |
| AppModel `Session/SessionApply`, `SessionApprovals` | Apply argument validation, immutable plans, approval/step state |
| AppModel `Session/SessionLog`, `SessionAttribution` | Ordered attributed records, evidence certification, export and field marks |
| AppModel `Session/SessionMailbox`, `SessionListener` | Bounded thread exchange and Unix socket lifetime |
| AppModel `Rendering/Settings/RenderSettingCommands` | Shared panel/session setting cascades and effective headless arguments |
| App shell `EditorSession`, `EditorDocuments` | Safe-point dispatch, polling, review, shared document replacement/save guards |
| App shell `ChildRun` | `posix_spawn`, redirected child log, polling, termination and reaping |
| App `Panels/Session`, `Panels/Shared/EditorStyle` | Listen/ceiling/review/log/Export, shared proposal/attention/actor widgets |
| App `Panels/Console` | Severity/search/actor filtering and held view |
| `Tools/Session/lmx_session.py` | Sidecar authoring, socket discovery, hello, one command per connection |

These folders add no module units. Asset remains CPU-only. AppModel has no ImGui/SDL/Metal types;
its listener uses POSIX sockets, a wake pipe and one thread. Shell/panel headers stay private in the
[module contract](../conventions/modules.md); Tests links AppModel and tests its public models.
Renderer passes, shaders, capture formats and existing manifests keep their contracts.

## Threads and frame boundary

The listener `poll`s the listening socket, one client and a wake pipe. It checks the peer effective
UID, frames lines and moves them through the mailbox. It never reads editor state, calls editor
functions or logs through spdlog. The mutex protects only inbox/outbox exchange; connection IDs
prevent queued lines from a departed client reaching the next connection.

The windowed loop consumes native menu commands, calls `pumpSession`, then `pumpDocuments` before
drawable acquisition. Neither an ImGui frame nor a device frame is active. Session drains requests,
executes approved steps and polls file/job state there. Panel actions become intents for this pump;
queries serialize main-thread state. A missing drawable therefore cannot skip session work.

Each connection resets hello/client/tier to ReadOnly. Hello records the last client for later Export.
Disconnect cancels awaiting approvals while a running step finishes. Listen off and shell shutdown
cancel work, release capture/measurement reservations, kill/reap children, wake/join the listener
and remove the socket. The panel's visibility persists; listening, ceiling and connection do not.

## Document proposals and state

Polling runs at most twice a second, waits for two equal changed stamps and excludes writer
`.lmx-save-*.tmp` directories. It hashes the glTF/buffer pair and waits up to four seconds for a
matching sidecar. A matching sidecar attributes Agent; otherwise the card says Unknown external
change with System attribution. Reader errors produce an Error card; later changes stale it.
The diff compares canonical writer JSON and animation bytes. Resolved proposals are retained in
a queue capped at 64, with rejected file hashes remembered until document replacement.

Pending file proposals block Save/Save As. File Accept rechecks the hash and uses Revert's
Stopped/no-measurement and Discard/Cancel workflow. Bridge Accept validates the whole batch before
applying persistent edits and marking fields; Save/Revert clears document attribution. Replacement
stales both sources and resets the watch. The editor adopts its own writes before watching again.

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
Measurement and GPU capture use interactive paths. Graph dump reads the displayed publication,
including Freeze. Child screenshots/sequences use the same executable and working directory,
a clean loaded pair, effective settings and lab overrides. One job runs at a time; measurement
cannot overlap a child. The activity strip exposes progress and routes Stop to active session work.

Outputs live under build-local `session/<UTC yyyyMMdd-HHmmss>-<pid>/`. Names and directory/target
checks reject traversal, existing outputs and symlinks, with `session.json` reserved for Export.
These are confinement checks, not a same-UID sandbox. Failed spawn logs remain available. Successful
regular outputs use SHA-256; GPU directory payloads and schema sidecar are each hashed; sequence
runs attach `manifest.json` and the child log. Certification failures are explicit failed outcomes.

`SessionLog` records Agent requests/results and Operator tier/review/export actions and mirrors
those records into Console. Export writes schema 1/protocol 1, loaded path/hash, last client, ordered
actions and a Console array. It keeps the held snapshot, search and severity but selects Operator
and Agent; one array item is one formatted entry, preserving embedded newlines. `export.request`
is recorded before serialization. The actual-write `export.result` is recorded afterward and is
available in the next Export. Listen off preserves the last client for this record.
