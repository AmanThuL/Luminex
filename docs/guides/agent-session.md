# Agent Session

**Status**: Implemented

Use Window > Session to review external scene files, inspect the editor through a local client,
and approve evidence jobs. The panel stays closed until opened and never opens itself for a
proposal. Session visibility persists in workspace schema 6; Listen, connection and tier do not.
[App Session](../architecture/app-session.md) explains ownership;
[ADR 0030](../decisions/0030-session-protocol-and-trust.md) remains Proposed. The
[validation record](../milestones/ux/ux5-validation.md) retains failed gates and unverified gestures;
the [review record](../milestones/ux/ux5-review-validation.md) lists what no native run has exercised.

## Enable and connect

Run the windowed editor with `--session`, or turn on Listen in the Session panel:

```sh
xmake run -P . App --session --scene temporal-lab
python3 Tools/Session/lmx_session.py --name RendererReview query status
```

`--session` is rejected with screenshot, capture-sequence or measure modes. Listen logs and displays
`$TMPDIR/luminex-session-<pid>.sock`, or `/tmp/luminex-session-<pid>.sock` when TMPDIR is unset;
the client looks in the same directory. The socket has mode 0600 and accepts one same-user client.
Listen refuses an existing path, except that it replaces a stale socket owned by the same user that
refuses connections, the file a crashed editor leaves. Copy path in the panel gives an explicit
socket location. Client precedence is `--socket`, then `LMX_SESSION_SOCKET`, then newest live-PID
socket discovery; discovery tries candidates until one connects and skips a busy editor when
another candidate exists. An explicit socket never falls back to discovery. Global client options
precede the subcommand; `--timeout` defaults to 600 seconds for the whole hello/wait/command
exchange and must be a positive finite number, and `--name` identifies the client in cards and logs.

Every invocation opens a fresh connection and starts at **ReadOnly**. For Propose or Apply work:

```sh
python3 Tools/Session/lmx_session.py --name RendererReview --wait-tier settings set temporal off
```

`--wait-tier` polls read-only `query.status` on the **same connection** until the operator changes
that connection's ceiling in the panel. It never raises a tier or approves anything. Without it,
an above-ceiling request immediately returns `tier` and is logged. Propose requires the operator
ceiling Propose or Apply; Apply also requires a separate Approve click. No automatic approval,
Accept, ceiling flag or environment bypass exists. The client exits 0 for success, 2 for an error
response or invalid input and 3 for transport failure, printing JSON on stdout. A `busy` answer
from an editor that already serves a client is an error response (exit 2); an invalid `--timeout`
exits 2 before connecting. Sidecar failures exit 1. A timeout closes the connection; inspect logs
before retrying a possibly completed job.

## Wire protocol and commands

A custom client sends UTF-8 JSON followed by a newline. Its first request and response are:

```json
{"id":1,"command":"hello","args":{"name":"RendererReview","protocol":1}}
{"id":1,"ok":true,"result":{"protocol":1}}
```

Then send `{"id":2,"command":"query.status","args":{}}`. IDs are nonnegative integers;
arguments are objects (omission means `{}`). Responses are either `{"id":2,"ok":true,"result":...}`
or `{"id":2,"ok":false,"error":{"code":"tier","message":"..."}}`. Approval/jobs answer when
finished, not when queued. Malformed envelopes use response ID 0. Hello is separate from the 22
commands below; the Python client sends it automatically.

Arguments are checked before any member is read. An unknown member, a member repeated in any
object at any depth, more than 64 members in one object and nesting deeper than 32 levels each
answer `invalid`; an above-ceiling command answers `tier` first. Allowed members: `hello` takes
`name`, `protocol`; `query.console` and `query.log` take `afterSequence`; other queries and
`capture.gpu` take none; `propose.edits` takes `summary`, `evidence`, `edits`; `propose.withdraw`
takes `proposal`; `settings.set` takes exactly one setting; `debugview.set` takes `topic`, `value`;
`scene.open` takes `scene`; `measure.run` takes `name`, `warmup`, `frames`; `graph.dump` takes
`name`; `capture.screenshot` takes `name`, `frames`; `capture.sequence` takes `name`, `frames`,
`warmup`; `plan.submit` takes `summary`, `steps`, and each step `command`, `args`.

ReadOnly commands need no operator approval. Each example below is a complete client invocation:

| Command | Example | Result |
|---|---|---|
| `query.status` | See command block below | Loaded path/hash/dirty, playback, measurement, tier, proposals, job |
| `query.hierarchy` | See command block below | Subject IDs and document/source rows, depth, enabled/effective/generated |
| `query.selection` | See command block below | Operator's current subject |
| `query.camera` | See command block below | Editor view; querying it does not change it |
| `query.settings` | See command block below | Current requested setting values |
| `query.readings` | See command block below | Visibility and lighting diagnostic snapshots |
| `query.performance` | See command block below | Current Performance snapshot |
| `query.graph` | See command block below | Displayed graph text, including Freeze, or unavailable |
| `query.console` | See command block below | Held Console entries after a sequence cursor |
| `query.proposals` | See command block below | Proposal lifecycle and rows |
| `query.log` | See command block below | Retained session actions after a sequence cursor |

```sh
python3 Tools/Session/lmx_session.py query status
python3 Tools/Session/lmx_session.py query hierarchy
python3 Tools/Session/lmx_session.py query selection
python3 Tools/Session/lmx_session.py query camera
python3 Tools/Session/lmx_session.py query settings
python3 Tools/Session/lmx_session.py query readings
python3 Tools/Session/lmx_session.py query performance
python3 Tools/Session/lmx_session.py query graph
python3 Tools/Session/lmx_session.py query console --after-sequence 0
python3 Tools/Session/lmx_session.py query proposals
python3 Tools/Session/lmx_session.py query log --after-sequence 0
```

`query.camera` reports `farZ` as a JSON number for a finite far plane, or the string `"infinite"`
when the authored perspective camera omits `zfar`. Querying either lens leaves the view unchanged.
Any non-finite float in `query.camera`, `query.performance` or a proposal change row is one of the
strings `"infinite"`, `"-infinite"` or `"nan"`. Actor values are `"Operator"`, `"System"` and
`"Agent"`.

`query.log` takes an optional `afterSequence` and returns `nextSequence`, `dropped`, `omitted` and
`actions`. The reply is capped just under 1 MiB and holds the newest rows that fit; `omitted`
counts older matching rows left out, which no later query returns. `dropped` counts actions the
editor no longer retains.

Propose commands create or withdraw review cards. Replace example IDs with current hierarchy or
proposal IDs; the sample environment edit applies to a saved look:

```sh
python3 Tools/Session/lmx_session.py --wait-tier propose edits --summary 'Disable bloom' --edit environment bloom '{"enabled":false}'
python3 Tools/Session/lmx_session.py --wait-tier propose withdraw 1
```

`propose.edits` accepts repeatable `--edit SUBJECT FIELD JSON` and `--evidence PATH`. Show displays
before/after rows. Accept needs Stopped and no measurement, re-runs the preview and stales the card
when its rows differ from the ones shown; otherwise it applies the validated batch and marks the
document dirty. Reject leaves the scene untouched; both ignore clicks for 0.5 s after the card list
changes. Withdraw stales a pending proposal from its submitting connection or, once that has closed,
from a client of the same name. While playing or paused, an edit to an orbiting light's `position`
answers `unavailable`. A pending proposal is never evicted: `propose.edits` answers `busy` while 64
bridge proposals await review. Save and Save As keep pending bridge cards; scene replacement, an
accepted file proposal and a ceiling lowered to ReadOnly stale them. No client command accepts or
rejects.

Apply commands each show their arguments/output in an approval card. The operator chooses Approve or
Deny; execution rechecks availability at the safe point. Approve and Deny are ignored for 0.5 s
after the shown approval changes. At most 8 approvals may await or run; the ninth answers `busy`. A
lowered ceiling cancels awaiting approvals: the client gets `cancelled` with the message `cancelled:
ceiling lowered`, also the `approval.cancel` log outcome. Open, the catalog, Revert, file Accept and
`scene.open` cancel them with `cancelled: scene replaced`; the plan running `scene.open` continues.
A running plan refuses Open, the catalog, Revert and file Accept until toolbar Stop ends it.

```sh
python3 Tools/Session/lmx_session.py --wait-tier settings set temporal off
python3 Tools/Session/lmx_session.py --wait-tier debugview set final
python3 Tools/Session/lmx_session.py --wait-tier scene open temporal-lab
python3 Tools/Session/lmx_session.py --wait-tier measure run 32 256 costs.json
python3 Tools/Session/lmx_session.py --wait-tier capture gpu
python3 Tools/Session/lmx_session.py --wait-tier capture screenshot still 32
python3 Tools/Session/lmx_session.py --wait-tier capture sequence sequence 32 16
python3 Tools/Session/lmx_session.py --wait-tier graph dump graph.txt
python3 Tools/Session/lmx_session.py --wait-tier plan submit plan.json
```

`settings.set` takes exactly one name/string value: `temporal` off/raw/taa/metalfx, `render-scale`
0.5–1, `visibility` cull/off, `classify` cpu/gpu, `submission` direct/indirect/batched,
`local-lights` off/direct/clustered, and on/off for `classify-check`, `occlusion`, `occlusion-check`,
`light-check`, `local-light-rig`. The rig is available only in its authored scene. Existing panel
cascades remain: GPU from CPU/Direct selects Indirect; Direct from GPU selects CPU; light-check on
selects Clustered; CPU or visibility off clears occlusion. Unavailable operations reject without
mutation. The original CLI-rejection gate remains failed for those resolved conflicts under the
[scoped owner-authorized exception](../milestones/ux/ux5.md#ux52--command-bridge); this is not
milestone acceptance. Applied setting marks clear when the operator edits/resets the group.

`debugview.set` accepts Final alone, or topic/value: temporal motion/reprojection/reprojected/
rejection/weight/age, lighting count/overflow/missed, occlusion mip 0–30. Availability matches
View > Debug View. `scene.open` accepts a catalog ID or document path. It requires a clean stopped
idle document, no measurement/pending file proposal, and selection None, Camera or Environment;
it shares replacement guards and preserves the semantic selection and current stopped editor view.
Object/light selection is refused because it cannot survive replacement without changing meaning.

`measure.run` uses nonnegative warmup and positive frames (32-bit total), stopped playback and
dynamic resolution off. It uses the interactive unscored measurement path with its own warmup,
frames and output; the Performance panel's Warmup, Frames and export path and an active mouse look
stay as the operator left them. `capture.gpu` needs
`MTL_CAPTURE_ENABLED=1` at startup; its trace name is generated. Graph dump exports the displayed
frame, including Freeze. Screenshot names receive `.png`; sequence names denote new directories.
Frames are positive 32-bit counts; sequence warmup is nonnegative. A capture refuses dirty documents,
pending file proposals, a disk hash different from the loaded pair, measurement or another job.
It spawns headless App with the clean loaded document, effective settings and lab overrides, no
`LMX_*` environment variable and no editor descriptor beyond its own log:
temporal off emits scale 1, rather than a dormant retained scale. Diagnostic views and dynamic
resolution are not replayed as dormant settings. The live viewport is not read back.

Save a plan as `plan.json`, then use the command above:

```json
{"summary":"Review diagnostics","steps":[
  {"command":"settings.set","args":{"temporal":"off"}},
  {"command":"graph.dump","args":{"name":"plan-graph.txt"}},
  {"command":"capture.screenshot","args":{"name":"plan-still","frames":32}}
]}
```

A plan has a summary and 1–32 Apply steps with object `args`. Queries, Propose and nested plans are
refused. One approval covers the immutable steps in order; the first failed step ends the plan.
Disconnect cancels pending approvals (`cancelled: client disconnected`); a running job finishes.
Listen off or quit cancels work.

## Proposal subjects and units

Use `query.hierarchy` IDs verbatim: `node:<n>`, `imported:<n>`, `object:<i>`,
`light:<index>:<generation>`, `dirlight:<i>`, `environment`, `camera`. IDs describe the current
scene, not portable references; stale generations fail. The query includes read-only subjects too.
Authored cameras use their document-node ID (`node:1` in the six checked-in scenes); literal
`camera` is not an alias for that editable saved camera.

| Editable subject | Fields / JSON values |
|---|---|
| Document/imported nodes, persistent objects/local lights | `enabled`: boolean |
| Movable, unanimated objects | `position`: XYZ meters; `eulerDegrees`: XYZ degrees; `scale`: XYZ factors 0.01–100 |
| Authored local lights | `position`: XYZ meters; `color`: RGB sRGB 0–1; `intensity`: nonnegative relative scalar; `range`: positive meters |
| Authored spot lights | `direction`: nonzero XYZ normalized on apply; `innerCone`, `outerCone`: degrees, 0 ≤ inner < outer ≤ 89 |
| `environment` | `exposure`, `bloom`: partial objects; `shadowFilter`: `"pcf"` or `"pcss"` |
| Saved scene camera `node:<n>` from `query.hierarchy` | `position`: XYZ meters; `yaw`, `pitch`: radians, pitch within ±π/2 |

Exposure keys are `autoEnabled`, `ev`, `lowPercentile`, `highPercentile`, `targetGrey`, `evMin`,
`evMax`, `compensationEv`, `adaptUpStopsPerSecond`, `adaptDownStopsPerSecond`; bloom keys are
`enabled`, `threshold`, `intensity`. Numeric values must be finite and within the document reader's
ranges: 0 ≤ `lowPercentile` < `highPercentile` ≤ 100, `targetGrey` > 0, `evMin` ≤ `evMax`,
nonnegative adaptation speeds and nonnegative bloom threshold and intensity. One value out of range
answers `invalid` and refuses the whole batch. Camera edits change the saved scene camera, never the
editor camera. Pose refusals use `<subject>/<field>: <poseLockReason>`; mobility refusals use
`<subject>/mobility: Mobility is authored in the scene file`. Generated edits, directional lights and structural, selection or playback edits are unavailable.

## External file proposals and sidecars

Write the glTF and companion buffer, then publish the matching sidecar:

```sh
python3 Tools/Session/lmx_session.py sidecar Assets/Scenes/temporal-lab.scene.gltf --actor RendererReview --summary 'Adjust saved look' --evidence evidence/still.png
```

This example writes beside the named document; use the copy you intend to edit. Schema 1 has
`actor`, `summary`, `evidence` paths relative to the sidecar and `documentSha256`. The hash is raw
glTF bytes followed by its referenced external buffer bytes. `x.scene.gltf` maps to
`x.scene.proposal.json`. The client atomically publishes the sidecar after validating the pair.

The editor polls the pair twice a second, except during Measure. A stamp holds size, modification
time, inode and device for both files, so an idle poll is three `stat` calls. After a change it
waits for one identical poll, hashes the pair and waits up to four seconds for a matching sidecar. A
`.lmx-save-*.tmp` directory defers the watch for 5 s; a leftover one then logs one Console warning
and the watch proceeds. A formatting-only change adopts the new hash with one Console info line and
no card. Without a matching sidecar it attributes Unknown external change to System. Show lists
property rows and rings affected Hierarchy nodes. Missing evidence is flagged. File Accept uses
Revert, needs Stopped/no measurement and offers Discard/Cancel for unsaved edits. Reject remembers
the current hash; a later Save can overwrite that file. Pending file proposals block Save/Save As
with “Review the pending proposal first”. Save, but not Save As, also hashes the pair on disk and
refuses unless that hash is the loaded one or one the operator rejected: “The scene file changed on
disk; its proposal will appear in the Session panel shortly” before the card exists, or “The scene
file or its buffer cannot be read on disk”. A newer file or scene replacement stales the proposal.
Reader failures show an Error card with Reject only. Invalid UTF-8 sidecars are rejected before
metadata is retained. Own saves reset the watch only with verified writer bytes, path and coherent
stamps; an uncertified successful adoption invalidates the baseline to review external changes.

## Evidence, Export and recovery

Output goes under build-local `session/<UTC yyyyMMdd-HHmmss>-<pid>/`. Supply a name, not a path:
1–128 ASCII bytes from letters/digits/`._-`, no leading `.`, separators or `..`. Existing outputs
are refused. `session.json`, including case aliases, is reserved for Export. Directory/target
checks refuse symlinks, but they do not provide sandbox protection against same-UID races.

Screenshot/log/measurement/graph files get SHA-256 hashes. A sequence records its `manifest.json`
hash and child log; this does not certify each PNG independently. GPU evidence hashes every regular
payload in the trace directory plus its `.schema.json` sidecar. Failed spawn logs are retained;
missing/unreadable/unsafe outputs yield explicit evidence-certification failures, not empty hashes.
Native Metal traces in validation contained internal relative symlinks; all four approved captures
failed certification as unsafe GPU entries. Those traces are not certified session evidence. A
failed step ends its plan; inspect the failure before approving a separate remaining job.
One session job runs at a time. The activity strip shows its state/progress; Stop cancels active
session work. Measurement cannot overlap a headless child. Shutdown kills/reaps the child and
joins the listener; the socket is removed when Listen stops.

Export writes schema 1/protocol 1 `session.json`, with loaded path/hash, last connected client,
a top-level `dropped` count, ordered actions and Console entries. The log retains 10,000 actions,
each command kept to 128 bytes and its arguments to 4,096 bytes with a `…[truncated N bytes]`
suffix. Export works after Listen off and retains the last client.
It uses the held Console snapshot with current search/minimum severity and Operator+Agent actors;
select those actors in Console to compare Copy visible. Each array item is one formatted entry,
including multiline text. A frozen Console may omit newly recorded actions from its held rows.
The actions include `export.request` before serialization; `export.result` reflects the actual
write afterward and appears in the next Export. Copy path/Reveal notices locate the output.

| Error | Recovery |
|---|---|
| `protocol` | Send valid UTF-8 object envelopes, hello first with protocol 1 |
| `tier` | Reconnect with `--wait-tier`, then have the operator raise that connection's ceiling |
| `denied` | Review the operator's decision before submitting new work |
| `unavailable` | Read the shared disabled reason; stop playback/measurement, save/review the document or enable capture capability |
| `invalid` | Correct names, IDs, fields, JSON values, counts or plan structure |
| `busy` | Wait for the current client, job, 8 approvals or 64 pending proposals to clear |
| `failed` | Inspect action outcome and retained child log/evidence certification message |
| `cancelled` | Inspect Stop/shutdown outcome before retrying |

Lines are limited to 1 MiB, the inbox to 64 requests, hello names to 128 bytes and plan summaries
to 1,024 bytes. Resolved proposals beyond 64 retained are evicted oldest first. A second client
gets busy and closes. Oversized lines
close the connection after a protocol error; a full inbox gets busy. Console remains bounded at
2,000 entries/2 MiB with 16 KiB messages. Restarting does not restore connection, tier, proposals or
session jobs. Export retained evidence before ending a run. For controlled GPU evidence, see
[GPU debugging](gpu-debugging.md); session output does not establish whole-application acceptance.
