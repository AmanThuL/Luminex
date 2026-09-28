# UX3 — Scene Documents and Hierarchy

**Status**: Implemented

Revised on 2026-09-27 after a survey of the code at the UX2 merge; the owner approved the design
section by section the same day. UX3 makes a scene a saved document instead of C++ code, rebuilds
the Hierarchy around that document, gives lights and objects an authored enabled state that is
distinct from culling, folds the two single-model scenes into labs, and gives the application an
icon. Unlike [R2](../r/r2.md) and [R3](../r/r3.md) it changes behaviour, so it carries its own
gates and one owned re-baseline. [Part IV](../../roadmap/editor-experience.md#ux3--scene-documents-and-hierarchy)
owns the outcome and gates; this record keeps the contract, evidence and limits.

**Placement:** R4 → [UX2](ux2.md) → **UX3** → N1. Renumbered from UX2 on 2026-09-25, when editor
surfaces took that identifier; records and ADRs dated earlier call this milestone UX2.

## Implementation result (2026-09-29)

All five UX3 slices are implemented. The six-scene document catalog, authored enabled state,
saved look, document Hierarchy and disk workflow are in place; Helmet and Truck remain lab
fixtures, and windowed startup loads the provisional FACET artwork. The executor plan is closed.

[Final validation](ux3-final-validation.md) records the integrated run at tag `ux3-validation`
(`7875edd`): build, contract, Metal, document and catalog-smoke checks pass; the three image gates
remain failed at 1/12 original off images, 5/15 original union cases and 10/15 current hashes.
[Original validation](ux3-validation.md) retains the orientation misses and unresolved independent
STEP-scale P2. [Editor validation](ux3-editor-validation.md) records 62/74 native gestures,
twelve unverified rows and unverified Dock/switcher appearance. The lab re-baseline, provisional
artwork and final behavior await owner acceptance. ADR 0028 remains Proposed; no merge is
authorized by implementation completion. The design and exit gates below retain their scope.

## Observed state before UX3

- Eight catalog scenes are defined in `SceneLibrary.cpp`; a `SceneId` is a catalog index. Sponza's
  16 local lights (`SponzaLightRig`), its 120-second tour (`SponzaCameraTour`, 7,201 keys), San
  Miguel's 721-key rail and every initial camera are authored in C++; the helmet and truck cameras
  are fitted to bounds; the four labs are procedural. Camera keys hold double times at exactly
  `k/60` and interpolate yaw and pitch linearly; Sponza's yaw is unwrapped.
- Every scene shares one neutral environment: a one-texel sky and a key/fill/rim directional rig
  whose first light casts the shadow. MaterialLab loads the studio HDRI and zeroes the rig only if
  the HDRI loaded. Exposure, bloom and shadow filter live in the editor-wide `EditorRenderSettings`,
  unchanged by a scene switch; headless runs use the same defaults from `SceneView`.
- `GltfLoader` reads meshes, materials and the first animation, flattening nodes into world-space
  instances without keeping a node index; a multi-primitive node yields several instances. It
  ignores cameras, `KHR_lights_punctual`, later animations and extensions. cgltf's embedded jsmn
  tokenizer is `static` inside the one implementation unit, and no C++ JSON writer exists.
- The Hierarchy lists Lights, Local lights and Objects grouped by source name. UX2 moved the
  local-light enabled checkbox to the Inspector header; objects have no enabled state, and a
  dimmed row means frustum or occlusion rejection. `VisibilityReason::Disabled` already exists and
  means camera culling is switched off. The shadow view reuses the item list unculled;
  `OcclusionReference` and the selection outline's coverage pass walk every item unclassified.
  A measurement run cancels on some light changes but freezes no population. Nothing is saved.
- Damaged Helmet and Milk Truck are catalog scenes that serve as fixtures: the helmet is the only
  generated-tangent, full-texture-set PBR asset and owns five of the fifteen hashes in
  `Tools/Screenshots/reference.json` (Sponza, helmet and MaterialLab × off, TAA 1, TAA 0.5,
  MetalFX 1, MetalFX 0.5); the truck is the only real glTF animation clip.
- Evidence formats — capture manifest v2, measurement schema 4, PNG `lmx:frame` metadata, graph
  dumps, workspace schema 4 — describe runs, not scene content. `App` is a bare binary with no
  Objective-C++, so macOS shows a generic icon.

## Decisions taken with the owner

| Topic | Decision |
|---|---|
| Format | A scene document is a valid glTF 2.0 file, `Assets/Scenes/<id>.scene.gltf`, checked in. Lights use `KHR_lights_punctual`, cameras and rails use glTF cameras and animations, and one vendor extension, `LMX_scene`, carries what glTF lacks |
| Saved settings | The scene's look is saved: exposure, bloom, shadow filter, environment, lights, cameras, enabled state. Renderer configuration — reconstruction, render scale, classification, submission, occlusion, lighting mode, diagnostic views — stays with the editor session and CLI |
| Save model | The live scene stays the only place edits happen. Save is a pure function of the loaded document, the node binding and the current scene; dirty means its result differs from the loaded document |
| Parity | glTF stays the single definition of an orientation. The one-time exporter searches neighbouring float quaternions until the loader reproduces the parent's exact values, and lists any it cannot match; image differences are reported as measured, with no tolerance |
| Hierarchy | Scene tree grouped by the document; the look opens in the Inspector from an Environment node |
| Editing scope | Save, Save As and Revert for what is editable today. No create, delete, duplicate or reparent |
| Generated subjects | Lab-generated objects and lights toggle and edit for the session only, marked "not saved"; they never make the document dirty |
| Fixture scenes | Helmet joins MaterialLab, Milk Truck joins TemporalLab; both leave the catalog. The standing matrix becomes Sponza, MaterialLab and TemporalLab × the same five modes |
| Delivery | One executor plan and one squash-merged pull request for all five slices, with owner checkpoints |

Rejected: a JSON document referencing glTF (Donut's model, not openable by glTF tools); a
self-contained glTF per scene (large assets are fetched and hash-pinned, labs have no file form);
Luminex-native orientation copies inside `LMX_scene` (two definitions that can disagree); a
document model as the edit truth (rewrites every edit path, drifting toward deferred Undo).

## The `LMX_scene` extension

It appears in `extensionsUsed`, never `extensionsRequired`, so generic tools still open the file
and show its lights, cameras and rails. The document never contains meshes. Animation keys live in
a standard external buffer, `<id>.scene.bin`; Sponza's rail makes it about 230 KB. Save and Save
As write both files with a deterministic writer — fixed key order, shortest round-trip floats — so
a save is byte-stable.

| Location | Field | Meaning |
|---|---|---|
| Root | `schemaVersion` | Starts at 1; the glTF scene's `name` is the scene label (`San Miguel`, `Sponza`) |
| Root | `camera` | Node index of the scene camera; its rest pose is the initial camera |
| Root | `look` | `exposure` (EV, auto flag and every metering field), `bloom` (enabled, threshold, intensity), `shadow` (filter), `environment` (a neutral sky colour as sRGB bytes, or an HDRI `uri`, `sha256`, yaw, scale and face sizes) |
| Root | `loop` | Whether the scene clock loops; every animation plays on the one clock |
| Animation | `sampleRate` | Key *k* of a document animation is exactly `k / sampleRate` seconds; the accessor holds the float times for other tools, and a time other than `float(k / sampleRate)` fails the load naming the key. Clips inside a referenced asset keep the loader's 60 Hz bake and loop on their own duration |
| Node | `asset` | `uri` relative to `Assets/` with that file's SHA-256; the asset's hierarchy is instantiated beneath this node, never copied |
| Node | `overrides` | Per asset node index, with its name as a check: `enabled` and a world pose in Luminex's own form (translation, rotation in degrees, scale), applying to every instance the node yields. A name mismatch fails with both names. Animated nodes take no pose override |
| Node | `generator` | A lab by name with its parameters; lab CLI options override them for the session only |
| Node | `enabled` | Authored state of this node; effective state is the AND over its ancestors |
| Light node | `role`, `castsShadow` | Directional role (key, fill, rim) and the single shadow caster |

Nodes above a light or camera carry no transform, so a light's position and a spot's direction
are the node's own. The light model already matches `KHR_lights_punctual`
([ADR 0023](../../decisions/0023-local-light-and-cluster-contract.md)); a point or spot light
without a range is skipped with a Console warning. A directional strength is written as colour
times a power-of-two intensity no smaller than its largest component, which is exact in both
directions. A disabled directional light keeps its role and direction, contributes zero strength
and changes no pass declaration; MaterialLab's rig is disabled in its document, and an HDRI a
document names is required. Loading converts rail rotations to today's yaw-and-pitch keys,
unwrapping yaw on every rail; the exporter's report proves each key reproduces, San Miguel's
included. Instantiation keeps the parent's order within each kind — asset content, then the
environment, then lights in document order, which the exporter writes in rig order — so handles,
rows and light summation order are unchanged.

The writer is canonical: its output depends only on the model. Save starts from the loaded model
and replaces only values that changed, so an unedited scene saves byte-identically and a changed
spot or directional orientation reuses the exporter's quaternion search. Dirty compares the
writer's output for the loaded model with its output for the exported one, and the validation
script also requires every catalog document to be in canonical form. A catalog scene's `scene`
evidence field stays its id; a document opened by path records the path as given.

## Units

| Unit | Home | Owns |
|---|---|---|
| `JsonWriter` | `Core/IO` | Byte-stable JSON with `std::to_chars` floats |
| `CgltfImplementation`, `JsonTokens` | `Engine/Asset/Model` | The one cgltf implementation unit and a reader over its tokenizer for whole scene documents; no new dependency |
| `GltfLoader` growth | `Engine/Asset/Model` | Node index per instance and every animation |
| `SceneDocument` | `Engine/Asset/Document` | Plain model, reader and writer for `.gltf` and `.bin`; no GPU type |
| `SceneLook` | `Engine/Asset/Document` | Per-scene look; `EditorRenderSettings` loses those fields |
| Instantiation, binding | `Engine/Scene` (asset, light, camera nodes); `Scenes` (generator registry) | Scene built from a document, plus the node-to-instance, light and camera binding |
| `exportSceneDocument` | `Scenes` | The pure save function |
| Catalog | `Scenes` | Document ids under `Assets/Scenes/`; `SceneId` becomes a document path; `--scene <id\|path>` |

`--local-light-rig` overrides the enabled state of the document's top-level local-light group,
replacing the check on the scene's name; on a document without one it is a no-op, and its default
in evidence metadata becomes "the document has such a group". The Inspector edits a
document-bound object's pose and enabled state for every instance its asset node yields.

## Enabled is not culled

| | Disabled | Culled |
|---|---|---|
| Source | Authored by the operator | Derived each frame from frustum or HZB tests |
| Saved | Yes, in the document (generated subjects: never) | Never |
| Scope | The node and its descendants | One instance |
| Rendering | No colour, depth, motion, shadow, outline or occluder contribution; identity and table row kept | In the population; rejected for this view only |
| Counters | Its own `disabled` count only | Frustum and occlusion counts as today |
| UI | Inspector header checkbox; muted row with an "off" marker | Dimmed row with the reason on hover and in the Inspector |

`kInstanceDisabled = 4u` joins the 240-byte row's flags beside `kInstanceMotionInvalid` and
`kInstanceBoundsUnreliable`. CPU and GPU classification test it before any culling bypass, so the
shadow view excludes it too; the existing reason is renamed `CullingOff` and the new one is
`AuthoredOff`. `OcclusionReference` and the outline's coverage pass skip flagged items; items stay
one per object. Toggling requests the reset a light toggle requests and bumps the occlusion
coverage epoch. Measure disables enabled toggles with other edits and records the starting
population. A new ADR records the document contract and these semantics, amending what
[ADR 0021](../../decisions/0021-gpu-scene-handoff-contract.md) says about population.

## UX3.1 — Scene documents under the existing editor

**Outcome:** every catalog scene loads from a document and renders as before; no UI changes.

**Deliver:** the units above; a one-time exporter, committed with the writer, run against the
unchanged C++ scenes to write all eight documents with its exact-match report, tagged
`ux3-exporter` and then deleted with `SponzaLightRig`, `SponzaCameraTour`, the San Miguel rail and
the C++ cameras. Capture manifest v3 and measurement schema 5 add `sceneDocument` with one SHA-256
over the document bytes followed by its buffer's; PNG `lmx:frame` metadata is unchanged; the
comparison tools read old and new versions. Setup pins the Khronos glTF Validator (an x86_64
binary run under Rosetta), and `Tools/Scenes/validate_documents.py` checks every catalog document
and every file the writer tests emit into the test build's `SceneDocuments/` directory, locally
and in CI. `Tools/Screenshots/parity_rounds.py` drives the parent-versus-candidate procedure R3.6
ran by hand: eight alternating rounds of `parity.py` per binary, where a candidate hash absent from
every parent round fails.

**Exit gate:** at `--temporal off`, all eight scenes at one frame, and Sponza and San Miguel when
installed at frames 600 and 3600, are byte-identical to the parent, with identical graph dumps; the
fifteen-case matrix passes `parity_rounds.py`; save, load, save is byte-stable; the validator reports no errors; a malformed or stale-hash document fails with
a message naming the JSON path. Differences are reported as measured.

## UX3.2 — Fixtures join the labs

**Deliver:** MaterialLab's document adds the helmet as an asset-node station with an axis
annotation; TemporalLab's adds the truck as an asset node playing its own clip beside the authored
motion. The `damaged-helmet` and `milk-truck` ids, documents, scene-id test rows and ten reference
hashes retire; loader tests keep loading both assets directly. MaterialLab and TemporalLab are
re-baselined once, with parent and candidate images in all five modes and the owner's acceptance in
the record. The truck's clip loops on its own duration within TemporalLab's clock.
`reference.json` schema 2 covers Sponza, MaterialLab and TemporalLab × five and records each
document hash, which `parity.py` checks before any image. The roadmap's integration rule
becomes "Sponza and MaterialLab".

**Exit gate:** the generated-tangent and animation-clip cases still run; Sponza still matches its
reference; the re-baseline lists each retired and each new hash.

## UX3.3 — Enabled state

**Deliver:** the semantics above in Scene, Render, both classification paths, the counter block
and classify-check reconciliation, and the measurement population; status vocabulary `Disabled`,
`Culled: frustum`, `Culled: occluded` in the model and readings; save and load of `enabled`.

**Exit gate:** a disabled object contributes to no target and no counter but its own; CPU and GPU
classification agree on a population with disabled rows; re-enabling restores the exact prior
image at `--temporal off`; `--occlusion-check` stays clean across a toggle of a major occluder.

## UX3.4 — Document hierarchy and workflow

**Deliver:** the tree below, in document order, with search, keyboard navigation and culled-row
cues kept; the Enabled checkbox on group, object and light headers; generated subjects labelled
"Generated by <lab> · not saved"; an Environment subject owning Exposure, Bloom and Shadows, which
leave the Rendering panel, with Sky and IBL read-only; look resets restoring the document's values.
File > Open… and Save As… use native dialogs; Save and Revert join them beside the catalog
submenu. A confirmation (Save, Discard, Cancel) precedes discarding edits on Open, a catalog switch,
Revert, Quit or window close. Save, Save As and Revert require a stopped transport and no active
Measure. The dirty marker shows on the tree root and the window title. View > Set Scene Camera from
View saves the editor pose only on request. Transport Stop restores the preview and so never leaves
the document dirty.

```
▾ Sponza *                 Rendering panel
    Tour Camera              Reconstruction · Resolution · Visibility · Occlusion
    Key · Fill · Rim         Submission · Lighting mode/check/view · Display · Scene tables
    Environment            Inspector [Environment]
  ▸ Local Lights (16)        Exposure · Bloom · Shadows · Sky and IBL (read-only)
  ▾ Crytek Sponza
      arch · bricks · …
```

**Exit gate:** the [UX1 completion tasks](../../roadmap/editor-experience.md#completion-gate) still
pass on the new layout; an operator can disable an object, edit a light, change exposure, save,
relaunch and find all three; Revert restores the file's state; schema 4 workspaces open without
loss. Unverified gestures are recorded as unverified.

## UX3.5 — Application icon and acceptance

**Provisional icon direction (owner-selected 2026-09-27):**
[B2.2 — FACET / 晶刃](https://www.figma.com/design/IElss0KLstgyaLJcr3HmyR?node-id=8-255),
an LMX shared-edge mark with a slanted outline, blue-violet facets and a diamond-shaped crossing.
The editable SVGs (`b2.2-facet-icon.svg` and `b2.2-facet-mark.svg`) are in the owner's local
`Luminex-Identity` design workspace under `svg/round-03/`, whose README records the selection.
Final artwork approval is a blocking checkpoint of this slice; the approved SVGs then live under
`Assets/Icons/`, so nothing durable depends on that workspace or the Figma file.

**Deliver:** the approved mark as SVG and a 1024-pixel PNG under `Assets/Icons/`, staged beside the
binary as `Fonts/` is; `Shell/AppIcon.mm` setting the application icon at windowed startup, which
macOS uses for the Dock, the switcher and detached windows. Headless runs skip it. An `.app` bundle
stays rejected: shader, font and scene loading resolve against the working directory. Then the
whole-application pass, the ADR, the architecture pages, a scene-document guide and `AGENTS.md`.

**Exit gate:** the icon appears on a fresh launch; all earlier slice gates hold together.

## Boundaries and deferrals

Create, duplicate, delete and reparent; importing an asset into an open scene (the vertex and index
pool is immutable after finalize); environment editing; Undo/Redo, gizmos, viewport picking and an
asset browser remain deferred as [UX1](ux1.md) left them. No renderer feature, glTF BLEND support,
skinning or morph targets. Renderer configuration is not persisted, and the workspace schema does
not change. `KHR_node_visibility` is not adopted: its meaning for lights is unsettled, and
`LMX_scene.enabled` has one definition here. Emissive step tracks, invalid-motion classes and
closed-form light orbits have no glTF form and stay inside lab generators.

## Risks and open points

- **Exactness beyond the search.** The exporter proves each orientation round trip before the image
  gate runs; a value it cannot match is named in its report and the gate result stays as measured.
- **Two files per animated scene.** A missing or wrong-length buffer fails the load with a named
  error; the recorded evidence hash covers both files. The owner confirmed the standard buffer over
  base64, readable key arrays and a private format.
- **Asset hashes cover the referenced file only.** Sponza's `.bin` and textures are covered by
  setup's pinned converted-tree hash, not by the document.
- **Validator under Rosetta.** The pinned release ships no arm64 macOS binary; CI installs Rosetta
  when absent, and the validation script names Rosetta when it cannot run.
- **Evidence provenance.** A saved look now shapes every capture; the document hash in manifests,
  reports and `reference.json` keeps old and new evidence comparable and fails a drifted scene by
  name before any image compares. Saving a catalog scene is allowed; Save As is the route for
  experiments.
- Settled: the editor-camera pose is saved only by the explicit action; lab generators accept no
  overrides on generated nodes.
