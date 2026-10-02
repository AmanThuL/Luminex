# ADR 0030 — Session protocol and trust

**Status**: Proposed

## Context

The editor accepts external document proposals and local commands while the operator keeps control
of scene edits and evidence jobs. The implemented protocol needs an explicit trust contract that
separates client requests from operator decisions. The [UX5 record](../milestones/ux/ux5.md) owns
the scope and outstanding acceptance gates; this ADR does not assert owner acceptance.

## Decision

Use protocol 1 over a Unix domain socket with UTF-8 newline-delimited JSON. The listener accepts
one client with the editor's effective user ID. The socket is mode 0600, off by default, enabled
only for the current run, and removed when listening stops. Listen replaces an existing path only
when it is a socket the same user owns that refuses connections. The client must send `hello` with a
nonempty name and protocol 1 before any command. Requests carry an integer ID, command name and
object arguments; one response completes a request after execution, denial or job completion.
Each command accepts only its own argument members. An unknown member, a member repeated at any
depth, an object over 64 members and nesting over 32 answer `invalid`, so the values a command
reads are the ones its approval card shows.

Each connection starts at ReadOnly. Only the operator can change its ceiling to Propose or Apply
in the Session panel. Above-ceiling requests fail immediately with `tier`; they are not queued.
Propose creates reviewable scene edits; Accept and Reject remain editor actions. Apply requires
Approve or Deny for every command, or one approval for an immutable submitted plan of at most 32
Apply steps. Plans run in order and stop at the first failure; nested plans are refused. No command,
flag or environment variable accepts, approves or raises a ceiling. Client `--wait-tier` polls
read-only status on the same connection until the operator changes the ceiling.

An approval awaiting review does not outlive the conditions it was submitted under. Lowering the
ceiling cancels awaiting approvals, and a ceiling of ReadOnly also stales pending bridge proposals.
Open, the catalog, Revert and `scene.open` cancel awaiting approvals; a plan the operator already
approved keeps running. At most 8 approvals await or run and a further request answers `busy`.
Approve and Deny are ignored for 0.5 s after the shown approval changes. A pending proposal is
never evicted; `propose.edits` answers `busy` at 64 pending bridge proposals and `propose.withdraw`
works only from the submitting connection. Bridge Accept re-runs the preview and stales the card
when its rows differ from the ones shown. Save hashes the pair on disk before writing and refuses
a change the operator has neither loaded nor rejected.

The listener thread touches the socket and mutex-guarded mailbox only. The main thread executes
commands after native menu commands and before document work, outside both the ImGui and device
frames. A session cannot change selection, the editor camera or playback. Scene replacement
requires a clean stopped document and a stable semantic selection; it preserves the current view.
Camera proposals edit the saved scene camera independently of the editor camera.

Evidence output names are checked and confined to the build-local session directory. Empty names,
leading dots, separators, `..`, names over 128 bytes and unsupported characters are refused;
`session.json` and its case aliases are reserved for Export. Existing targets and symlinked output
components are refused, except that Export replaces its regular record. These checks reduce
accidental overwrite and traversal; they do not provide sandbox protection against same-UID races.
The peer check trusts the local user, not the client's name or its claimed actor.

Attribute requests/results to Agent and review/tier/export actions to Operator. Record action order,
UTC time, client, arguments, tier, plan, outcome and evidence SHA-256. GPU capture certification
hashes every regular payload and the schema sidecar; sequence certification hashes its manifest.
Unreadable or unsafe evidence yields an explicit certification failure. Export uses the held
Console snapshot and its search/severity filters with Operator and Agent actors selected. The log
keeps a command to 128 bytes and its arguments to 4,096 bytes with a truncation note, retains the
newest 10,000 actions and reports the dropped count in `query.log` and the export.

## Consequences and limits

- The operator can inspect the complete request before permitting a change. ReadOnly queries need
  no approval; a ceiling alone never approves Apply work.
- A stalled main loop delays responses. Disconnect cancels pending approvals; already running work
  finishes. Listen off and shutdown cancel work, join the listener and kill/reap the child.
- One local client and one job simplify ownership. No network transport, MCP layer, in-process
  client execution, automatic approval, live viewport readback or session persistence is provided.
- Headless captures use the loaded clean document and effective rendering arguments. The child
  starts with close-on-exec default descriptors and without any `LMX_*` environment variable. It
  shares the GPU with the editor; captures are evidence jobs, not realtime performance measurements.
- An approved plan runs its remaining steps after its client disconnects. Any same-user socket that
  refuses connections is reclaimed, and another process can place a symlink at the socket path
  between the path check and `bind` in a shared temporary directory.
- Current behavior is described in [App Session](../architecture/app-session.md) and the
  [operator/client guide](../guides/agent-session.md). Native gestures and whole-application
  acceptance remain bounded by the [validation record](../milestones/ux/ux5-validation.md) and the
  [review record](../milestones/ux/ux5-review-validation.md).
