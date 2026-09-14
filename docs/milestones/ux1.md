# UX1 — Editor Usability and Diagnostics

**Status**: Proposed

This is the milestone's initial design specification, prepared before implementation. It
summarizes the intended behavior and verification; it will become the implementation/evidence
record at closure. The [Editor Experience roadmap](../roadmap/editor-experience.md) owns the
accepted P1–P3 boundary and delivery order. The
[detailed design](../specs/2026-09-14-ux1-editor-experience-design.md) specifies interactions and
presentation, and the [execution plan](../plans/2026-09-14-ux1-editor-experience.md) sequences work.
No UX1 implementation or acceptance result is recorded yet.

## Objective and baseline

The editor should let a person identify a scene subject, understand and restore its settings,
read the effective rendering configuration, find expensive measured work and inspect one coherent
frame. The [baseline audit](../research/2026-09-14-editor-uiux-audit.md) observed clipped values
and labels, ambiguous temporal/controller status, unstable graph navigation under Native TAA,
weak selection feedback and missing explanations/results for diagnostic and capture actions.

The renderer baseline remains M6.5 plus the completed R1 work.
[Interface gate B](interface-gate-b.md) is PASS; the roadmap places UX1 before M7.1 while retaining
that technical approval and [ADR 0021](../decisions/0021-gpu-scene-handoff-contract.md). All P1–P3
areas are required by the accepted boundary; their priority determines sequence, not inclusion.

## Intended implementation

### Workspace and visual system

Retain the maximized main macOS window with its native title bar, the four docked panels and the
independent undocked Render Graph window. Scene stays left, Viewport center, Inspector right and
Performance below. Preserve saved docking/visibility through explicit workspace migration when
needed; Reset Default Layout remains available. The design's logical-point dimensions are initial
targets to measure at maximized and 1280 × 720 pt client sizes.

Use shared typography, spacing, field rows and state colors across panels. Give editable controls,
read-only telemetry, section headings and exceptional states distinct hierarchy. Wide field rows
place labels left and controls right; narrow rows stack them. Wrap or deliberately scroll long
content, retain complete names in details/hover/copy, and label vector components and units.
Loading, empty, unavailable, failed and frozen states carry readable text as well as color.

### Inspector and truthful state

Group Rendering into Exposure, Bloom, Shadows, Reconstruction, Resolution and Display & Details.
Show current mode and extents prominently, with advanced/history/vendor/display information in
explicit Details sections. Camera, object and light inspectors share the same field conventions.

Separate requested, effective and available reconstruction. Temporal-off presentation reports
Off/N/A and the actual full-resolution extent; dependent controls explain why they are disabled.
Vendor fallback retains the request beside the effective algorithm and reason. Distinguish live
retired-frame telemetry from a dynamic-resolution controller's last observation. Waiting data
must not become a false zero, and a last-reset event retains its paired reason and frame. Preserve
the meaning of existing renderer/capture fields while adding editor presentation where needed.

### Performance and frame inspection

Performance presents readable frame-interval/FPS, timed-pass-sum, extent, draw and memory summaries.
The timed pass sum keeps its current limited meaning; it is not total GPU frame cost. The sortable
pass table distinguishes latest values from rolling averages and identifies sample counts/window.
The interval plot has labeled units, history and a marked 60 Hz reference. Full internal pass
names and byte-exact memory values remain available in details.

`Freeze metrics`, `Clear history` and `Resume metrics` operate on a coherent displayed snapshot.
They do not pause the scene. Render Graph independently shows Live/Frozen and its frame number;
freezing owns that compiled record and only timings matched to its frame. Dump exports the record
currently displayed. Unavailable matched timing is N/A rather than a newer frame's measurement.

Keep graph selection, group state, pan/zoom and user positions stable while physical temporal
resources alternate. Investigate the observed signature/invalidation behavior before choosing a
fix; preserve visibility of real topology changes. Provide Fit graph, Fit selection and 100%
actions, readable resource details and card layouts that do not overlap when selected. Frozen
records remain valid through later frames and scene changes; Resume reconciles selection clearly.

### Scene and daily workflows

Keep the scene selector, clearable search and counts above the scrollable grouped list. Use source
names where available, with scene-local disambiguation for duplicate/unnamed subjects. A selection
hidden by filtering stays explicit in Inspector. Frame selected and a viewport bounds/highlight
cue identify the selected geometry; missing reliable bounds produce a clear unavailable state.
Editor cues remain outside ordinary scene-only captures.

Each editable group exposes Reset and changed-from-default state. Object/light resets use authored
scene defaults, camera reset uses the scene's initial camera and rendering reset uses documented
editor defaults. Animated subjects go through SceneSession at the current playback time; preserve
motion discontinuity handling and scene-linear radiance semantics.

Viewport transport shows scene Play/Pause, time, Step (1/60 s) and Reset time. Scene playback,
metric freeze and graph freeze stay independent. Contextual camera help and keyboard focus make
navigation discoverable without sending text-entry keys to camera or capture actions.

Every supported diagnostic view has a mode badge, shader-derived legend/range/units and Return
to Final. Unsupported vendor views explain their availability. MaterialLab identifies its axes;
lab scenes provide concise purpose/context. Dump/capture provide availability, pending/result
feedback, recoverable failure reasons and output-path reveal/copy actions. Capture menu and C
shortcut share the same action result, including the default capture-disabled explanation.

## Verification plan

The [roadmap completion gate](../roadmap/editor-experience.md#completion-gate) governs acceptance.
The following evidence is required to turn this proposed specification into a completed record:

| Area | Planned evidence | Current result |
|---|---|---|
| Layout and readability | Matched maximized/1280 × 720 pt screenshots with original, reset and restored layouts; full labels, values and reachable actions | Pending implementation |
| Rendering state | Temporal on/off, Raw/Native/vendor/fallback, manual/dynamic scale, sample freshness and persistent reset-event checks | Pending implementation |
| Performance | Most-expensive-pass task, latest/average/window interpretation and coherent Freeze/Clear/Resume checks | Pending implementation |
| Render Graph | At least ten seconds of Native TAA selection/navigation stability; frozen record/timing/dump agreement; real topology changes and resume | Pending implementation |
| Scene and recovery | Large-scene search/identification/framing, hidden selection, component labels and static/animated edit/reset tasks | Pending implementation |
| Diagnostics and playback | Every supported legend and unavailable-view explanation, MaterialLab axes and TemporalLab transport | Pending implementation |
| Actions and workspace | Capture-disabled and capture-enabled paths, write failures/results, menus, panel visibility, persistence, detached window, real camera/docking gestures | Pending implementation |
| Engineering | Build, formatting, policy, focused behavior/unit tests; image/GPU evidence for rendering-facing changes under engineering rules | Pending implementation |

Retain commands, environment, screenshots, action/result records and unsuccessful runs outside
published source. Record real-device coverage separately from presentation fixtures and synthetic
tests. At closure, replace pending entries with actual evidence, implementation details and limits;
do not infer human usability from a clean build or a source-level assertion.

## Known limits and design boundaries

The baseline audit did not fully validate continuous fly-camera gestures, docking/node dragging,
capture-enabled success, unsupported-vendor hardware or multi-monitor behavior. Those observations
remain limited; use the plan's actual coverage requirements and identify any remaining gaps.
The graph instability symptom is confirmed, but the proposed alternating-resource explanation
is still a hypothesis. None of the proposed corrections is already implemented.

Retain the AppModel/module boundaries, Native TAA default and SDR/UI/capture domains. GPU scene
identities remain owned by M7.1; current scene-local identifiers must not imply that future
contract is implemented. The [roadmap's deferrals](../roadmap/editor-experience.md#boundaries-and-deferrals)
keep full picking/gizmos, general Undo, asset tooling, broad theme/layout management and renderer
feature expansion outside this milestone. Historical M6/R1 evidence limits remain unchanged.

## Integration state

The milestone specification, detailed design and plan are Proposed; only their roadmap boundary
and delivery order are accepted. No executor plan is active and no UX1 completion is claimed.
Implementation follows the existing three-stage plan. At closure, this file records shipped
behavior, validation results and durable deviations, and becomes the stable handoff record for
the next rendering milestone.
