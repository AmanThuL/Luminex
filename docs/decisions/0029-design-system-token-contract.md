# ADR 0029: Design-system token contract

**Status**: Proposed
**Date**: 2026-10-01

## Context

The editor needs matching Light/Dark colors across ImGui, the Render Graph and custom panel
primitives. Its pure models must stay free of UI and platform types, and theme changes must
preserve scene/display semantics. The [UX4 record](../milestones/ux/ux4.md) defines the settled
design; the [editor roadmap](../roadmap/editor-experience.md#ux4--design-system-and-themes) owns
its delivery gate. This ADR records the token contract for review. It does not record its own
acceptance.

## Proposed decision

### One color generator

Use [Tools/Theme/generate_tokens.py](../../Tools/Theme/generate_tokens.py) as the single source of
color primitives, semantic palettes, slot mappings and required contrast pairs. It converts OKLCH
ramps to gamut-clamped sRGB, quantizes semantic RGB to bytes and emits:

- [EditorThemeTokens.h](../../Source/App/Model/Workspace/EditorThemeTokens.h): `ThemeRole`,
  `ThemeColor`, `ThemePalette`, `SlotRole` and `ContrastPair` contracts.
- [EditorThemeTokens.cpp](../../Source/App/Model/Workspace/EditorThemeTokens.cpp): both palettes,
  63 ImGui mappings, 19 node-editor mappings and 24 contrast pairs per theme.
- [figma-variables.json](../../Tools/Theme/figma-variables.json): primitive and semantic color data.

No runtime JSON is loaded. `--check` is read-only and fails naming stale outputs; CI runs it.
`--audit` prints both contrast tables. Change the generator and regenerate together; do not hand
edit its outputs. Shape, density and type remain shared C++ constants/model helpers in
[EditorTheme](../../Source/App/Model/Workspace/EditorTheme.h) and UI helpers, outside color generation.

### Roles and encoding

Components consume `ThemeRole`, covering `surface/*`, `text/*`, `border/*`, operator and agent
accents, `status/*`, `actor/*`, `prov/session`, selection, graph kinds/links, plots, overlays and
Console severity. RGB channels are encoded sRGB byte / 255.0f; alpha is straight coverage.
App's SDR ImGui target blends encoded channels. For selection contrast, composite `selection/bg`
over `surface/panel` in encoded sRGB first, then decode for WCAG luminance. The required selection
ratios are Dark 10.326799:1 and Light 13.836046:1 against a 4.5 floor.

The audit covers 48 specified pairs, with explicit disabled-text, island and hairline floors.
This is a token-pair contract, not a whole-screen accessibility certification. Overlay labels
are audited against the fixed viewport surround, not arbitrary scene pixels. AppModel contrast
tests use an independent WCAG formula. Scene clear color and display encoding remain outside
this contract under [ADR 0019](0019-display-domains-and-edr.md). The editor outline uses one encoded
`#4CABFD` constant in both themes; it does not follow the Light operator accent.

### Complete slot mapping

`SlotRole` carries the upstream name, role and alpha multiplier. Arrays retain pinned enum order;
apply palette RGB and `palette.alpha × alphaScale` to every slot. The following groups cover all
63 ImGui slots. Names omit the `ImGuiCol_` prefix; the generated array owns ordering.

| Slots | Semantic role | Alpha multiplier |
|---|---|---|
| `Text`, `InputTextCursor` | `text/primary` | 1 |
| `TextDisabled`, `ScrollbarGrabHovered` | `text/disabled` | 1 |
| `WindowBg`, `TitleBgActive`, `MenuBarBg`, `TabSelected`, `TabDimmedSelected` | `surface/panel` | 1 |
| `ChildBg`, `TableRowBg` | `surface/panel` | 0 |
| `PopupBg` | `surface/raised` | 1 |
| `Border`, `Separator`, `TableBorderLight`, `TreeLines` | `border/subtle` | 1 |
| `BorderShadow` | `border/subtle` | 0 |
| `FrameBg` | `surface/sunken` | 1 |
| `FrameBgHovered`, `Button`, `HeaderHovered`, `TabHovered` | `surface/hover` | 1 |
| `FrameBgActive`, `ButtonHovered`, `HeaderActive` | `surface/active` | 1 |
| `TitleBg`, `TitleBgCollapsed`, `ScrollbarBg`, `Tab`, `TabDimmed`, `DockingEmptyBg`, `TableHeaderBg` | `surface/canvas` | 1 |
| `ScrollbarGrab`, `SeparatorHovered`, `ResizeGrip`, `TabDimmedSelectedOverline`, `TableBorderStrong` | `border/strong` | 1 |
| `ScrollbarGrabActive` | `text/secondary` | 1 |
| `CheckMark`, `SliderGrab`, `SeparatorActive`, `ResizeGripActive`, `TabSelectedOverline`, `PlotLines`, `PlotHistogram`, `DragDropTarget`, `NavCursor`, `NavWindowingHighlight` | `accent/operator` | 1 |
| `CheckboxSelectedBg`, `ButtonActive`, `DockingPreview`, `DragDropTargetBg` | `accent/operator-subtle` | 1 |
| `SliderGrabActive` | `accent/operator-active` | 1 |
| `Header`, `TextSelectedBg` | `selection/bg` | 1 |
| `ResizeGripHovered`, `PlotLinesHovered`, `PlotHistogramHovered` | `accent/operator-hover` | 1 |
| `TableRowBgAlt` | `surface/hover` | 0.35 |
| `TextLink` | `accent/operator-text` | 1 |
| `UnsavedMarker` | `actor/operator` | 1 |
| `NavWindowingDimBg`, `ModalWindowDimBg` | `surface/canvas` | 0.6 |

The following groups cover all 19 node-editor `StyleColor` slots.

| Slots | Semantic role | Alpha multiplier |
|---|---|---|
| `Bg` | `surface/canvas` | 1 |
| `Grid`, `NodeBorder`, `GroupBorder` | `border/subtle` | 1 |
| `NodeBg` | `surface/raised` | 1 |
| `HoveredNodeBorder`, `HoveredLinkBorder` | `accent/operator-hover` | 1 |
| `SelNodeBorder`, `NodeSelRectBorder`, `SelLinkBorder`, `LinkSelRectBorder`, `PinRectBorder`, `Flow`, `FlowMarker` | `accent/operator` | 1 |
| `NodeSelRect`, `LinkSelRect`, `PinRect` | `accent/operator-subtle` | 1 |
| `HighlightLinkBorder` | `accent/operator-active` | 1 |
| `GroupBg` | `surface/panel` | 0.5 |

[EditorThemeApply](../../Source/App/Shell/EditorThemeApply.cpp) statically asserts both counts
and verifies every upstream `GetStyleColorName`: the ImGui table at shell startup and the node-editor table when the Render Graph canvas first draws. An upstream slot change requires
an explicit generator mapping. `Header` is the selection background; the shared neutral
`collapsingHeader` helper overrides its three slots for disclosure topics. Custom panel drawing
uses `editor_style::color`/`editor_style::colorU32`; the literal-color checker guards shell/panel adoption.
Shader-derived debug-view swatches remain image data.

### Appearance and native chrome

`Appearance { Auto, Light, Dark }` resolves against `SystemTheme`; Auto maps Unknown to Dark.
`AppearanceState` separates saved preference from an optional session override. View choices
save the preference and clear the override. `--appearance auto|light|dark` is windowed-only and
rejects screenshot, sequence and measurement modes.

The shell applies palette changes to base/current styles before `NewFrame`. `ThemeTransition`
interpolates encoded channels for 160 ms, restarts from its current sample and returns the exact
target at the deadline; Reduce Motion snaps. Scale/density reapplication retains the palette.
Theme changes do not reload fonts. The graph and custom drawing consume the same active palette.

The shell observes SDL system theme changes; [AppAppearance](../../Source/App/Shell/AppAppearance.mm)
queries system/accessibility state and sets Aqua/DarkAqua/nil on all main/detached viewport windows after `UpdatePlatformWindows`.
[NativeMenu](../../Source/App/Shell/NativeMenu.mm) sets the same value on `NSApp.mainMenu`.
`NSApp.appearance` stays untouched, preserving system observation. The OS controls the menu-bar
strip. Native Luminex/File/Edit/View/Window/Debug/Help menus share the platform-neutral command
model; the toolbar holds transport, activity and zoom. ImGui menus remain on other platforms.

### Workspace schema 5

[WorkspaceModel](../../Source/App/Model/Workspace/WorkspaceModel.h) persists eight visibilities,
UI scale, `Appearance` and `Density` alongside ImGui docking and viewport bounds. Storage uses
`auto|light|dark` and `comfortable|compact`; missing/unknown names keep Auto and Comfortable.
Schema 4 restores layout, visibilities and scale without rebuilding, with those new defaults.
Schema 3 keeps six visibilities, scale and detached bounds, enables the two new docked panels and
rebuilds main docks once; schema 2 keeps valid scale and rebuilds defaults. Unknown schemas reset
preferences. Reset Default Layout preserves scale, appearance and density. CLI appearance is
never written as the saved preference. Style Gallery visibility is not persisted.

## Consequences and limits

AppModel owns pure tables and models; the shell owns ImGui application and AppKit bridges.
Shared shape/type/density constants avoid parallel per-theme metrics. Geist 1.7.2 is hash-pinned
with SIL Open Font License 1.1; Sans/Medium digits force 0.6 em while Mono keeps native advances.
Current consumers of the actor roles and proposal/attention components are described in
[App Session](../architecture/app-session.md) and the [App companion](../architecture/app-design-system.md).

The [App companion](../architecture/app-design-system.md) and
[operator guide](../guides/editor-workspace.md) describe current source behavior and recovery.
The [validation record](../milestones/ux/ux4-validation.md) retains historical exact-image failures,
Compact cost failure after control radius reached 0, and incomplete native verification. These
limits remain open; this Proposed ADR does not relax the gate or imply owner acceptance.
