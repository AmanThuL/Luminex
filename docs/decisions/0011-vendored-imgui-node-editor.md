# ADR 0011: The Render Graph node view is drawn with a vendored, patched imgui-node-editor

**Status**: Accepted (2026-09-04) · **Roadmap**: ../roadmap.md (M5.4) ·
**Spec**: ../specs/2026-09-04-m5.4-render-graph-node-view-design.md

## Context

M5.4 draws the compiled frame as a node graph inside the Dear ImGui editor. Three options were
weighed: a hand-rolled `ImDrawList` canvas, vendoring `imnodes`, and vendoring
[imgui-node-editor](https://github.com/thedmd/imgui-node-editor). The hand-rolled option adds no
dependency but re-implements pan, zoom, drag, hit-testing, and link routing. `imnodes` has no zoom.
`imgui-node-editor` (MIT) has zoom, pan, drag, selection, hover highlighting, and link drawing, but
its `master` does not compile against Luminex's pinned Dear ImGui docking commit (1.93.0 WIP) and
its `develop` branch has been idle since 2024.

## Decision

Vendor `thedmd/imgui-node-editor` at `master` commit `021aa0ea4da1`, fetched by `xmake setup` into
`ThirdParty/imgui-node-editor` with a pin assertion, and apply a Luminex-maintained patch,
`Tools/Patches/imgui-node-editor-imgui-1.93.patch`, using the same apply / reverse-check /
diff-equals-patch discipline as the Dear ImGui patch. The patch contains exactly: a version gate
around the library's `operator*(float, ImVec2)`, an `<exception>` include in `crude_json.cpp`, and
upstream pull request 339's four call-site fixes for Dear ImGui 1.92.8's swapped
`AddRect` / `PathStroke` argument order.

The library is compiled by its own `ImGuiNodeEditor` static target and linked by `App` only. Its
header is included in one translation unit, `Source/App/Panels/RenderGraphPanel.cpp`; no
`ax::NodeEditor` type appears in a Luminex header. Its JSON settings persistence is disabled; node
positions are session state and Luminex's `imgui.ini` schema is unaffected. Automatic layout is
Luminex's own pure `GraphNodeModel`, not a library feature.

## Consequences

- Re-pinning either Dear ImGui or imgui-node-editor requires rebasing this patch and recompiling
  all four library translation units; `xmake setup` rejects a mismatched checkout before applying.
- Upstream fixes that land later shrink the patch; the pin-bump procedure is to re-fetch, drop the
  hunks upstream absorbed, and re-verify the in-app smoke draw under Metal validation.
- If the library proves unusable at runtime during M5.4 stage 1, it is removed and the canvas is
  drawn with `ImDrawList`; `GraphNodeModel` and the panel's details pane are unchanged either way.
- The Tests target continues to link no UI library.
