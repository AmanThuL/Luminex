# ADR 0032: Vendor ImGuizmo for viewport transform handles

**Status**: Proposed · **Roadmap**: [UX6](../roadmap/editor-experience.md#ux6--scene-authoring) ·
**Record**: [Scene authoring](../milestones/ux/ux6.md)

## Context

The editor needs Move, Rotate, Scale and combined transform handles for a selected object or
local light. A custom `ImDrawList` implementation would require axis hit testing, constrained
movement and rotation, and world/local handling. ImGuizmo supplies these operations in the
existing Dear ImGui UI layer. The scene model remains responsible for edit permissions, finite
poses, cancellation, dirty state and persistence.

## Decision

Vendor [CedricGuillemet/ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) at commit
`18cef5e031d8c6973d80284c67f60549fafd78c1`, under the MIT license. `xmake setup` fetches that
commit into `ThirdParty/ImGuizmo`, including the upstream `LICENSE`, and rejects an existing
checkout whose HEAD differs from the pin. `ThirdParty/README.md` records its provenance.
The selected pin builds against the pinned Dear ImGui without a compatibility patch.

The `ImGuizmo` static target compiles only `src/ImGuizmo.cpp` and depends on the same `ImGui`
target as App. App is its only project consumer; the module contract grants the dependency to
`app-shell`. The library stays out of Engine, Render, AppModel and Tests.

The viewport integration will confine `ImGuizmo.h` to one App translation unit and expose no
ImGuizmo types in Luminex headers. Handles will draw into the editor UI using a finite projection
from the unjittered camera; they will not enter scene targets or temporal history. Luminex owns
the model and editor tool state, so the workspace and capture schemas remain unchanged.

## Consequences

- Re-pinning ImGuizmo or Dear ImGui requires rebuilding this target and verifying the viewport
  integration. A compatibility patch, if needed, follows the maintained-patch procedure used by
  [ADR 0011](0011-vendored-imgui-node-editor.md).
- Vendoring the whole upstream checkout retains its license and provenance; only the transform
  implementation is built. There is no new xrepo package.
- Build compatibility alone does not verify handle alignment or native gestures. Those belong to
  the scene-authoring acceptance gates once the viewport integration exists.
- If a maintained compatibility patch cannot make the pin build, execution stops for the owner
  to decide whether to use the record's limited `ImDrawList` fallback.
