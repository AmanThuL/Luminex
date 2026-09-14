# Editor Experience

**Status**: Accepted

Part IV of the [rendering roadmap](../roadmap.md) owns editor usability and the presentation of
rendering evidence. The [2026-09-14 audit](../research/2026-09-14-editor-uiux-audit.md) found that
the shipped controls expose substantial capability, but clipped data, ambiguous states and an
unstable graph make that capability difficult to inspect. This part owns the accepted boundary; the milestone record distinguishes implementation
from completed acceptance.

## Placement and ownership

The accepted delivery order is **interface gate B (PASS) → UX1 → M7.1**. Finish the complete UX1
boundary below before opening the M7.1 executor plan. The
[gate B record](../milestones/interface-gate-b.md) and
[ADR 0021](../decisions/0021-gpu-scene-handoff-contract.md) remain accepted: UX1 changes project
priority without invalidating or repeating the technical handoff review.

Rendering Foundations owns the existing image, temporal and display contracts; GPU-Driven Hybrid
Rendering owns scene identities/tables, visibility and lighting; Codebase Refactoring owns
structural reorganization. UX1 uses those boundaries and improves the editor through their current
contracts. Later renderer features consume its presentation conventions as they add real data.

## UX1 — Editor usability and diagnostics

**Outcome:** a person can find a scene subject, understand and restore its settings, read the
active rendering configuration, identify expensive work, and inspect a stable frame without
guessing clipped text, inferring color meanings or reading the terminal for action results.

**Deliver:** all P1–P3 items in this table. Priority determines implementation order and risk;
P2 and P3 are part of completion, not optional polish.

| Priority | Area | Required outcome |
|---|---|---|
| P1 | Performance | Readable summaries, meaningful units, labeled plots and a sortable cost table; measurement source, sample window, freshness and unavailable values are explicit |
| P1 | Inspector | Consistent label/control layout, readable narrow panels and task-oriented settings groups; advanced diagnostics remain available with clear hierarchy |
| P1 | Status correctness | Requested, effective and available modes are distinct; disabled dependencies explain why; last reset reason/frame stay paired; controller measurements cannot masquerade as current GPU frame time |
| P1 | Render Graph | Stable live selection/navigation under Native TAA; freeze one coherent frame; 4 Hz coherent frame publication, fit/navigation controls, readable details and non-overlapping selected cards |
| P2 | Scene identification | File-menu catalog loading, persistent search/counts and an indented Hierarchy; explicit hidden selection; Frame selected and a visible-geometry outline |
| P2 | Diagnostics and labs | Viewport mode name, accurate legend/range/units, return to Final, and concise lab purpose/axis annotations |
| P2 | Playback and recovery | Discoverable transport, time and step duration; distinguish playback, graph freeze and metric freeze; labeled components/units and per-group reset to defined defaults |
| P2 | Capture and dump | Availability and reason, pending/result feedback, output path and reveal/copy actions; both menu and keyboard routes report the same outcome |
| P3 | Visual consistency | Shared typography, spacing, field layout and status colors; controls and telemetry have distinct hierarchy; usable default and compact layouts, including loading/empty/disabled states |

**Sequence:** establish one overall interaction and visual design before implementing individual
panels. Deliver three stages under one UX1 plan, without creating more milestone identifiers:

1. Trustworthy, readable diagnostics: P1 plus the shared visual primitives and capture/dump
   feedback needed to make diagnostic actions understandable.
2. Complete daily workflows: scene identification, diagnostic legends, playback and recovery,
   using the same layout and state conventions.
3. Complete visual consistency and whole-application acceptance: apply the design across all
   panels, reconcile default/compact/detached layouts and run the complete interaction matrix.

P3 informs stage 1 rather than being postponed until the last stage. Each stage must leave a
usable application; completing one stage does not pass UX1 or admit M7.1.

## Completion gate

All required areas above must pass the following observable tasks on the actual application:

- Start with `xmake && xmake run App`, retaining the native macOS title bar. Verify both restored
  and Reset Default Layout states, at the maximized usable display bounds and a 1280 × 720 pt
  client window. Core labels, values and actions remain readable/reachable; narrow content
  reflows or scrolls deliberately rather than silently clipping essential data.
- In Sponza and San Miguel when installed, search for a subject, distinguish duplicate names,
  locate the selection in the viewport, edit its transform and restore it. Filtering must not
  leave unexplained Inspector content. Missing optional content has an actionable state.
- Switch Raw, Native TAA and MetalFX, toggle temporal inputs and dynamic resolution, and read
  requested/effective reconstruction, render/output extents and timing freshness correctly.
  Explain disabled diagnostic views and capability fallback; no unavailable value appears as a
  valid zero. Existing render/display semantics remain intact.
- Find the most expensive measured pass, explain latest versus rolling average and the timed
  pass sum, freeze metrics, clear history and resume without mixing snapshots or stale states.
- Under default Native TAA, retain graph selection, group state and pan/zoom for at least ten
  seconds across history-slot alternation. Freeze a frame, inspect its resources and matched
  timings, dump that same record, then resume live. Genuine topology changes remain visible.
- Read every supported debug view using its legend, return to Final, identify MaterialLab axes,
  operate TemporalLab playback/step/reset and recover changed camera/light/rendering settings.
- Complete dump and capture workflows with visible output paths or actionable failure reasons.
  Verify capture-disabled startup and capture-enabled success, as well as recoverable failures.
- Exercise menus, panel visibility, workspace persistence/reset, detached graph close/reopen,
  keyboard focus, fly-camera gestures and docking/navigation gestures. Record an unverified
  gesture as unverified, never as a pass inferred from source or screenshots.

Retain before/after screenshots and action/result records outside published source; document
actual coverage, unresolved defects and evidence limits in the [UX1 milestone record](../milestones/ux1.md),
which records owner acceptance for integration and the limits of automated evidence.
The original audit's tool limitations require new verification, not inherited passes. Apply the
[engineering evidence rules](../conventions/engineering.md#validation-evidence) by change risk;
UI work neither closes historical M6/R1 exceptions nor substitutes for GPU/image validation.

## Boundaries and deferrals

Retain ImGui, the native maximized main window, the independent undocked Render Graph window,
versioned workspace persistence and the AppModel/module layering. Preserve Native TAA as the
default and the SDR/UI/capture domains. Fix observer data where truthful presentation requires it;
do not alter rendering algorithms merely to improve a status label.

Frame selected and a selection cue are included. Full viewport picking, transform gizmos, a
general Undo/Redo stack, asset browser/database, arbitrary layout-preset management and theme
editing are deferred. Likewise, side-by-side image comparison and a general event-log product
are not required. The manual review adds a bounded read-only Console for existing logs, with
filter/copy/clear/freeze, while keeping CLI output. Contextual hover help supplements visible
status and failure reasons. The bounded action feedback and reset behavior above must still ship.

GPU scene identities/tables remain owned by M7.1. UX1 may expose current source names and
scene-local identifiers for disambiguation, without presenting them as durable GPU IDs. No
renderer feature, new backend, HDR/EDR path or broad UI framework replacement expands UX1.

The [implemented design](../specs/2026-09-14-ux1-editor-experience-design.md) records the interaction
contract. On 2026-09-14 the owner accepted the result after manual review and requested integration
and PR merge. The executor plan is closed. This acceptance does not turn unverified automated
gestures or known GPU/image limitations into passes; the [milestone](../milestones/ux1.md) retains
them. M7.1 is eligible for a separate plan and remains inactive.
