# Editor workspace

**Status**: In progress

Use [GPU Debugging](gpu-debugging.md) for frame capture, dumps and timings. This companion covers
editor appearance, density, fonts, native menus, playback and workspace recovery. Source behavior
is described here; the [validation record](../milestones/ux/ux4-validation.md) retains failed image
and cost gates and unverified native checks.

## Appearance and density

View > Appearance offers Auto (system), Light and Dark. Auto follows macOS changes live and uses
Dark if SDL cannot determine the system theme. Forced Light/Dark ignore system theme changes.
Colors crossfade over 160 ms; macOS Reduce Motion snaps to the target. A second choice during a
fade starts from the current palette. UI scale and density changes retain the selected appearance.
The viewport surround stays dark in both themes; selection uses the same encoded `#4CABFD` outline.
Scene clear color, rendering settings and scene-only captures are outside the theme.

Forced Light uses Aqua and forced Dark uses DarkAqua for the main and detached native windows and
`NSApp.mainMenu`. Auto clears these overrides. Newly opened Render Graph, Performance and Style
Gallery windows receive the current appearance. The macOS menu-bar strip itself is controlled by
the OS; forcing the editor's theme does not force the strip's color.

For a temporary editor launch:

```bash
xmake run -P . App --appearance light
```

`--appearance auto|light|dark` is windowed-only and rejects `--screenshot`, `--capture-sequence`
and `--measure`. It does not change the saved preference. Choosing an appearance from View clears
the override and saves that choice. To resume system following, choose Auto (system).

View > Density chooses Comfortable or Compact without changing fonts or UI scale:

| Density | Frame padding | Item spacing | Window padding |
|---|---|---|---|
| Comfortable | 8 × 5 | 8 × 8 | 12 |
| Compact | 6 × 3 | 6 × 4 | 8 |

Values are unscaled logical points. The current control radius is 0; popup/card/pill radii remain
4/6/10 with square docked surfaces. The historical Compact index-count gate remains failed;
[the retained evidence](../milestones/ux/ux4-validation.md#task-12-cost-gate-failure) does not support
a claim that both densities meet the measured budget.

## Native menus and activity

macOS has Luminex, File, Edit, View, Window, Debug and Help menus. Luminex owns About, Hide and
Quit; File owns scene/document actions; View owns framing, debug views, UI scale, appearance and
density; Window opens panels, resets layout and carries Minimize, Zoom, Enter Full Screen and Close; Debug requests capture; Help explains controls.
Edit's Cut, Copy, Paste and Select All act on a focused ImGui text field and are disabled with a
reason otherwise. Cmd+Q quits from any focus through the unsaved-changes confirmation, and a Save chord
refused while playing or measuring reports its reason. Menus and keyboard commands share action state and disabled reasons. Other
platforms retain the ImGui menus. Native gesture coverage remains
[unverified where recorded](../milestones/ux/ux4-native-menu-validation.md).

The in-window toolbar holds transport, activity and zoom. Measurement and requested capture use
operator marks; document work and recent dynamic-resolution changes use system marks. Hover a
mark for its source. Measurement has Stop; capture and document work do not gain cancellation.
When width is tight, the activity verb hides first, then time moves to Play's tooltip, then zoom
hides. Actions and the activity mark remain.

Authored subjects have no provenance mark. Dirty documents/edits show an operator dot; generated
and CLI-masked state shows a session-only dashed underline with the generator/flag in its tooltip.
Active dynamic resolution shows a system-applied gear naming scale and budget. The window title
keeps `*` and generated subjects keep "not saved". Agent lifecycle, proposals and attention rings
are Style Gallery specimens only; no runtime agent session or command bridge exists.

## Editor playback

The toolbar holds Play/Pause, Stop, Step, time and a rail-follow toggle when the scene has a rail. Scenes load Stopped. First Play captures camera/time, animation-owned object poses/emissive strength
and tracked light positions; Pause holds time, and Step advances 1/60 s and pauses. Stop or scene switch restores that preview and resets motion, temporal and exposure history. Rendering settings and
unrelated edits remain outside restoration. Static scenes still allow camera preview. View > Reset Camera (Home) and Frame Selected (F) control framing; the latter is also in Hierarchy's context menu.
View > Selection Outline toggles the outline, and Help > Controls explains movement. F, Home and C are suppressed during text entry, popups and RMB look. The window title names the scene. Start a
fixed run in Performance > Measure; transport then shows progress and enables only Stop. Playback, metric freeze and graph freeze are independent. See [Measure](gpu-debugging.md#measure-visibility-and-submission).

## Restore editor settings

Inspector Reset actions affect the named group. Camera Reset restores the scene's initial pose/lens and stops camera-rail following. Directional Light Reset restores authored direction and scene-linear radiance. Local-light Reset restores authored enablement/color/intensity/range/cones and the current-time orbit position; Stop restores only its captured position, preserving other light edits. Object Reset restores its authored transform or samples that object's animated transform
at the current playback time, preserving other object edits. Pause scene to retain a manual edit to an animated transform; playback replaces it on the next track sample.

Rendering groups restore these editor defaults without resetting playback or another group. Environment Reset restores Exposure, Bloom and Shadows from the loaded or saved document:

| Group | Defaults |
|---|---|
| Lighting | Clustered local lights; lighting diagnostic off; CPU list check off; LightLab overflow pile cleared; individual light state unchanged |
| Environment: Exposure | The document's manual/auto EV and metering settings |
| Environment: Bloom | The document's enabled state, threshold and intensity |
| Environment: Shadows | The document's shadow filter |
| Reconstruction | Temporal inputs and jitter enabled; Native TAA; temporal diagnostic off |
| Resolution | Scale 1; dynamic resolution off; timed-pass budget 16 ms |
| Display | Encoded sRGB clear RGBA (0.7, 0.7, 0.7, 1); wireframe off; transient pooling on |

## Workspace recovery

Workspace schema 5 persists appearance, density, eight panel visibilities and UI scale in
build-local `imgui.ini`, alongside docking and viewport bounds. Schema 4 restores without a dock
rebuild using Auto and Comfortable. Schema 3 keeps six visibilities, valid scale and detached
bounds, adds Rendering and the Performance summary and rebuilds main docks once. Schema 2 keeps
valid scale and rebuilds defaults; unknown schemas use default preferences/layout.

Window > Reset Default Layout preserves scale, appearance and density, closes detached Performance
and Render Graph and resets Performance's next-open bounds. Ordinary launches retain both windows'
geometry. Style Gallery starts closed, is not persisted and can be opened with Window > Style
Gallery. Its Current/Dark/Light selector previews content only; it does not save a workspace theme.
The Gallery shows the type ramp and 21 component sets, including reserved proposal/attention states.
Native Gallery captures and Figma correspondence remain limited as recorded in
[Gallery validation](../milestones/ux/ux4-gallery-validation.md).

## Editor UI scale

View > UI Scale offers 75/80/90/100/110/125/150%. Click the toolbar percentage or press Cmd+0 for
100%; Cmd+- shrinks and Cmd++ / Cmd+= grows. Text editing, active drags, popups and RMB look
suppress zoom shortcuts. Detached windows share UI scale; graph zoom is independent. Saved integer
percentages in 75–150 are valid. Missing/invalid scale is 100%; migration and Reset Default Layout
preserve valid scale.

At 100%, Geist Sans Regular body text is 16 logical points; captions are 13, strong text uses
Medium 16 and dialog titles use Medium 20. Geist Mono 13/16 serves data such as timestamps, costs,
IDs, hashes and paths. Sans/Medium digits have a fixed 0.6 em advance (9.6 at 16); Mono keeps native
spacing, so equal advance across faces is not promised. Dock tabs use Regular. Body 16 was retained
from actual 13/16/20 Gallery captures at 2×; 1× legibility remains unverified.

`xmake setup -P .` fetches hash-pinned Geist 1.7.2 and its SIL Open Font License 1.1, plus Codicons
0.0.46-24 and CC BY 4.0 license/provenance. Building App stages them in `Fonts/` beside the binary.
Missing Medium or Mono falls back to Regular with one warning per face; missing Regular uses the
embedded fallback. Missing Codicons uses labeled buttons with one warning. Rerun setup and rebuild
to restore missing resources. See the [App type contract](../architecture/app-design-system.md#type-shape-and-density)
for the source owners.
