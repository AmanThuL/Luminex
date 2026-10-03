# UX6 milestone review and fix validation

**Status**: Implemented — owner authorized integration on 2026-10-04; failed and incomplete gates retained as measured

An independent review examined the UX6 branch on 2026-10-04 against the [record](ux6.md), the
closed executor plan and the [execution](ux6-validation.md) and [final](ux6-validation-final.md)
validation pages. Three fix commits followed. This page records what the review verified, each
defect with its fix, the gates re-run afterwards and what stays unverified. It changes no gate
result: the original parent parity failure and the incomplete native completion gate stay as
measured, and ADRs [0031](../../decisions/0031-scene-document-content-and-mobility.md) and
[0032](../../decisions/0032-vendored-imguizmo.md) stay Proposed.

## Scope and identity

| Item | Value |
|---|---|
| Reviewed revision | `8a4ca67` on `feat/ux6-scene-authoring`, tag `ux6-integration-chain` |
| Fix range | `8a4ca67..a315068`, three commits |
| Final code revision | `a315068`, tag `ux6-review-gates`; later commits change documentation only |
| Last revision holding the executor plan | `a315068` |
| Parent | main at `80fa716` |
| App measured after the fixes | SHA-256 `7a4855b7ce5028aba21ce290085196905c3f2fb8b377737d43fdf3f73fdf1802` |

## What the review verified

By reading the code end to end, the review confirmed these properties of the reviewed revision:

- **Content integrity.** Every geometry and image byte is hash-checked before use; accessor and
  view bounds cannot overflow, indices are checked against vertex counts, and URIs reject `..`,
  absolute and scheme paths. The `KHR_animation_pointer` parse accepts only the emissive-strength
  pointer of an in-range material.
- **Hash and dirty definitions.** The loaded-document hash is still JSON then animation bytes;
  Save never rewrites a present geometry buffer or image, and Save As copies them.
- **One pose rule.** Inspector fields and reset, the gizmo and session proposals and commands all
  refuse a locked pose through `SceneSession` with the lock's reason; Enabled stays editable and
  no route changes mobility.
- **Drag lifecycle.** Playback, measurement, disablement, selection change and scene replacement
  end a drag; Escape restores the drag-start pose bitwise and does nothing afterwards.
- **Gizmo math and layer.** Scale is clamped to 0.01–100 with nonfinite and sheared results
  refused; the gizmo's projection matches the camera; point lights offer Move only and lights
  never Scale. `ImGuizmo.h` is included by `ViewportGizmo.cpp` alone, App alone links ImGuizmo,
  and setup asserts the pinned commit.
- **Shortcuts.** Q, W, E, R, Y and X are refused during text entry, popups and right-mouse look,
  and collide with no existing binding.

## Findings and fixes

| Finding | Severity | Outcome |
|---|---|---|
| A saved object's rotation with no exact glTF quaternion within the ±4-ULP search blocked Save. Most typed or dragged rotations have none. No catalog object reaches the defect, since the movable Helmet saves through an Euler override; it affects movable mesh nodes | P1 | Fixed in `534f0d0`: the nearest quaternion is saved and its decoded value adopted, as lights and the scene camera already were |
| Any hidden entry in a texture folder, such as Finder's `.DS_Store`, blocked every Save of that document | P1 | Fixed in `534f0d0`: hidden entries are ignored; other foreign files are still refused |
| Stopped TemporalLab Wheels and Wheels.001 rows showed the operator mark and "not saved" (the open P2 of the final validation). Their stopped pose comes from the importer's baked track, while the comparison samples the asset clip through a different float path, so an exact compare reported an edit no route made | P1 | Fixed in `83f2099`: an animation-owned object never reports a change. The new catalog regression test failed 4 assertions without the fix and passes with it |
| Animated imported source nodes showed a checked, disabled Static box, although they take no mobility | P2 | Fixed in `83f2099`: they show no Static value |
| Schema 2 materials, textures, images or samplers without meshes were verified and then silently dropped, and the next Save lost them | P2 | Fixed in `534f0d0`: the reader refuses them with the JSON pointer |
| Every edit generation, each frame of a gizmo drag included, re-hashed the geometry buffer and decoded every image | P2 | Fixed in `534f0d0`: edit exports trust content shared unchanged with the read document; Save still verifies it |
| `Tools/Screenshots/reference.json` pinned the three standing documents as they were before mobility was authored, so standard parity refused at the branch tip with "document drift" | P2 | Fixed in `a315068`: only the document pins changed, to the values the final measurement's candidate reference used; no image hash changed |

Retained without change, all P3:

- A schema 1 node with `"motion"` loads as Rigid without a diagnostic. A schema 1 pointer channel
  reports a material-range error instead of a schema error.
- `mobility` on a local light without a range reports the wrong node-kind message.
- `validate_documents.py` is stricter than the reader about stray `contentHashes` entries.
- ImGuizmo hides its handles for a subject closer than about twice the near plane. A gizmo
  blocked by text entry or a popup is drawn gray without a reason tooltip.
- Native menu shortcuts decide on the previous frame's focus, so the first tool key typed after
  focusing a text field may still switch the tool. F, C and Home share the same design.

## Gate re-runs

Measured at `a315068` on the Apple M3 Max reference host. Evidence is under
`../Luminex-evidence/ux6/review/`.

| Gate | Result |
|---|---|
| `MTL_DEBUG_LAYER=1 xmake test -P .`, all four groups | Passed: RojoRHITests unit 0.267 s and GPU 1.717 s; Tests unit 11.009 s and GPU 48.179 s |
| Format check, the six root checkers, Python suite | Passed; 363 Python tests |
| Pinned glTF validator | Passed for 101 documents: six catalog and 95 writer outputs |
| Standing parity against the live reference | 15/15 byte-identical, one round |
| LightLab parity against the final measurement's candidate anchor | 5/5 byte-identical, one round |

CI runs again on the pushed head, and the merge waits for it.

## Limits

- No native gesture was re-run after the fixes. The Wheels fix is verified through the loaded
  catalog document in a model test, not in the running editor.
- The parity re-runs are single rounds on the fixed build, not the final measurement's eight
  alternating parent/candidate rounds.
- Every gesture the [final validation](ux6-validation-final.md#native-authoring-checks) lists as
  unverified stays unverified: held gizmo and field drags, Escape and right-mouse combinations
  mid-drag, Inspector live-follow during drags and the complete scene-only GPU draw inventory.

## Unchanged status

- Original standing parent parity: 10/15, failed. The five MaterialLab differences are the
  owner-accepted re-baseline for the removed axis station, not a pass.
- The native completion gate is incomplete as the final validation's inventory states.
- The accepted-reference matrix (15/15) and LightLab (5/5) results stand as measured.

## Owner authorization and integration

On 2026-10-04 the repository owner asked for this milestone review, for its findings to be fixed
and for [pull request #69](https://github.com/AmanThuL/Luminex/pull/69) to merge automatically by
squash once CI is green. The owner did not re-run native gestures, so the authorization is not a
manual verification: the milestone is Implemented, and every failed gate, incomplete gate and
unverified gesture stays as recorded. No tolerance, threshold or default changed, and ADRs 0031
and 0032 stay Proposed.

- **Executor plan.** Closed and removed from the published tree. It stays in the history of the
  tag `ux6-review-gates`, placed on `a315068`, which also preserves the fix range and the
  revision the post-fix gates measured.
- **Original chain.** The tag `ux6-integration-chain` stays on `8a4ca67` and preserves the
  development chain the execution and final validation measured; `ux6-exporter` preserves the
  one-time exporter.
- **Squash commit.** Squashing turns no failed gate or scoped exception into a pass.
