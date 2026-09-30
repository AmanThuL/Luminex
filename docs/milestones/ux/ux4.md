# UX4 — Design System and Themes

**Status**: In progress — accepted by the owner on 2026-09-30; [executor plan](../../plans/2026-09-30-ux4-design-system.md) active

Written on 2026-09-29 from the merged UX3 editor, accepted on 2026-09-30, from its
[Figma baseline](https://www.figma.com/design/QxllnS3RVkIvzDWoaL6Bcq) and two research passes
(collaborative-tool design languages; Dear ImGui 1.93 and SDL 3.4 theming facts). UX4 replaces the
inherited `StyleColorsDark` look with one token-driven design system, ships light, dark and
system-following appearance, makes the macOS menu bar native, and gives the editor a visual
vocabulary for a workspace where a human operator and software agents act on the same scene and
evidence. It changes the editor only. [Part IV](../../roadmap/editor-experience.md#ux4--design-system-and-themes)
owns the outcome and gates; this record keeps the design, its evidence and its limits.

**Placement:** [UX3](ux3.md) → **UX4** → N1, so N1's surfaces are designed once in the new system.

**Why this shape.** Luminex is developed as a partnership: the owner sets direction, reviews and
accepts; agents implement and validate against recorded evidence. The editor is where both look at
the same frame, so a design system for the next five to ten years has to say on every surface who
did this, what is proposed, what is running and what the evidence is, while staying cheap to draw in
immediate-mode ImGui. The owner confirmed this reading of the objective on 2026-09-30.

## Observed state before UX4

- The style is ImGui's `StyleColorsDark` plus three overrides (frame padding 8×5, item spacing
  8×8, window padding 12) applied at shell construction, and three custom colors in
  `EditorStyle` (accent, warning, muted). The main clear color is a literal in `main.cpp`.
  About 40 literal colors live in the viewport, graph, scene and performance panels; the Render
  Graph's `imgui-node-editor` has its own 19-entry style that ImGui's colors never reach.
- Menus are ImGui items in an in-window row that also holds the transport; the native title bar is
  kept, macOS gets only SDL's default application menu, and the detached Render Graph and
  Performance windows have no menus at all.
- Corners are square except tabs (5) and graph cards (6). Buttons, frames and headers are all
  tints of one blue, so selection, action and decoration share a color.
- There is no light theme, no system-appearance handling and no theme preference in workspace
  schema 4. The pinned ImGui (1.93.0 WIP, 63 `ImGuiCol_` slots, dynamic fonts) and SDL 3.4.12
  (`SDL_GetSystemTheme`, `SDL_EVENT_SYSTEM_THEME_CHANGED`) already provide what themes need.
- Inter Regular 16 with a forced 9 pt digit advance and Codicons 16 are the only type. The UI
  renders to a `BGRA8Unorm` drawable and blends in gamma space, so sRGB tokens reach the display
  unchanged.
- The Figma baseline holds 55 screens, 348 components and 328 variables in three collections
  (`Luminex · Source values`, `Luminex · UI colors`, `Luminex · Metrics`), each with one mode.

## Decisions taken with the owner

| Topic | Decision |
|---|---|
| Identifier and placement | UX4, between UX3 and N1; one executor plan, four slices, one squash-merged pull request |
| Design language | Working name **Facet**, after the provisional FACET mark: one neutral graphite surface family, blue for the operator, violet for agents, three status hues. The name is provisional with the icon |
| Appearance | View > Appearance: **Auto (system)**, **Light**, **Dark**; Auto is the default and follows macOS live. Persisted in workspace schema 5 with a `--appearance` CLI override for windowed and gallery runs |
| Density | View > Density: **Comfortable** (today's metrics) and **Compact**; persisted in schema 5 |
| Typeface | **Geist Sans** for the interface and **Geist Mono** for data replace Inter, one pair in both themes (owner decision 2026-09-30). Both are OFL 1.1 from the `vercel/geist-font` v1.7.2 release, staged and hash-pinned by setup as Inter is today |
| Native chrome | The macOS menu bar becomes native from one platform-neutral menu model (UX4.5); the in-window row keeps the transport, activity and zoom. Windows chrome waits for a validated host; the ImGui menu row stays as the non-macOS rendering |
| Cost rule | Color, borders and small rounding only. No blur, drop shadows, gradients as layers or ImGui branch merges; rounding at most 6 pt on ordinary surfaces |
| Collaboration vocabulary | Actors (operator, system, agent), provenance marks, an activity strip and proposal/attention components are part of the system. Operator and system consumers ship in UX4; agent consumers are reserved until an agent session milestone exists |
| Evidence | Contrast is a unit test over the token tables, not a manual check; a Style Gallery window renders every component in every state so both themes are captured and compared against Figma |
| Scope guard | Scene-only screenshots stay byte-identical to the parent; the SDR/UI/capture domains of [ADR 0019](../../decisions/0019-display-domains-and-edr.md) are untouched |

Rejected: user theme editing (deferred since UX1); frosted or translucent chrome (a backend pass per
platform window); ImGui's `features/shadows` branch (no docking, a merge to maintain); a second
icon set; per-theme scene clear colors (the image is not UI); a selectable "classic" theme (no
marks, more to maintain; gallery captures document the old look); a runtime JSON theme (a missing
file is a new failure mode; generated C++ tables give one source without it).

## Principles

1. **Two actors, one surface.** The operator's color is blue; an agent's is violet; the editor's
   own automation is neutral with a glyph. A color therefore names *who*, never decoration. The
   default button is neutral; accent fills are reserved for the primary action and selection.
2. **Provenance is visible.** Authored, edited, session-only, system-applied, proposed and
   agent-applied are distinct marks with the same grammar everywhere: Hierarchy rows, Inspector
   fields, the window title and notices.
3. **Evidence over chrome.** Status by exception continues; every mark and every activity has a
   tooltip naming its source, and anything an agent claims links to the record that shows it.
4. **Flat, tonal and fast.** Depth comes from four surface steps, 1 px hairlines and 2 pt gutters
   between docked panels drawn in the canvas color, since docked windows cannot round. Every
   theme fills all 63 ImGui slots and the 19 node-editor slots from tokens; switching a theme
   copies a table between frames and touches no font atlas.
5. **Adaptive appearance.** Light and dark are peers designed from one semantic table, each
   passing WCAG 2.2 AA (4.5:1 text, 3:1 non-text); auto follows the system; Reduce Motion snaps.

## Tokens

Primitives are OKLCH ramps generated by a script (`ux4_tokens.py` in the owner's local
`Luminex-Identity/ux4-design` workspace, moving to `Tools/Theme/` in UX4.1): a near-achromatic
graphite ramp (hue 255, chroma 0.004, so the chrome never biases color judgment of the image), and
blue (248), violet (302), green (152), amber (78) and red (24) ramps at nine lightness steps.
Semantic roles alias primitives per theme; components consume roles only. The generator is the
single source: it writes the C++ token tables, the Figma variable JSON, the tables below and the
contrast audit, and CI fails when the checked-in tables differ from a fresh run.

| Role | Dark | Light | Role | Dark | Light |
|---|---|---|---|---|---|
| `surface/canvas` | `#050607` | `#DEE0E2` | `accent/operator` | `#4CABFD` | `#056FB8` |
| `surface/panel` | `#151618` | `#FFFFFF` | `accent/operator-hover` | `#87C4FD` | `#005B99` |
| `surface/raised` | `#1A1C1E` | `#FFFFFF` | `accent/operator-active` | `#B4D9FD` | `#044574` |
| `surface/sunken` | `#0B0C0E` | `#F2F4F6` | `accent/operator-text` | `#87C4FD` | `#005B99` |
| `surface/hover` | `#232426` | `#E9EBEE` | `accent/agent` | `#B88AF7` | `#7E4FB7` |
| `surface/active` | `#2C2E30` | `#DEE0E2` | `accent/agent-hover` | `#CCABFD` | `#6A3AA0` |
| `surface/overlay` (α .86) | `#050607` | `#FFFFFF` | `accent/agent-active` | `#DDCAFD` | `#502C7A` |
| `surface/viewport` (both) | `#0B0C0E` | `#0B0C0E` | `accent/agent-text` | `#CCABFD` | `#6A3AA0` |
| `text/primary` | `#F8FAFD` | `#131416` | `status/success` | `#5EBC7B` | `#0F6936` |
| `text/secondary` | `#A3A5A7` | `#67696B` | `status/warning` | `#D49824` | `#755000` |
| `text/disabled` | `#67696B` | `#848688` | `status/error` | `#F97772` | `#9E2228` |
| `text/on-accent` | `#050607` | `#FFFFFF` | `actor/system` | `#848688` | `#67696B` |
| `border/subtle` | `#232426` | `#DEE0E2` | `prov/session` | `#D49824` | `#755000` |
| `border/strong` | `#67696B` | `#848688` | `selection/bg` (α .28 / .20) | `#4CABFD` | `#056FB8` |
| `border/focus` | `#4CABFD` | `#056FB8` | `actor/operator` | `#4CABFD` | `#056FB8` |

`actor/agent` aliases the agent accent; `status/info` aliases `text/secondary`; `accent/*-subtle` is
the accent at α .18 for docking previews, drag targets and checked backgrounds. Text on a selected
row always uses `text/primary`. `surface/viewport` is the dark surround of the rendered image in
both themes, so a light chrome never changes how bright the frame looks. The generated audit checks
24 pairs per theme (text on every surface, accent and status text, accent, focus and border
boundaries, composited selection, the panel-over-canvas island step and overlay labels on the
viewport surround) and every pair passes; the smallest margins are disabled text (3.29 dark,
3.64 light against 3.0), secondary text on the light hover surface (4.62 against 4.5) and the
dark island step (1.12 against a 1.1 floor, where the light floor is 1.2: near black the WCAG
formula flattens ratios, so dark islands also rely on the 2 pt gutter and hairline).

**ImGui mapping** (all 63 slots set explicitly; `BorderShadow` stays α 0): text slots → `text/*`;
`WindowBg`, `MenuBarBg`, `TitleBgActive`, `TabSelected`, `TabDimmedSelected` → `surface/panel`;
`TitleBg`, `TitleBgCollapsed`, `Tab`, `TabDimmed`, `TableHeaderBg`, `DockingEmptyBg`,
`ScrollbarBg` and the main clear color → `surface/canvas`; `PopupBg` → `surface/raised`;
`FrameBg` → `surface/sunken`, hovered and active → `surface/hover` and `surface/active`; `Button`
→ `surface/hover`, hovered → `surface/active`, active → `accent/operator-subtle`; `Header` →
`selection/bg`; `CheckMark`, `SliderGrab`, `TabSelectedOverline`, `SeparatorActive`, `PlotLines`,
`DragDropTarget`, `NavCursor`, `NavWindowingHighlight` → `accent/operator`; `CheckboxSelectedBg`,
`DockingPreview`, `DragDropTargetBg` → `accent/operator-subtle`; `TextLink` →
`accent/operator-text`; `UnsavedMarker` → `actor/operator`; `Border`, `Separator`,
`TableBorderLight`, `TreeLines` → `border/subtle`; `TableBorderStrong`, `ScrollbarGrab`,
`ResizeGrip`, `TabDimmedSelectedOverline` → `border/strong`; `TextSelectedBg` → `selection/bg`;
`TableRowBgAlt` → `surface/hover` at α .35; the dim backgrounds → `surface/canvas` at α .6. A
`static_assert` on `ImGuiCol_COUNT` fails the build when upstream adds a slot. Node-editor slots
map the same way (`Bg` → canvas, `NodeBg` → raised, borders → `border/*`, hover and selection →
`accent/operator*`, groups → panel at α .5).

**Editor colors** become tokens too: the seven graph kind title hues and eight link hues with light
variants of matched lightness, `plot/line` and `plot/limit`, the viewport overlay colors, the
legend chip and the Console severities.

## Type, shape, space, motion and icons

- **Type ramp:** Geist Sans at three sizes and two weights, never animated: caption 13 Regular
  (chip labels, legend notes, secondary captions), body 16 Regular (every control and reading),
  body 16 **Medium** (subject and topic headers, selected tab, dialog actions) and display 20
  Medium (dialog titles only); Geist Mono 13 and 16 for timestamps, hashes, paths, Performance
  tables, graph costs, Diagnostics identifiers, legend ranges and evidence lines, so evidence reads
  as evidence. A theme changes color, never metrics: both themes share the pair. Geist Sans
  figures are proportional by default and ImGui applies no OpenType features, so the merged digit
  range keeps its forced advance, now 0.6 em (9.6 pt at 16 px, from 9 pt): the measured width of
  Geist's own tabular figures and of every Geist Mono glyph, so numerals align across both faces.
  Static instances ship in the release (`Geist/ttf`, `GeistMono/ttf`); the archive's SHA-256 is
  `7fc800d2ac6b92844895196e5041aca55d814c15db70c44f79b3b83ab82b04e2`. Codicons stay merged at
  16 px and tint through text tokens.
- **Shape:** `radius/control` 3 (buttons, inputs, checkboxes, tab tops), `radius/popup` 4 (menus,
  popups, tooltips, notices), `radius/card` 6 (graph cards, legend chip), `radius/pill` 10 for
  chips no taller than 20 pt, 0 for windows and dock surfaces. Every frame, popup, child and
  window has a 1 px `border/subtle` stroke in both themes; a light theme cannot rely on tone alone.
- **Space:** 4 / 8 / 12 stay, with 16 and 24 for dialog and notice padding. Comfortable keeps
  today's paddings; Compact uses frame padding 6×3, item spacing 6×4 and window padding 8.
- **Motion:** a 160 ms color crossfade on theme change, a 2 pt activity progress bar and nothing
  else; Reduce Motion snaps. Sizes, spacing and fonts never animate.
- **Icons and marks:** Codicons continue for actions. Actor and provenance marks are draw-list
  shapes, not glyphs: a filled dot (operator), a diamond (agent, echoing the FACET crossing), a ring
  (system) and a dashed underline (session), so no mark depends on color alone or on a sparkle
  icon that users do not read as "agent". ImGui's own `UnsavedMarker`, `TreeLines` and slider
  `ColorMarkers` are adopted for the dirty dot, Hierarchy guide lines and XYZ/RGB fields.

## Actors, provenance and activity

| Element | Grammar | Consumers in UX4 | Reserved for an agent session |
|---|---|---|---|
| Actor mark | 8 pt shape in `actor/*` with a label; tooltip names the source | Operator, System | Agent, with lifecycle states idle, working, awaiting, proposed, applied, error and stale, each a label and shape as well as a color |
| Provenance | Authored: no mark. Edited: operator dot, `UnsavedMarker` on root and title. Session-only: `prov/session` dashed underline and "not saved". System-applied: gear glyph naming the policy | Dirty documents, generated subjects, CLI masks, dynamic-resolution scale | Proposed: ghost value in `accent/agent-text` beside the struck current value. Agent-applied: violet dot |
| Activity strip | In the menu row beside the transport: actor mark, verb, progress bar, Stop | Measure phases, pending capture, scene load, controller scale changes | Agent tasks |
| Proposal card | Title, actor, change count, evidence link, Show, Accept, Reject | Style Gallery only | Agent proposals |
| Attention ring | 2 pt `accent/agent` outline on a row or field | Style Gallery only | Agent focus, never the operator's selection |
| Console | Severity colors from tokens | All rows | An actor chip and filter |

Selection stays the operator's alone. Agent vocabulary ships as tokens, Figma components and gallery
renderings so a later milestone consumes a finished design; no fake agent runs in the editor and no
experimental agent source lands on `main`.

## Appearance behavior

Auto reads `SDL_GetSystemTheme` at startup and applies `SDL_EVENT_SYSTEM_THEME_CHANGED` without a
relaunch, treating Unknown as dark. Light and Dark set each platform window's `NSWindow.appearance`
(Aqua or Dark Aqua) so native title bars and the menu bar match, while `NSApp.appearance` stays
unset so SDL keeps reporting the true system value; Auto clears it. The theme is written into the
shell's base style and applied between `Render` and `NewFrame`, so UI-scale reapplication keeps it
and pushed colors are never reverted; literal `PushStyleColor` sites become tokens. Workspace
schema 5 adds `Appearance` and `Density`; a missing value means Auto and Comfortable, and schema 4
migrates without loss. Detached windows stay opaque. High-contrast variants are a named candidate.

## Units

| Unit | Home | Owns |
|---|---|---|
| `EditorTheme` | `App/Model/Workspace` | Semantic token tables, ImGui and node-editor mappings, `Appearance` and `Density` enums, crossfade state; pure data |
| `ThemeContrastTests` | `Tests/App/Model/Workspace` | WCAG ratios for every required pair in every theme |
| `AppAppearance.mm` | `App/Shell` | Per-window appearance, system theme events, Reduce Motion query |
| `EditorStyle` growth | `App/Panels/Shared` | Chips, actor and provenance marks, activity strip, restyled notice and legend |
| `StyleGalleryPanel` | `App/Panels/Gallery` | Window > Style Gallery: every component, state, actor and theme |
| `MenuModel` | `App/Model/Workspace` | Menu tree with shortcuts, checked and enabled state and disabled reasons; rendered by native menus or ImGui |
| `NativeMenu.mm` | `App/Shell` | NSMenu rendering: key equivalents, tooltips for disabled items, submenus rebuilt on open, app and Edit menus |
| `Tools/Theme/` | Tools | OKLCH token generator writing the C++ tables and the Figma JSON, with its audit and a CI freshness check |
| Workspace schema 5 | `App/Model/Workspace` | Appearance and density persistence and migration |

## UX4.1 — Tokens and appearance

**Deliver:** `EditorTheme` with both themes filling all ImGui and node-editor slots and every editor
color; View > Appearance with schema 5 persistence and `--appearance`; system following; per-window
native appearance; the crossfade; the clear color as a token; literal colors in panels replaced;
the contrast tests.

**Exit gate:** the contrast tests pass for every pair in both themes; scene-only `--screenshot`
outputs for the reference set are byte-identical to the parent; a theme switch rebuilds no font
atlas and allocates no GPU resource; schema 4 workspaces open with Auto; a macOS appearance change
retints the editor and its detached windows without relaunch, recorded as a manual gesture.

## UX4.2 — Shape, type and density

**Deliver:** rounding, border and spacing tokens; Geist Sans Regular and Medium and Geist Mono
Regular staged by setup with license and provenance, Inter retired; the type ramp and the 0.6 em
digit advance; density presets; neutral default buttons with accent primaries; tab, frame, header,
table and scrollbar restyle; graph card, link, plot and overlay tokens; restyled notice and legend
chip.

**Exit gate:** the [completion gate](../../roadmap/editor-experience.md#completion-gate) tasks pass
in both themes at both window sizes; `MetricsRenderIndices` on the default Sponza layout stays
within 15% of the parent in both themes and both densities, recorded from the same frame; no panel
stacks labels at the default width; gallery captures at 13, 16 and 20 px show no digit ink overlap
at the forced advance on 1× and 2× displays.

## UX4.3 — Actors, provenance and activity

**Deliver:** actor and provenance marks for dirty, generated, CLI-masked and controller-applied
state; the activity strip for measurement, capture, scene loading and controller changes; reserved
agent components in tokens, Figma and the gallery only.

**Exit gate:** each existing state shows its mark with a tooltip naming its source; AppModel tests
classify provenance case by case; no new editor state is invented and no existing status text is
lost.

## UX4.4 — Style Gallery, documentation and acceptance

**Deliver:** the gallery window; gallery captures in both themes at 100% and 150% retained as
evidence beside the Figma pages; the architecture and guide pages, `AGENTS.md` and an ADR for the
theme and token contract; whole-application acceptance.

**Exit gate:** every Figma component has a gallery counterpart in both themes; all earlier slice
gates hold together; unverified gestures are recorded as unverified.

## UX4.5 — Native chrome

**Deliver:** `MenuModel` behind today's ImGui menus; on macOS the application menu bar rendered from
it (Luminex, File, Edit, View, Window, Debug, Help) with key equivalents, checked state, disabled
items whose reason is the item's tooltip and submenus that rebuild on open; the in-window row
reduced to a toolbar for the transport, activity strip and zoom; the ImGui menus kept as the
rendering for other platforms and headless tests.

**Exit gate:** every command keeps its one named route with unchanged shortcuts; disabled reasons
stay visible; the detached windows reach the same menus; the bar follows Auto, Light and Dark;
AppModel tests cover the model's enabled and checked state case by case.

## Boundaries and deferrals

No renderer, capture, manifest, measurement or CLI change beyond `--appearance`; no agent runtime,
session log or command bridge ([UX5](../../roadmap/editor-experience.md#ux5--agent-session)); no
Windows chrome until a validated host exists; no high-contrast themes; no user theme editing; no
blur, shadow or glow; no new icon set; no `.app` bundle; nothing UX1–UX3 already defer. N1's
surfaces adopt the system when they are designed, not before.

## Risks and open points

- **Retina cost.** A 4 pt corner costs about twenty times the indices of a square fill and each
  stroke doubles it; if the default layout exceeds the 15% gate, rounding drops to 2 or 0 first.
- **Derived colors.** ImGui derives several slots (tabs, docking preview) in `StyleColorsDark`;
  UX4 sets all 63 explicitly so no derivation runs on a theme table.
- **Fonts.** Geist's x-height is 0.53 em against Inter's 0.546; gallery captures decide whether body
  stays 16 or moves to 17. Geist Mono at 0.6 em is wider than the sans, so Console and Performance
  columns budget width; nothing depends on synthetic bold or OpenType features ImGui cannot apply.
- **Figma plan limits.** One mode per collection and a spent MCP quota make the themes two
  collections authored through the local development plugin; a plan upgrade removes both.
- Settled: the viewport image, its debug views and the selection outline pass are outside the
  theme; the outline color follows `accent/operator` per theme through its existing constant.

## Figma workspace

The design lives beside the baseline in the same file: `03 · UX4 Foundations` (principles, ramps,
both semantic tables with contrast ratios, editor colors, type, shape, density, actors and
provenance, appearance, cost rules and the ImGui mapping), `04 · UX4 Components` (21 sets with
`Theme=Dark|Light` variants and their state axes, plus the Appearance and Density menu) and
`05 · UX4 Screens` (the default workspace and the collaboration scenario in both themes under the
macOS menu bar and title bar drawn as system references above the toolbar row, and the Style
Gallery), all set in Geist Sans and Geist Mono. Variables sit in `UX4 · Primitives`,
`UX4 · Color (Dark)`, `UX4 · Color (Light)` and `UX4 · Metrics`, one mode each, with six `UX4/*`
text styles. Baseline pages stay untouched for comparison. The owner's local
`Luminex-Identity/ux4-design` workspace holds the token generator, the plugin phases, reports and
PNG exports; nothing durable depends on it once UX4.1 moves the generator into `Tools/`.
