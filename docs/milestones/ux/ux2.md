# UX2 — Scene Documents and Hierarchy

**Status**: Proposed

First draft for owner revision. UX2 makes a scene a saved document instead of C++ code, rebuilds
the Hierarchy around that document, gives lights and objects an authored enabled state that is
distinct from culling, folds the two single-model scenes into labs, and gives the application an
icon. Unlike [R2](../r/r2.md) and [R3](../r/r3.md) it changes behaviour, so it carries its own gates and an
explicit re-baseline. [Part IV](../../roadmap/editor-experience.md#ux2--scene-documents-and-hierarchy)
owns the outcome and gates; this record keeps the contract, evidence and limits.

**Placement:** R4 → **UX2** → N1.

## Observed state before UX2

- Eight catalog scenes are defined in `SceneLibrary.cpp`. Sponza's 16 local lights
  (`SponzaLightRig`), its 120-second tour (`SponzaCameraTour`), San Miguel's rail, every initial
  camera and the environment are authored in C++; the four labs are fully procedural.
- `GltfLoader` reads meshes, materials, a flattened node hierarchy and the first animation. It
  ignores cameras, `KHR_lights_punctual`, later animations and `extras`.
- The Hierarchy shows a `Workspace` root (Editor Camera, Rendering with eleven topics) beside the
  scene root (Lights, Local lights with per-row checkboxes, Objects as a flat list). Editor state,
  renderer configuration and scene content share one tree.
- Only local lights have an enabled flag. Objects have none: a dimmed row means frustum or
  occlusion rejection, a per-frame result. Nothing an operator changes is saved.
- Damaged Helmet and Milk Truck are catalog scenes but serve as fixtures: the helmet is the only
  generated-tangent, full-texture-set PBR asset and owns three reference hashes; the truck is the
  only real glTF animation clip. No lab contains either.
- Every other serialized format — capture manifest v2, measurement schema 4, PNG frame metadata,
  the light-check dump, graph dumps, workspace schema 3, bake manifests, the module contract — is
  evidence, provenance or layout. None describes scene content, and none is a candidate for glTF.
- `App` is a bare binary, so macOS shows a generic icon in the Dock and application switcher.

## Decisions taken with the owner

| Topic | Decision |
|---|---|
| Format | A scene document is a valid glTF 2.0 file, `Assets/Scenes/<id>.scene.gltf`, checked in. Lights use `KHR_lights_punctual`, cameras and rails use glTF cameras and animations, and one vendor extension, `LMX_scene`, carries what glTF lacks |
| Saved settings | The scene's look is saved: exposure, bloom, sun and shadow, environment, lights, cameras, enabled state. Renderer configuration — reconstruction, render scale, classification, submission, occlusion, lighting mode, diagnostic views — stays with the editor session and CLI, so a scene file never changes the technique an evidence run measures |
| Hierarchy | Scene-only tree; look settings open in the Inspector from an Environment node; renderer configuration and live readings move to a dockable Rendering panel; the editor camera becomes a viewport control |
| Editing scope | Save, Save As and Revert for what is editable today. No create, delete, duplicate or reparent |
| Fixture scenes | Helmet joins MaterialLab, Milk Truck joins TemporalLab; both leave the catalog |

Rejected: a JSON document referencing glTF (Donut's model — simpler to write and diff, but not
openable by glTF tools); a self-contained glTF per scene (the large assets are fetched,
hash-pinned and ignored by git, and procedural labs have no file form).

## The `LMX_scene` extension

It appears in `extensionsUsed`, never `extensionsRequired`, so generic tools still open the file
and show its lights and cameras. cgltf already parses `KHR_lights_punctual` and cameras and hands
unknown extensions back as raw JSON, which the tokenizer it embeds can read; no JSON dependency is
added. Saving uses a deterministic Luminex writer — fixed key order, shortest round-trip floats —
so a save is byte-stable and diffs are minimal. The document never contains meshes. Animation keys
live in a standard external glTF buffer, `<id>.scene.bin`, beside the document: a few kilobytes of
little-endian floats whose layout the document's accessors describe. It is not a private format,
so other tools still play the rail. Save and Save As write both files; the Inspector lists a
rail's keys, since neither file shows them readably.

| Location | Field | Meaning |
|---|---|---|
| Node | `asset` | `uri` relative to `Assets/`, with the referenced file's SHA-256. The asset's node hierarchy is instantiated beneath this node at load; it is never copied into the document |
| Node | `overrides` | Per referenced node: asset node index with its name as a check, then any of `enabled`, translation, rotation, scale. A name mismatch fails the load with both names |
| Node | `generator` | A procedural lab by name with its parameters; lab CLI options still override |
| Node | `enabled` | Authored enabled state for this node and its descendants |
| Light node | `role`, `castsShadow` | Directional-light role and the single shadow caster |
| Root | `look` | Exposure, bloom, shadow filter and environment settings |
| Root | `schemaVersion` | Starts at 1 |

The light model already matches `KHR_lights_punctual`: linear colour times intensity under the
candela/lux relation, radian cone angles, and the same smooth range window
([ADR 0023](../../decisions/0023-local-light-and-cluster-contract.md)). Two differences are handled
at load: Luminex requires a finite range, so a punctual light without one is skipped with a
Console warning; glTF encodes direction as node rotation, so a spot's direction is derived from
its node.

## Enabled is not culled

| | Disabled | Culled |
|---|---|---|
| Source | Authored by the operator | Derived each frame from frustum or HZB tests |
| Saved | Yes, in the document | Never |
| Scope | The node and its descendants | One instance |
| Rendering | Leaves the draw population: no colour, depth, motion, shadow, outline or occluder contribution; identity and table row are kept, as disabled lights keep theirs | Remains in the population; rejected for this view only |
| Counters | Its own count; never reported as rejected | Frustum and occlusion counts as today |
| UI | Inspector header checkbox; muted row with an "off" marker | Dimmed row with the reason on hover and in the Inspector, as today |

An instance flag joins `kInstanceMotionInvalid` and `kInstanceBoundsUnreliable` in the 240-byte
row, so CPU and GPU classification exclude the same set and the row ABI keeps its size. Toggling
requests the reset a light toggle requests today and invalidates occlusion coverage, because the
previous frame's HZB may contain an occluder that no longer draws. A measurement run freezes the
enabled population of lights and objects at its start. A new ADR records the document contract
and these semantics, amending what [ADR 0021](../../decisions/0021-gpu-scene-handoff-contract.md)
says about population.

## UX2.1 — Scene documents under the existing editor

**Outcome:** every catalog scene loads from a document and renders as before; no UI changes.

**Deliver:** loader growth in Asset — cameras, punctual lights, the retained node hierarchy,
every animation, animated camera nodes, and `LMX_scene` parsing; the document model and writer in
Asset with no GPU type; instantiation in Scene; `--scene <id|path>` with ids resolving under
`Assets/Scenes/`. A one-time exporter, run at the parent commit, writes the six documents from the
live scenes; then `SponzaLightRig`, `SponzaCameraTour`, the San Miguel rail and the C++ initial
cameras are deleted. `--local-light-rig off` becomes the enabled state of the rig's group node.
Capture manifests and measurement reports record one SHA-256 over the document and its buffer (manifest v3, measurement
schema 5, with the comparison tools reading the previous versions). A pinned glTF validator,
fetched by setup, checks every catalog document and every file the writer tests produce.

**Exit gate:** at `--temporal off`, Sponza, San Miguel when installed, and the four labs match the
parent under the exact-image matrix and produce identical graph dumps; save-then-load is
byte-stable; the validator reports no errors; a malformed or stale-hash document fails with a
message naming the field.

## UX2.2 — Fixtures join the labs

**Deliver:** MaterialLab's document adds the helmet as a real-asset station with an axis
annotation; TemporalLab's adds the truck as a real-asset mover beside the authored motion. The
`damaged-helmet` and `milk-truck` ids, their reference hashes and their scene-id test rows retire;
loader tests keep loading both assets directly. MaterialLab and TemporalLab are re-baselined once,
with parent and candidate images and the owner's acceptance in the record. The roadmap's
integration rule becomes "Sponza and MaterialLab".

**Exit gate:** the generated-tangent and animation-clip cases still run; every unchanged scene
still matches its reference; the re-baseline lists each retired and each new hash.

## UX2.3 — Enabled state

**Deliver:** the semantics table above in Scene, Render, both classification paths and the
measurement freeze; status vocabulary `Disabled`, `Culled: frustum`, `Culled: occluded` in the
model and readings; save and load of `enabled`.

**Exit gate:** a disabled object contributes to no target and no counter but its own; CPU and GPU
classification agree on the population; re-enabling restores the exact prior image at
`--temporal off`; occlusion checks stay clean across a toggle of a major occluder.

## UX2.4 — Hierarchy, Rendering panel and document workflow

**Deliver:** the tree below with search, keyboard navigation and culled-row cues kept; an Inspector
header with the Enabled checkbox, name and kind for lights and objects, and no checkboxes in rows;
the Rendering panel holding the renderer-configuration topics with their 250 ms coherent readings
and scoped resets; workspace schema 4 migrating schema 3; File > Open, Save, Save As and Revert
with native dialogs, a dirty marker, and a confirmation before discarding edits. Transport Stop
still restores the preview state and never marks the document dirty.

```
▾ Sponza *                 Rendering panel
    Tour Camera              Reconstruction · Resolution · Visibility · Occlusion
    Sun · Fill · Rim         Submission · Lighting mode/check/view · Display · Scene tables
    Environment            Inspector [Environment]
  ▸ Local Lights (16)        Exposure · Bloom · Shadows · Sky and IBL
  ▾ Crytek Sponza
      arch · bricks · …
```

**Exit gate:** the [UX1 completion tasks](../../roadmap/editor-experience.md#completion-gate) still
pass on the new layout; an operator can disable an object, edit a light, change exposure, save,
relaunch and find all three; Revert restores the file's state; schema 3 workspaces open without
loss of scale or window bounds. Unverified gestures are recorded as unverified.

## UX2.5 — Application icon and acceptance

**Deliver:** an owner-approved mark, with its source under `Assets/Icons/`; a PNG staged beside
the binary as `Fonts/` is; a small AppKit unit in the App shell that sets the application icon at
startup for the Dock, the switcher and both detached windows. Headless runs skip it. An `.app`
bundle was rejected for now: shader, font and scene loading resolve against the working directory.
Then the whole-application pass, the architecture and guide pages, and `AGENTS.md`.

**Exit gate:** the icon appears on a fresh launch; all earlier slice gates hold together.

## Boundaries and deferrals

Create, duplicate, delete and reparent; importing an asset into an open scene (the vertex and
index pool is immutable after finalize); Undo/Redo, gizmos, viewport picking and an asset browser
remain deferred as [UX1](ux1.md) left them. No renderer feature, glTF BLEND support, skinning or
morph targets. Renderer configuration is not persisted. `KHR_node_visibility` is not adopted: its
meaning for lights is unsettled, and `LMX_scene.enabled` has one definition here.

## Risks and open points

- **Two files per animated scene.** The document and its `.scene.bin` must travel together: a
  missing or wrong-length buffer fails the load with a named error, and the recorded evidence hash
  covers both. Rejected: an embedded base64 buffer (pollutes every diff of the document), readable
  key arrays inside `LMX_scene` (invisible to other tools), and a private `.animation` format (a
  second specification to version). The owner confirmed the standard `.bin` buffer.
- **Spot direction round trip.** Direction to quaternion and back is not bit-exact, so UX2.1's
  exact-image gate may fail on spot-lit pixels. The record will report it as measured; no
  tolerance is pre-approved.
- **Evidence provenance.** A saved look now shapes every capture. The document hash in manifests
  and reports is what keeps old and new evidence comparable.
- **Editing checked-in scenes.** Saving a catalog scene is allowed: authoring Sponza's rig is the
  purpose, and git shows the change. `Tools/Screenshots/reference.json` records each scene's
  document hash, so a drifted scene fails the reference run by name before any image compares.
  Save As is the route for experiments.
- Settled: the editor-camera pose is saved only by an explicit "save view as camera" action; lab
  generators accept no overrides on generated nodes.
