# Editor Experience

**Status**: Accepted

Part IV of the [rendering roadmap](../roadmap.md) owns editor usability and the presentation of
rendering evidence: UX1 before M7.1, then [UX2](#ux2--editor-surfaces),
[UX3](#ux3--scene-documents-and-hierarchy) and [UX4](#ux4--design-system-and-themes) between R4
and N1, ending with [UX5](#ux5--agent-session); [UX6](#ux6--scene-authoring) is in progress and placed before N1. The [2026-09-14 audit](../research/2026-09-14-editor-uiux-audit.md) found that
the shipped controls expose substantial capability, but clipped data, ambiguous states and an
unstable graph make that capability difficult to inspect. This part owns the accepted boundary; the milestone record distinguishes implementation
from completed acceptance.

## Placement and ownership

The roadmap entry's [execution sequence](../roadmap.md#execution-sequence) places UX1 as
**interface gate B (PASS) → UX1 → M7.1**. Finish the complete UX1 boundary below before opening
the M7.1 executor plan. The
[gate B record](../milestones/m6/interface-gate-b.md) and
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
- Read every supported debug view using its legend, return to Final, identify a selected object's axes with the gizmo,
  operate TemporalLab playback/step/reset and recover changed camera/light/rendering settings.
- Complete dump and capture workflows with visible output paths or actionable failure reasons.
  Verify capture-disabled startup and capture-enabled success, as well as recoverable failures.
- Exercise menus, panel visibility, workspace persistence/reset, detached graph close/reopen,
  keyboard focus, fly-camera gestures and docking/navigation gestures. Record an unverified
  gesture as unverified, never as a pass inferred from source or screenshots.

Retain before/after screenshots and action/result records outside published source; document
actual coverage, unresolved defects and evidence limits in the [UX1 milestone record](../milestones/ux/ux1.md),
which records owner acceptance for integration and the limits of automated evidence.
The original audit's tool limitations require new verification, not inherited passes. Apply the
[engineering evidence rules](../conventions/engineering.md#validation-evidence) by change risk;
UI work neither closes historical M6/R1 exceptions nor substitutes for GPU/image validation.

## UX1 boundaries and deferrals

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

The [implemented design](../milestones/ux/ux1-design.md) records the interaction
contract. On 2026-09-14 the owner accepted the result after manual review and requested integration
and PR merge. The executor plan is closed. This acceptance does not turn unverified automated
gestures or known GPU/image limitations into passes; the [milestone](../milestones/ux/ux1.md) retains
them. M7.1 is eligible for a separate plan and remains inactive.

## UX2 — Editor surfaces

**Placement:** [R4](codebase-restructuring.md#r4--shader-source-deduplication) → UX2 → UX3 → N1.
The owner inserted it on 2026-09-25 and renumbered scene documents to UX3; records and ADRs dated
earlier call scene documents UX2.

**Outcome:** every control has one home where its task belongs. The viewport shows the image and
overlays that describe it, the transport controls time, panels show controls before readings, and
status appears when it is abnormal. No capability is lost except redundant routes.

**Deliver:** shared surface conventions, File/View/Window/Debug/Help menus, a header-free viewport,
one Debug View selector and the transport in the menu-bar row, with measurement started from
Performance (UX2.1); a scene-only Hierarchy, a Rendering panel and one property-grid Inspector
layout (UX2.2); a one-row Console, a compact docked Performance tab beside the detached window and
a one-row Render Graph header (UX2.3); workspace schema 4 and acceptance (UX2.4). The
[implemented record](../milestones/ux/ux2.md) holds the placement map and decisions.

Implemented and owner-accepted on 2026-09-27; the executor plan is closed. The
[validation record](../milestones/ux/ux2-validation.md) retains passing 8/8 exact-image pairs,
unit/GPU suites and native schema migration, plus unverified per-gesture evidence and the
corrections made after acceptance. It is integrated by squash merge.

**Exit gate:** the [completion gate](#completion-gate) tasks pass on the new layout at both window
sizes; every command removed from a surface stays reachable by a named route; schema 3 workspaces
migrate without loss; scene-only screenshots are byte-identical to the parent.

**Defer:** renderer, capture, manifest, measurement and CLI changes; everything UX3 owns;
everything the UX1 deferrals above already name.

## UX3 — Scene documents and hierarchy

**Placement:** [R3](codebase-restructuring.md#r3--subsystems-and-tree-restructure) →
[R4](codebase-restructuring.md#r4--shader-source-deduplication) → UX2 → UX3 → N1, so
new editor and scene code lands in the restructured tree and the learned-rendering lab starts from
saved scenes. UX3 changes behaviour, so it uses its own gates and one owned re-baseline instead of
the refactoring comparison protocol.

**Outcome:** a scene is a saved document. An operator opens it, disables an object, edits a light
or the look, saves, relaunches and finds the same scene; the Hierarchy shows only scene content;
an authored disabled state is never confused with culling; the application has an icon.

**Deliver:** valid glTF 2.0 scene documents under `Assets/Scenes/` (`KHR_lights_punctual`,
glTF cameras and animations with keys in a standard external buffer, and one `LMX_scene` extension
for asset references, overrides, lab generators, enabled state and the saved look) loaded under
the unchanged editor, replacing the C++ light rig, camera rails and initial cameras (UX3.1);
Damaged Helmet joining MaterialLab and Milk Truck joining TemporalLab, leaving a six-scene catalog
(UX3.2); enabled state for lights and objects across Scene, both classification paths, counters
and measurement (UX3.3); a document-grouped Hierarchy, UX2's Inspector header checkbox extended to
objects and Open/Save/Save As/Revert (UX3.4); the application icon and whole-application
acceptance (UX3.5). The scene's look is saved; renderer
configuration stays with the editor session and CLI. The [implemented record](../milestones/ux/ux3.md)
holds the contract.

Implemented on 2026-09-29; the executor plan is closed and the owner authorized integration by
squash merge the same day. The image gates failed as measured because the parent's non-unit
directional rig is now normalized; ADR 0028 stays Proposed. The
[final validation](../milestones/ux/ux3-final-validation.md) records the build, contract, Metal and
document checks and the failed gates.
Native verification covers 62/74 gestures; twelve rows and Dock/switcher appearance remain
unverified. ADR 0028 stays Proposed. These measured limits do not amend the exit gates.

**Exit gate:** converted scenes match the parent under the exact-image matrix at `--temporal off`
with identical graph dumps, reported as measured with no tolerance approved in advance; saves are
byte-stable and pass a pinned glTF validator; the re-baseline lists every retired and new hash and
carries the owner's acceptance, and the standing matrix becomes Sponza, MaterialLab and
TemporalLab; a disabled object contributes to no target and no counter but its
own, identically on CPU and GPU classification; the [completion gate](#completion-gate) tasks
still pass on the new layout; capture manifests and measurement reports record the document hash.

**Defer:** create, duplicate, delete and reparent; importing an asset into an open scene;
persisting renderer configuration; an `.app` bundle; everything the UX1 deferrals above already name.

## UX4 — Design system and themes

**Placement:** [UX3](#ux3--scene-documents-and-hierarchy) → UX4 → N1. The owner asked for it on
2026-09-29, before the learned-rendering lab, so N1's surfaces are designed once in the new system.
The [record](../milestones/ux/ux4.md) was accepted on 2026-09-30 after the owner reviewed the Figma
pages. It is implemented and owner-accepted for integration; see below.

**Outcome:** the editor has one token-driven design system instead of ImGui's default dark style.
An operator picks Auto, Light or Dark once and the whole editor, including its detached native
windows, follows; both themes read at WCAG 2.2 AA contrast; the design language says who acted on
a value (the operator, the editor's own automation, or a future agent), what is session-only and
what is running, using the same marks on every surface; drawing stays as cheap as today's flat
ImGui within a measured budget.

**Deliver:** semantic token tables for both themes covering every ImGui and node-editor colour,
the clear colour and every editor colour, with View > Appearance (Auto follows macOS live),
per-window native appearance, a colour crossfade, workspace schema 5 and contrast unit tests
(UX4.1); rounding, border, spacing and type tokens with Geist Sans and Geist Mono replacing Inter,
Comfortable and Compact density, neutral default buttons with accent primaries, and themed graph, plot, overlay,
notice and legend surfaces (UX4.2); actor and provenance marks for dirty, generated, CLI-masked
and controller-applied state, an activity strip for measurement, capture, loading and controller
changes, and the reserved agent vocabulary as tokens, Figma components and gallery renderings only
(UX4.3); a Style Gallery window, retained gallery captures in both themes, documentation, an ADR
for the theme and token contract, and whole-application acceptance (UX4.4); the macOS menu bar as
native chrome from one platform-neutral menu model, the in-window row reduced to a toolbar for the
transport, activity and zoom, and the ImGui menus kept for other platforms (UX4.5).

Implemented and owner-accepted for integration on 2026-10-01; the executor plan is closed and the
change integrates by squash merge. The eight-round exact image matrix fails (11/15 on the earlier
runtimes, 14/15 on the final one) and both Compact draw-index cells fail at 1.1561092764935454×
against 1.15×; the full native gate is incomplete. These results are retained as measured with no
tolerance or default change, and ADR 0029 stays Proposed. The
[review validation](../milestones/ux/ux4-review-validation.md#owner-acceptance-and-integration)
lists the retained failures and the unverified native checks, and does not amend the exit gates.

**Exit gate:** contrast tests pass for every required pair in both themes; scene-only screenshots
for the reference set are byte-identical to the parent; a theme switch rebuilds no font atlas and
allocates no GPU resource; the [completion gate](#completion-gate) tasks pass in both themes at
both window sizes; rendered index counts on the default Sponza layout stay within 15% of the
parent in both themes and densities; schema 4 workspaces open with Auto and Comfortable; every
existing state keeps its status text and gains a mark whose tooltip names its source; a macOS
appearance change retints the editor without relaunch, recorded as a manual gesture; every command
keeps its one named route with unchanged shortcuts and visible disabled reasons through the native
menus, which the detached windows reach too; unverified gestures are recorded as unverified.

**Defer:** renderer, capture, manifest, measurement and CLI changes beyond `--appearance`; an
agent runtime, session log or command bridge ([UX5](#ux5--agent-session)); Windows chrome until a
validated host exists; high-contrast variants; user theme editing; blur, shadows and glow; a new
icon set; everything UX1–UX3 already defer.

## UX5 — Agent session

**Placement:** [UX4](#ux4--design-system-and-themes) → UX5 → N1. On 2026-10-01 the owner placed all three slices before N1, so N1's studies can be the first work run through the bridge; the [record](../milestones/ux/ux5.md) is implemented; the owner authorized integration (see below). Agents keep working as they do today, through documents, the CLI and pull requests; UX5 makes that work visible and reviewable in the editor without an agent runtime inside it.

**Outcome:** an agent's change to a scene reaches the operator as a proposal with an actor, a summary and evidence links, reviewed before it applies; a live session issues the same commands the CLI offers, with attribution, permission tiers and plan-level approval; every agent action is logged and exportable with its evidence.

**Deliver:** file-based proposals (UX5.1): an external change to the open document plus a sidecar naming the actor, summary and evidence paths appears as a proposal with Show (the changed nodes and fields), Accept (reload) and Reject, through UX4's proposal card, attention ring and Console actor row; a local command bridge (UX5.2): a socket carrying the CLI's commands and editor queries with actor attribution, tiers (read-only, propose, apply with approval), plan-level approval for multi-step jobs, headless capture runs and the activity strip; session log and evidence export (UX5.3): the Console's actor filter and an exportable session record joining actions to evidence.

Implemented; on 2026-10-02 the owner authorized integration by squash merge after an independent review, without re-running native gestures, and the executor plan is closed. Gallery exact comparison (Light and Dark 0/16), four of the five original CLI rejection pairs, the initial standing matrix (14/15) and native GPU capture certification (0/4) fail as measured, and the original five-case Off gate is incomplete; no tolerance or default changed and ADR 0030 stays Proposed. The [review validation](../milestones/ux/ux5-review-validation.md#owner-authorization-and-integration) lists the retained failures and unverified native checks, and does not amend the exit gates.

**Exit gate:** a proposal from a modified document lists exactly the fields the writer reports as changed, Accept yields the same scene as opening the file and Reject leaves the in-memory scene untouched; the bridge refuses any command outside its tier and records every action with its actor; the exported log reproduces the Console view; scene-only screenshots stay byte-identical.

**Defer:** an in-process agent, autonomous apply without approval, Undo/Redo (deferred since UX1), multi-user sessions, a live viewport readback, MCP or network transports.

## UX6 — Scene authoring

**Placement:** [UX5](#ux5--agent-session) → UX6 → N1. The owner asked for the proposal on 2026-10-02 and placed it before N1 on 2026-10-03, answering the record's open decisions the same day. The owner accepted the [record](../milestones/ux/ux6.md) and authorized execution on 2026-10-03; the [plan](../plans/2026-10-03-ux6-scene-authoring.md) is in progress and the outcome and gates below bind.

**Outcome:** lab objects are saved in the scene document; each object and light says whether it may move; an operator moves a movable subject with a viewport gizmo and cannot move a static one.

**Deliver:** lab objects, materials and animations saved in the scene documents, with geometry in a read-only buffer and textures in image files beside them, and only parameter-sized populations left generated (UX6.1); an authored `static` or `movable` value per object and light, changed only in the scene file and honored by the Inspector, the gizmo and the session bridge (UX6.2); a vendored transform gizmo in the editor's UI layer for objects and authored lights, with Unity's tool shortcuts, replacing MaterialLab's axis-station geometry (UX6.3).

**Exit gate (proposed):** the standing matrix and LightLab are compared with the parent as measured and MaterialLab is re-baselined once with the owner's acceptance; edits to former generated objects survive save and relaunch; a static subject's pose cannot change through any editor route; a gizmo drag exports the same bytes as the equivalent Inspector edit and the two track each other live; scene-only screenshots are identical with the gizmo shown.

**Defer:** create, duplicate, delete and reparent; viewport picking; Undo/Redo; snapping; material editing; changing mobility inside the editor; any renderer use of mobility.

## Candidate — offline pipeline editing

**Status:** unscheduled candidate from the owner's 2026-09-25 review; it has no identifier, step or
gate until the owner discusses it.

**Idea:** edit the rendering pipeline without a running renderer, then launch Luminex and see the
same graph live. Today `Renderer::declarePasses` composes the stages in code every frame, and the
Render Graph window is a read-only view of each frame's `CompiledFrameRecord`.

- **Stage-level pipeline document (the starting point).** A checked-in file lists catalog stages
  (shadow, scene, light clusters, HZB, exposure, bloom, temporal, display) with enabled state,
  parameters and connections, and the Renderer interprets it. Each stage publishes a static,
  GPU-free description of its ports and parameters, so an editor or a small tool can load,
  validate and lay out a pipeline without a device. Graph compilation runs on the CPU, so an
  offline dry run can show schedule, barriers and lifetimes; memory totals need a size model,
  since heap sizes come from the device. Online, the same canvas adds live timings and resources.
  Hard parts: cross-frame edges (exposure feedback, temporal ping-pong, previous HZB) become
  explicit previous-frame ports; CLI modes become preset documents; the document's hash joins
  capture manifests and measurement reports. Roughly three to four slices.
- **Pass-level wiring** (Falcor's Render Graph Editor and Mogwai). Bloom, light clustering and
  temporal resolve are multi-pass stages with private invariants that arbitrary wiring would
  break; several times the size. Not recommended.
- **Shader node authoring.** Out of scope.

**Open questions:** whether the purpose is toggling and rewiring existing stages or adding new
passes; placement relative to N1, whose learned passes need insertion points; whether offline
means the App without a scene or a separate tool.
