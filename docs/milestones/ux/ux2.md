# UX2 — Editor Surfaces

**Status**: Implemented — owner accepted on 2026-09-27; local integration pending

On 2026-09-27 the owner accepted the final running editor and authorized closure. The four
slices below are implemented: shared Codicons surfaces and menu-bar transport; scene-only
Hierarchy, Rendering and Inspector grids; compact Console, Performance and Graph controls;
and workspace schema 4 with migration. The executor plan is closed and removed.
[Validation](ux2-validation.md) records 8/8 exact scene-image pairs, passing unit/GPU suites,
native workspace migration and the limits of overall owner acceptance. Per-gesture evidence
not supplied remains unverified; no historical rendering gate is closed by this UI work.
The branch remains local under the owner's no-push instruction; no pull request has been opened.

From the owner's review of the running editor on 2026-09-25; its five open points were decided
with the owner the same day. UX2 gives every control one home where its task belongs, removes duplicate routes and chrome that is
always shown but rarely needed, and keeps every capability that is not redundant. It changes the
editor only: rendered images, capture and measurement outputs and the CLI stay as they are.
[Part IV](../../roadmap/editor-experience.md#ux2--editor-surfaces) owns the outcome and gates;
this record keeps the contract and limits.

**Placement:** R4 → **UX2** → [UX3](ux3.md) → N1. Before scene documents, so UX3 adds its
document workflow to a settled layout and its exact-image gates stay separate from layout changes.

## Observed state before UX2

Reviewed in Sponza and Damaged Helmet at the default layout on 2026-09-25.

- **Viewport:** two header rows above the image: the scene name, Reset camera, Camera help, GPU
  capture, Frame selected and a Selection outline checkbox. A debug view adds a `View:` line with
  Return to Final.
- **Toolbar:** a full-width row under the menu bar: a Scene/Measure combo, Play/Pause, Stop,
  Step, time, a status word (`Stopped`) and an options chevron holding Show measurement and Follow
  camera rail. `Scene` reads as scene selection. Measurement is configured in Performance, whose
  text tells the operator to choose Measure in the top toolbar and then press Play.
- **Menu bar:** File, Window, Layout and Debug, with −/percentage/+ UI zoom on the right.
- **Hierarchy:** a Workspace root (Editor Camera, eleven Rendering topics) beside scene content.
  Local-light rows carry checkboxes that indent their names differently from other rows. Clear
  search is always visible. The count reports more matches than its total (57 / 41 in Sponza)
  because the total omits local lights. A dimmed row means culled, explained only on hover.
- **Inspector:** every page opens with a reset row (Reset camera | Scene default, Reset light |
  Scene default, Reset transform | Authored pose, Reset Lighting | Default) whose grey label reads
  as a disabled button. Resolution uses a label | value grid; Lighting and Object stack each label
  above its value, so Lighting's three controls sit above about fourteen stacked readings, among
  them per-pass timings. Permanent footnotes explain behaviour.
- **Debug views:** the temporal view is in Reconstruction, the lighting view in Lighting and the
  HZB level in Occlusion, with conflict rules between them.
- **Console:** two rows of controls (Freeze display, Clear, Copy visible, Follow newest; a severity
  combo and search) and a statistics line that always prints zero eviction and truncation counts.
- **Performance:** a detached window, closed by default, with no docked summary. Freeze metrics,
  Clear history and a Schedule order checkbox; a Measure section with full-width inputs for two
  small integers; one table row per bloom sub-pass (eight rows).
- **Render Graph:** six header rows before the canvas: Freeze with live status, an explanatory
  line, Fit graph/Fit selection/100%/Reset layout, Dump frame with a Columns field, an empty
  `Ready:` line and a pooling/memory line whose high-water also appears in the details pane. The
  scene node shows about fifteen input pins, most of them imported read-only scene buffers.
- **Duplicate routes:** GPU capture three times (Viewport, Debug menu, `C`); UI zoom three times
  (menu bar, Layout > UI Scale, Cmd shortcuts); per-pass timings in the Inspector's Resolution and
  Lighting topics, Performance and the Render Graph.

## Decisions taken with the owner

| Topic | Decision |
|---|---|
| Identifier | Editor surfaces become UX2; scene documents and hierarchy are renumbered UX3. Records and ADRs dated before 2026-09-25 use UX2 for scene documents |
| Placement | A separate milestone before UX3, not folded into UX3's fourth slice |
| Scope | Editor only. No renderer, capture, manifest, measurement-schema or CLI change; an unchanged-image check replaces image gates |
| Principle | The viewport shows the image and overlays that describe it; the transport controls time; panels show controls first and readings second; status appears when it is abnormal |
| Performance | A compact docked tab exists beside the detached window, which it opens for detail |
| Camera | The View menu and shortcuts, with no viewport chrome: a corner popover would repeat two or three commands, and a right-click menu would contend with right-button look |
| Console reading | Scrolling off the bottom freezes the displayed view, because retained-log eviction otherwise slides lines under the reader; a ↓ N new chip, or returning to the bottom, resumes |
| Icons | Codicons, pinned by `xmake setup` like Inter; drawn on a 16 px grid, it holds the editor's debugger and view icons, and the transport moves to it |
| Menu bar | One row that never wraps. When it is tight it drops the time readout, into Play's tooltip, then the zoom percentage, which View > UI Scale keeps; buttons never drop |
| Debug View reach | The View menu, grouped by topic, and the legend chip, whose title switches views while one is active. Rendering topics keep no route; their tooltips name the menu |

## Surface conventions

UX2.1 records these in the [App architecture page](../../architecture/app.md) and applies them to
every panel:

1. **One home per function.** A command has one menu route, plus a shortcut or context-menu item
   where it is frequent. Panels do not repeat global commands.
2. **Header row.** A panel's actions sit in one row: frequent ones as icon buttons with tooltips,
   rare ones in a ⋯ overflow menu. An Inspector page's header holds the subject name, its kind and
   a ↺ reset whose tooltip names what it restores.
3. **Property grid.** Every Inspector page uses one label | value grid, reflowing to one column
   only below a stated width.
4. **Controls, readings, diagnostics.** Readings an operator acts on stay visible; identifiers,
   capacities, bounds and frame numbers sit in a collapsed Diagnostics section. Timings belong to
   Performance; other surfaces link there instead of repeating them.
5. **Status by exception.** Normal states (`Stopped`, `Ready`, zero eviction) are not printed;
   warnings, failures and work in progress are. Explanations move into tooltips.
6. **The viewport is the image.** Only overlays that describe the pixels, such as the debug-view
   legend chip with its view dropdown and × and a pending capture, draw on it.

## Placement map

| Today | UX2 |
|---|---|
| Viewport scene name | Main window title |
| Reset camera, Frame selected | View menu, with Home and F; Frame selected also in the Hierarchy context menu |
| Camera help | Help > Controls |
| Viewport GPU capture | Removed; Debug > Capture Next GPU Frame and `C` remain, and results appear in a transient notice |
| Selection outline checkbox | View > Selection Outline |
| Temporal, lighting and HZB views | One View > Debug View selector grouped by topic; unavailable views are disabled with the reason. The viewport's legend chip gains a view dropdown in its title and, for HZB, the mip-level stepper. CLI options are unchanged |
| Toolbar row | The transport joins the menu-bar row: Play/Pause, Stop, Step, time, and a rail-follow toggle shown only for scenes with a rail |
| Scene/Measure combo, options chevron | Removed. A run starts from Performance; during it the transport shows progress and only Stop is enabled |
| Layout menu, menu-bar zoom | Layout merges into Window; View > UI Scale and the Cmd shortcuts remain; the menu bar keeps the percentage, which resets on click |
| Workspace > Rendering topics | A dockable Rendering panel with each topic's controls, readings and scoped reset |
| Workspace > Editor Camera | Leaves the Hierarchy; View > Editor Camera shows it in the Inspector |
| Local-light row checkboxes | The Inspector header checkbox; UX3 extends it to objects |
| Clear search button | An × inside the search field |
| Console controls | One row: search, severity chips with counts, ⋯ for Clear and Copy visible. Following is automatic at the bottom; scrolling up freezes the view and a ↓ N new chip resumes, so Freeze and Follow newest are removed. Statistics print when eviction or truncation is non-zero and otherwise sit in a tooltip |
| Performance | A compact docked tab: frame and GPU time with a sparkline, the costliest stages, freshness and Details ↗. The detached window gains Live and Measure tabs, stage-grouped rows and a sortable schedule column in place of the Schedule order checkbox |
| Render Graph header | One row: Freeze with live status, fit and 100% icons, ⋯ for Reset layout, Columns and Dump. Dump results appear in a transient notice; memory totals only in the details pane. Imported scene buffers bundle into one expandable pin per node |

## UX2.1 — Conventions, menus, viewport and transport

**Deliver:** the conventions above in the architecture page; shared primitives in `EditorStyle`
(header row, icon button, overflow menu, property grid, Diagnostics section, transient notice);
Codicons in `xmake setup` (pinned version, SHA-256, CC BY 4.0 license and `SOURCE.txt` in
`ThirdParty/Codicons`), staged to `Fonts/` beside Inter, merged into the atlas, named by code-point
constants in one header and credited in `THIRD_PARTY_NOTICES.md`; File, View, Window, Debug and
Help menus; the viewport without a header; one Debug View selector in AppModel whose availability
reproduces the existing conflict rules, with the chip dropdown and HZB stepper; the transport in
the menu-bar row, placed by a pure AppModel fit function; measurement started from Performance;
the scene name in the window title.

**Exit gate:** every command removed from a surface stays reachable by its named route; AppModel
tests cover Debug View availability case by case against the CLI conflict rules, and the fit
function's drop order at stated widths; without the icon font, every icon button shows its text
label and one warning is logged; capture and dump still report their path or reason.

## UX2.2 — Hierarchy, Inspector and Rendering panel

**Deliver:** a scene-only Hierarchy; the Rendering panel; the property grid, header reset and
Diagnostics section on every Inspector page; the local-light enable in the Inspector header; a
Hierarchy count whose total equals the selectable rows, with a test.

**Exit gate:** the completion gate's search/select/edit/restore task and every scoped reset behave
as before; no Inspector page stacks labels at the default layout width.

## UX2.3 — Console, Performance and Render Graph chrome

**Deliver:** the Console, Performance and Render Graph rows of the placement map, with the
Console's freeze-on-scroll transitions owned by `ConsoleModel`.

**Exit gate:** the completion gate's performance and graph tasks pass: find the costliest pass,
freeze, clear and resume; hold graph selection and navigation for ten seconds under Native TAA,
freeze, dump and resume. The compact tab and the detached window show the same snapshot; Console
filtering, copying and clearing are unchanged. `ConsoleModel` tests cover freezing on scroll-up,
counting new arrivals, resuming by the chip and by the bottom, Clear while frozen, and Copy visible
copying the frozen view.

## UX2.4 — Workspace schema 4 and acceptance

**Deliver:** workspace schema 4 with the Rendering panel and the Performance tab in the default
docking, migrating schema 3 with its UI scale and window bounds; Reset Default Layout; the
architecture and guide pages and `AGENTS.md`, including the Codicons setup line.

**Exit gate:** the whole [completion gate](../../roadmap/editor-experience.md#completion-gate) on
the new layout at both window sizes; schema 3 workspaces open without loss; scene-only
`--screenshot` outputs for the reference set are byte-identical to the parent; unverified
gestures are recorded as unverified.

## Boundaries and deferrals

No renderer, capture, manifest, measurement or CLI change. Scene documents, object enabled state
and Open/Save belong to [UX3](ux3.md). Viewport picking, gizmos, Undo/Redo, layout presets and
theme editing remain deferred as [UX1](ux1.md) left them. Editing the rendering pipeline is a
separate [candidate](../../roadmap/editor-experience.md#candidate--offline-pipeline-editing), not
part of UX2.

## Risks

- **Attribution.** Codicons' CC BY 4.0 license must travel with the staged font and its notice.
- **Frozen Console.** The ↓ N new chip is the only sign that the view is frozen; it stays visible
  whenever the view is scrolled up.
- **Narrow windows.** Below 1280 pt the menu bar relies on dropping readouts; narrower widths are
  otherwise unverified.
