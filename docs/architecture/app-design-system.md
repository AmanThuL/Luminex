# App design system

**Status**: In progress

This companion to [App](app.md#editor-shell-and-windows) describes the editor's colors, metrics,
appearance, menus and attribution. [ADR 0029](../decisions/0029-design-system-token-contract.md)
remains Proposed; the [milestone](../milestones/ux/ux4.md) and its plan remain In progress.
The [validation record](../milestones/ux/ux4-validation.md) retains failed image/cost gates and
unverified native checks. Source behavior below does not establish those checks as passed.

## Color ownership

[Tools/Theme/generate_tokens.py](../../Tools/Theme/generate_tokens.py) is the single color generator.
It emits [EditorThemeTokens.h](../../Source/App/Model/Workspace/EditorThemeTokens.h),
[EditorThemeTokens.cpp](../../Source/App/Model/Workspace/EditorThemeTokens.cpp) and
[figma-variables.json](../../Tools/Theme/figma-variables.json). App loads no runtime theme JSON.
`ThemeRole` indexes semantic colors for surfaces, text, borders, operator/agent accents, status,
actors, provenance, selection, graph kinds/links, plots, overlays and Console severity.
`ThemeColor` stores encoded sRGB RGB as byte / 255.0f and straight alpha.

`SlotRole` maps all 63 pinned ImGui slots and all 19 node-editor slots in enum order, multiplying
only alpha by `alphaScale`. [EditorThemeApply](../../Source/App/Shell/EditorThemeApply.cpp) asserts
both slot counts at compile time and verifies each upstream slot name: ImGui at shell startup, the node editor when the Render Graph canvas first draws. Shell colors
come from these tables; the graph applies the same active palette when beginning its canvas.
`editor_style::color` and `editor_style::colorU32` supply custom drawing. The base `Header` slot is selection;
`collapsingHeader` pushes neutral hover/active surfaces so topic headers do not become selections.

The generator audits 24 required contrast pairs per theme. Selection composites encoded channels
over the panel before WCAG luminance decoding: Dark 10.326799:1 and Light 13.836046:1.
These pair checks cover the listed token combinations, including explicit disabled-text and
island/hairline floors; they do not certify all text over arbitrary rendered images.
`overlay/text` is checked over the fixed viewport surround. Shader-derived debug swatches remain
image data. The renderer's scene clear color and display transform stay outside the theme.
[SelectionOutline.slang](../../Shaders/Passes/SelectionOutline/SelectionOutline.slang) uses one
encoded `#4CABFD` outline in both themes.

Check generated colors without writing:

```bash
python3 Tools/Theme/generate_tokens.py --check
python3 Tools/Theme/generate_tokens.py --audit
```

CI runs the freshness check. The root policy task runs
[check_literal_colors.py](../../Tools/check_literal_colors.py) for panel/shell literal colors and
centralized collapsing headers; neither check generates shape, density or type metrics.

## Appearance and persistence

[EditorTheme](../../Source/App/Model/Workspace/EditorTheme.h) owns `Appearance`, `SystemTheme`,
`ThemeKind`, `AppearanceState` and `ThemeTransition`. Auto resolves SDL Light to Light and both
Dark and Unknown to Dark. Forced modes ignore system changes. The shell reads the system at
startup and handles `SDL_EVENT_SYSTEM_THEME_CHANGED` live.

`prepareUIFrame` applies palette changes to both the unscaled base style and current style before
`NewFrame`. Its 160 ms encoded-sRGB transition restarts from the sampled current palette; Reduce
Motion snaps to the target. A scale or density edit reapplies metrics without resetting the fade,
which still ends at the exact target. Color changes do not reload fonts.

[AppAppearance.mm](../../Source/App/Shell/AppAppearance.mm) applies Aqua, DarkAqua or nil to every
ImGui viewport's native window after `UpdatePlatformWindows`, including newly opened detached
windows. Auto clears the forced appearance. `NSApp.appearance` stays untouched so SDL can observe
the system. [NativeMenu](../../Source/App/Shell/NativeMenu.mm) applies the same forced value to
`NSApp.mainMenu`; the OS controls the menu-bar strip itself.

[WorkspaceModel](../../Source/App/Model/Workspace/WorkspaceModel.h) stores schema 5 in the
build-local `imgui.ini`: eight panel visibilities, UI scale, `Appearance` and `Density`, alongside
ImGui's docking and viewport bounds. Lowercase storage names are `auto|light|dark` and
`comfortable|compact`; missing or unknown values keep Auto and Comfortable. Schema 4 restores
layout, visibilities and scale without rebuilding and uses those new defaults. Schema 3 keeps its
six visibilities, valid scale and detached bounds, enables Rendering/Performance summary and
rebuilds main docks once. Schema 2 keeps valid scale and rebuilds defaults; unknown schemas reset
preferences. Reset Default Layout preserves scale, appearance and density.

[AppOptions](../../Source/App/Model/Options/AppOptions.cpp) accepts `--appearance auto|light|dark`
only for the windowed editor, rejecting screenshot, capture-sequence and measurement modes.
`AppearanceState::override` is session-only: persistence writes `persisted`, and a View >
Appearance choice calls `choose`, saving the choice and clearing the override.

## Type, shape and density

These contracts live in hand-written [EditorTheme](../../Source/App/Model/Workspace/EditorTheme.h)
and shared UI helpers. Generated tables contain colors only. Both themes and densities share
`typeSpec` and `kShape`; sizes and spacing do not animate.

| Type role | Face | Logical points at 100% |
|---|---|---|
| Caption | Geist Sans Regular | 13 |
| Body | Geist Sans Regular | 16 |
| BodyStrong | Geist Sans Medium | 16 |
| Display | Geist Sans Medium | 20 |
| MonoCaption | Geist Mono Regular | 13 |
| MonoBody | Geist Mono Regular | 16 |

[setup.lua](../../xmake/setup.lua) pins Geist 1.7.2's archive, three static faces and SIL Open Font
License 1.1. [App staging](../../Source/App/xmake.lua) copies faces, license and provenance into
`Fonts/`. [EditorFont](../../Source/App/Shell/EditorFont.cpp) forces Sans/Medium digits to 0.6 em,
9.6 at body 16. Mono retains native advances; the retained font probe observed 7.384615 at 16,
so cross-face equal advance is not a contract. Missing Medium/Mono falls back to Regular with one
warning per face; missing Regular uses the embedded font. Codicons uses a 16-point grid and
labeled buttons plus one warning when missing.

`ScopedType` selects the semantic face/size. Medium serves subject/topic headers, dialog actions
and selected editor-drawn tabs; ImGui dock tabs remain Regular. Mono serves timestamps,
Performance tables, graph costs, identifiers, hashes, paths and legend ranges. Body 16 is retained
from actual 13/16/20 Gallery captures at 2×; 1× legibility remains unverified. The
[validation record](../milestones/ux/ux4-validation.md#task-12-cost-gate-failure) retains that scope.

`kShape` currently uses control radius 0, popup 4, card 6, pill 10, border 1 and dock gutter 2;
windows and children remain square. Shell scaling restores 1-pixel style borders after
`ScaleAllSizes`. The radius reduction did not pass the full historical cost gate: Compact stayed
above 1.15×. Current metrics do not establish a new cost pass.

| Density | Frame padding | Item spacing | Window padding |
|---|---|---|---|
| Comfortable | 8 × 5 | 8 × 8 | 12 |
| Compact | 6 × 3 | 6 × 4 | 8 |

UI scale applies once from an unscaled base. `kUiScalePresets` lists
75/80/90/100/110/125/150%; persisted integer percentages within 75–150 remain valid.

## Attribution and Gallery

[Provenance](../../Source/App/Model/Workspace/Provenance.h) owns copied source strings. Clean
authored state has no mark; dirty documents and ordinary edits show the operator dot. Generated
and CLI-masked subjects stay session-only even when edited: tooltips retain generator, CLI flag
and edit attribution. Active dynamic resolution shows a system-applied gear naming scale/budget.
The existing title `*`, generated "not saved" text and diagnostic status remain visible.

[ActivityModel](../../Source/App/Model/Workspace/ActivityModel.cpp) chooses measurement before
pending capture, document work, then a controller change less than two seconds old. Measurement
and requested capture are operator activities; document work and controller changes are system
activities. Only measurement exposes Stop. The toolbar drops the activity verb before time and
zoom when space is tight, keeping the mark and transport actions.

[StyleGalleryPanel](../../Source/App/Panels/Gallery/StyleGalleryPanel.cpp) is a detached, opaque,
nonpersistent, titled and closable native window, closed at launch. Its Current/Dark/Light preview scopes all 63 ImGui colors
and the shared drawing palette to its content, then restores the parent. The
[21-entry catalog](../../Source/App/Model/Workspace/GalleryCatalog.cpp) covers control states,
provenance/activity and reserved agent lifecycle, proposal and attention specimens, alongside the type ramp.
Agent marks and proposal controls have Gallery consumers only; no runtime agent, proposal ingestion,
session log or editor command bridge exists.

## Menus and frame boundaries

[MenuModel](../../Source/App/Model/Workspace/MenuModel.h) supplies command arguments, shortcuts,
checked/enabled state and disabled reasons from a snapshot. `EditorShell::runMenuCommand` in [EditorMenus](../../Source/App/Shell/EditorMenus.cpp) routes
both renderers to existing operations. On macOS, `NativeMenuBar` replaces SDL's menu with Luminex,
File, Edit, View, Window, Debug and Help. It adds About/Hide/Quit, text-edit actions and the standard window commands (Minimize, Zoom, Enter Full Screen, Close); other
platforms keep the ImGui menu renderer. The in-window toolbar holds transport, activity and zoom.
Submenus rebuild on open and disabled items carry reason tooltips.

Native keyboard intents bind to owned ImGui input event IDs after SDL's event batch. After all
panels resolve widget ownership, `takeCommands` rechecks current availability and shortcut policy;
macOS shell polling of those chords is disabled. Edit Cut/Copy/Paste/Select All posts Command
chords to a focused ImGui text field; otherwise those actions are disabled. These mechanisms are
source contracts; [native validation](../milestones/ux/ux4-native-menu-validation.md) retains the
unverified gestures.

[main.cpp](../../Source/App/Shell/main.cpp) prepares style, starts SDL/Metal ImGui frames, builds
all UI and closes it with `Render` before acquiring a drawable. Skips still update platform windows
and appearance, with no unpaced platform rendering. Presented frames render detached windows
after main `endFrame`. Capture intent remains pending until a drawable is acquired.

## Editor surface conventions

The [UX2 placement map](../milestones/ux/ux2.md#placement-map) fixes each control's destination.
Shared primitives in `Panels/Shared/EditorStyle` implement these conventions:

1. **One home per function.** Commands have one menu route, plus a shortcut or frequent context
   action. Panels do not repeat global commands.
2. **Header row.** Frequent panel actions use icon buttons with tooltips; rare actions use More.
   Inspector headers name the subject and kind; Reset, enabled only after a change, names its scope.
3. **Property grid.** Inspector pages use label | value grids; they reflow to one column below
   `kPropertyGridMinWidth` (260 base UI points, adjusted by UI scale).
4. **Controls, readings, diagnostics.** Actionable readings stay visible. Identifiers, capacities,
   bounds and frame numbers use a collapsed Diagnostics section. Timings belong to Performance;
   other surfaces link there.
5. **Status by exception.** Normal states such as Stopped, Ready and zero evictions stay quiet;
   warnings, failures and pending work remain visible. Explanations use tooltips.
6. **The viewport is the image.** Only pixel-related overlays belong there, including the Debug
   View legend chip with its selector and Close action, and pending capture.

`iconButton` uses square Codicons buttons and readable text labels when the font is unavailable. `nextInRow` shares
width-aware wrapping; `beginHeaderRow` groups panel actions, `overflowMenu` opens their popup, `beginPropertyGrid`
shares the reflow threshold, and `beginDiagnostics` starts collapsed. `drawNotice` displays the retained result and Copy
path/Reveal actions in a dismissible, borderless window at the main viewport's bottom-right work area. `ActionFeedback`
suppresses Ready. File > Open Scene owns catalog availability, loading and retry. Source names are disambiguated within
each scene; filtering keeps selection. Hierarchy follows the document and imported source-node tree, with Environment and generated children. View > Editor
Camera selects the camera in Inspector; View also owns Reset Camera (Home), Frame Selected (F), Selection Outline, Debug
View and UI Scale. Frame Selected is also a Hierarchy context action. `EditorShortcuts` suppresses F, Home and C during text
entry, popups, RMB look or Render Graph/Performance focus; C without capture explains why. Help > Controls explains movement. `EditorMenus` routes shared menu commands; `EditorTransport` owns the toolbar
with Play/Pause, Stop, Step, time and rail follow. The window title shows `*` and the Hierarchy root shows the operator dot
when dirty. Stop restores the preview without adding document edits. View > Set Scene Camera from View
is the explicit way to save the editor camera.
`RenderingPanel`, `RenderingTopics` and `RenderingLighting` draw eight collapsing topics, with Reconstruction initially
open. Topic headers have scoped resets that keep the Debug View; controls precede readings and Diagnostics, and Details opens Performance.
Inspector pages share subject/kind/reset headers. Enabled controls on groups, objects and lights preserve
identities and descendant own flags; Measure locks edits. Exposure, Bloom and Shadows live under Environment.

App alone declares `Render/Passes/SelectionOutline/SelectionOutline`, opting into it after scene
display; see [render-passes.md#selection-outline](render-passes.md#selection-outline) for the pass
itself.

`ConsoleLogSink` subscribes after logger setup and before option parsing or device initialization, and its RAII
subscription lasts through application shutdown; its callbacks only append to the thread-safe Console store. Console
displays UTC timestamps and six severity levels, with minimum-severity chips and case-insensitive search. Scroll-up
holds the displayed rows while logging continues; returning to the bottom or clicking `↓ N new` resumes. The chip
remains visible at zero arrivals. More owns Clear and Copy visible: Clear preserves filters/freeze, and Copy exports the
matching held view. Loss counts print only when nonzero and otherwise remain in search help. Console is read-only.
Header counts can trail the messages by one frame because scroll state is resolved before refreshing the displayed
snapshot. See [gpu-debugging.md](../guides/gpu-debugging.md#console-and-editor-selection-diagnostics) for the operator
workflow.
