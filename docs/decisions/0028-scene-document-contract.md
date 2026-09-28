# ADR 0028: Scene documents and authored enabled state

**Status**: Proposed

## Context

The editor can change object poses, lights and the scene's look. Those changes need a saved form
with stable source-node identity. The former C++ catalog cannot represent an edited scene, while
copying large fetched assets into every scene would duplicate their content and provenance.
[UX3](../milestones/ux/ux3.md) specifies glTF scene documents and the editor workflow. Its
[validation](../milestones/ux/ux3-validation.md) retains failed image gates and implementation
limits. This ADR remains Proposed while the owner reviews that behavior, the lab re-baseline and
the provisional icon artwork.

## Decision

### File and ownership contract

A scene is a valid glTF 2.0 document under `Assets/Scenes/<id>.scene.gltf`, or an explicit path.
Standard cameras, animations and `KHR_lights_punctual` describe their respective content.
`LMX_scene` appears in `extensionsUsed`, never `extensionsRequired`, and carries schema 1,
look, selected camera, looping, asset references, generators, enabled flags and source-node
overrides. The document has no meshes; referenced assets resolve under the discovered `Assets/`
root regardless of the document's directory. Asset hashes cover the named file; setup's pinned
converted-tree hash separately covers asset companions and textures.

Asset owns the CPU document model, `SceneLook`, reader, writer and orientation conversion.
Engine owns validation before upload, instantiation and document/source-to-runtime bindings.
Scenes owns the catalog, generators and pure export function. App owns editing and disk-operation
sequencing. These roles preserve the existing dependency direction; RojoRHI has no document API.

Animation keys use a standard external `<id>.scene.bin` buffer. `sampleRate` gives exact double
key time `k / sampleRate`; the standard accessor must contain `float(k / sampleRate)`. glTF
quaternions remain the sole orientation definition, with sequential yaw unwrapping on camera
rails. Directional strength uses colour times a power-of-two intensity. Ordinary export searches
neighboring float quaternions and returns an error if exact conversion cannot be represented.
The one-time migration's unmatched fallback values are a recorded deviation, not a Save rule.

### Live edits and saving

The live scene is the editing authority. Export starts from the loaded document and substitutes
changed persistent values using its binding and `SessionDocumentState`. That state preserves own
flags, imported-pose baselines and an explicitly chosen scene camera. Imported source-node pose
and enabled overrides apply to all its primitive instances; the source name checks the index.
Animated source nodes cannot receive saved pose overrides. Generated subjects, CLI masks,
playback previews and ordinary editor-camera navigation stay outside persistence.

Core's JSON writer uses shortest round-trip float text and a fixed key order. Dirty compares
canonical JSON and animation-buffer bytes, so source formatting does not make a loaded document
dirty. A non-canonical valid document can load clean and Save rewrites it canonically. An export
error remains dirty. Save writes both outputs using temporary files and ordinary failure rollback;
this is not a crash-atomic two-file transaction. Path/hash/baseline adoption follows successful
write, canonical reread/equality and hash. A verification failure after writing may leave changed
disk bytes without adopting them in memory. Save As cannot overwrite either active file through
normalized, symbolic-link or hard-link aliases.

The look saves exposure, bloom, shadow filter, environment, lights and scene camera. Reconstruction,
render scale, classification, submission, occlusion, lighting mode and debug views remain session
configuration. Environment Inspector owns look editing and resets; Sky and IBL are read-only.
Stop restores only the preview and cannot introduce document edits. Set Scene Camera from View
is the explicit camera-persistence action. Save, Save As and Revert require stopped playback and
no active measurement. Open, catalog switch, Revert, Quit and window close confirm before discarding.

### Amendment to ADR 0021: population and identity

[ADR 0021](0021-gpu-scene-handoff-contract.md)'s identity, update and retirement rules remain.
Authored disabling preserves an instance's identity, row, resources and previous-state ownership;
it does not remove the entity. Own-enabled flags combine through ancestor AND. Re-enabling a
parent preserves each descendant's own choice. Generated subjects use the same runtime behavior
but their edits are session-only.

`kInstanceDisabled = 4u` occupies the existing 240-byte instance row flags. CPU and GPU classifiers
test it before culling bypasses, including shadow candidates. Disabled instances contribute no
colour, depth, motion, shadow, selection outline or occluder coverage, and count only as disabled.
`AuthoredOff` is distinct from `CullingOff` and from frustum/HZB rejection. An enabled-state change
advances coverage invalidation and requests a temporal reset. Measurement freezes its starting
population and rejects edits to it.

Disabled local lights keep full identities and values. Disabled directionals retain their roles
and directions with zero contribution. A single document-selected caster receives shadowing;
disabling it changes contribution without changing pass declarations. The document's top-level
local-light group is the target of `--local-light-rig`; a document without one treats it as a no-op.

### Evidence contract

Capture manifest v3 and measurement schema 5 add the loaded document path and a SHA-256 over JSON
bytes followed by buffer bytes. This identifies the loaded snapshot, not unsaved live state.
PNG frame metadata and workspace schema 4 remain unchanged. The standing image reference uses
schema 2, checks document hashes before images, and covers Sponza, MaterialLab and TemporalLab in
five modes. The new lab images are an explicit re-baseline with owner acceptance pending.

## Consequences and limits

The catalog has six documents; Helmet remains a MaterialLab fixture and Truck a TemporalLab
fixture. Required fetched content or stale hashes fail with a named field. Khronos validation
checks catalog and writer outputs; deterministic save/load/save checks the Luminex model.

The runtime currently supports LINEAR selected-camera document rails, referenced asset clips and
generator motion. Other document animation targets and transformed generator roots fail before
GPU creation. Independent source clips can still combine singular scales outside the preflight's
sampled phases; the retained P2 counterexample is unresolved. Broader reader validity does not
promise runtime support. The original exact-image and orientation failures remain failed.

Create/delete/duplicate/reparent, asset import into a finalized scene, environment editing,
Undo/Redo, gizmos, picking, BLEND, skinning and morph targets remain outside this change.
