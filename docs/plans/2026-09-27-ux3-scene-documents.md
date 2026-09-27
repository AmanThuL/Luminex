# UX3 — Scene documents implementation

**Status**: In progress

> **For agentic workers:** REQUIRED SUB-SKILL: use superpowers:subagent-driven-development (or
> superpowers:executing-plans) task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** deliver the [UX3 record](../milestones/ux/ux3.md) (the spec, approved 2026-09-27; its
tables and slice gates are binding) as one pull request.
**Architecture:** a plain Asset document model read and written by Luminex code; Engine
instantiates it with a node binding; Scenes owns generators, the catalog and the pure save
function; the live scene stays the edit truth; decisions are pure functions tested first. Stack:
C++23, glm, cgltf (assets only), Catch2, Slang, ImGui, SDL3, AppKit, Python, xmake.

## Global constraints

- Parent `main` at `f181d8e`, built at `../Luminex-ux3-parent` for identity checks; branch
  `feat/ux3-scene-documents`; one pull request, squash-merged after owner acceptance. Commits follow
  `docs/conventions/commits.md` with `(UX3)` subjects; no AI co-author, session link or footer.
- Every commit builds and passes `xmake test -P . Tests/unit`, `xmake format -P . --check` and the
  six root checkers run from the worktree root (`AGENTS.md`, nested-worktree gotcha). Engine,
  Render and shader commits also pass `MTL_DEBUG_LAYER=1 xmake test -P . Tests/gpu`.
- Nothing under `RojoRHI/` changes; no new xrepo dependency; `AGENTS.md` ≤ 250 lines, pages ≤ 300.
- Document floats are written with `std::to_chars` shortest form and read with `std::from_chars`,
  never `atof`, `strtod` or the locale. `Orientation.cpp` builds with `-ffp-contract=off`.

## Review focus

1. A hand-edited, non-canonical catalog document loads clean and Save rewrites it canonically (15).
2. `--scene some/dir/copy.scene.gltf` resolves `uri`s under `Assets/` and records the path (7, 9).
3. Rig group saved off, relaunched with `--local-light-rig on`: lit this session, still clean (15).
4. Save As onto the open path or a read-only `.bin` fails, stays dirty, keeps old files (4, 17).
5. Quit or window close during a native dialog neither quits nor drops its result (17).

## Roles, waves and paths

The main thread (Opus 5) coordinates, reviews every result and runs owner gates. Opus 5 implements
Tasks 2–4, 6–8, 13–15 and 17, reviewed by a Fable 5 subagent; Sonnet 5 implements the rest, reviewed
by Opus 5. Work in the sibling worktree `../Luminex-ux3` (submodule initialised, `ThirdParty/*` and
`Assets/Fetched` symlinked, `-P .` on every xmake call); the controller commits serially.

| Wave | Tasks | Blocks on |
|---|---|---|
| W1 (UX3.1) | 1, 2; then 3, 4, 5 in parallel; then 6 | — |
| W2 (UX3.1) | 7, 8, 9, then the gate 10 | W1 |
| W3 (UX3.2) | 11, then 12 (owner re-baseline, blocking) | W2 |
| W4 (UX3.3) | 13 (may run beside W3), 14, 15 | W2 |
| W5 (UX3.4) | 16 and 17 in parallel; owner check ends 17 | W3, W4 |
| W6 (UX3.5) | 18 (after artwork approval), 19, 20 | W5 |

### Task 1: Start execution (main thread)
- [x] Create both worktrees. Record `Accepted`, this plan `In progress`, the roadmap step row and
  `AGENTS.md` (net zero) name UX3 in progress. Amend the record's units table: `SceneLook` lives in
  `Engine/Asset/Document` (Asset cannot include Scene) and the reader parses whole documents with
  `JsonTokens`, so `GltfLoader` grows only nodes and animations. Commit `docs: start UX3 execution`.

### Task 2: JSON writer and token reader (UX3.1, Opus)
**Files:** create `Source/Core/IO/JsonWriter.{h,cpp}`, `Source/Engine/Asset/Model/JsonTokens.h`;
`CgltfImplementation.cpp` there, moved from `GltfLoader.cpp`, defines `JsonTokens` too (jsmn is
`static`); tests `Tests/Core/IO/CoreJsonWriterTests.cpp`, `EngineJsonTokensTests.cpp`.
**Produces:** `JsonWriter` with `beginObject/endObject`, `beginArray(bool inline = false)/
endArray`, `key`, `string`, `number(float|double)`, `integer`, `boolean`, `take() -> std::string`
(two-space indent, one member per line, inline arrays on one line, final newline);
`formatShortest(float)`. `JsonNode` with type predicates, `find(key) -> std::optional<JsonNode>`,
`size`, `at`, `asFloat() -> std::expected<float, std::string>` (and `asDouble`, `asUInt`,
`asString`, `asBool`) and `path()`, a JSON pointer such as `/extensions/LMX_scene/look/bloom/
threshold`; `JsonTokens::parse(std::string) -> asset::AssetResult<JsonTokens>` and `root()`.
- [x] Tests first: a nested object writes exact golden bytes; `formatShortest` round-trips 100,000
  random finite floats via `std::from_chars`; a non-finite number asserts; an unterminated string
  reports its byte offset; `find` on a missing key is empty; a type mismatch names the pointer;
  `EngineGltfTests` pass after the move. Commit `core: add a byte-stable JSON writer (UX3)`.

### Task 3: Loader node table and every animation (UX3.1, Opus)
**Files:** `Source/Engine/Asset/Model/GltfLoader.{h,cpp}`, `EngineGltfTests.cpp`.
**Produces:** `struct GltfNode { std::string name; int32_t parent = -1; std::vector<uint32_t>
instances; bool animated = false; }`, `GltfInstance::node`, `GltfScene::nodes`; every animation
baked at `kAnimationBakeRate` onto one clock, `animationDuration` the longest.
- [x] Tests first: a two-primitive node yields two instances sharing `node`; names and parents
  match the file; InterpolationTest's clips all bake and STEP still steps; the Milk Truck's key
  count and key-byte digest equal the parent's. Commit `asset: keep glTF nodes (UX3)`.

### Task 4: Scene document model, reader and writer (UX3.1, Opus)
**Files:** create `Source/Engine/Asset/Document/{SceneDocument.h,SceneLook.h,SceneDocumentRead.cpp,
SceneDocumentWrite.cpp,Orientation.h,Orientation.cpp}`; `Source/Engine/Asset/xmake.lua`,
`Tools/module_contract.json`; tests `Tests/Engine/Asset/Document/`, golden
`Tests/Golden/scene-document-min.scene.gltf`. **Consumes:** Task 2.
**Produces:** the record's extension table as a model. `SceneLook { exposure; bloom; shadowFilter;
environment }` holds, with identical defaults, every exposure, bloom and filter field of
`EditorRenderSettings`; `Environment { std::array<uint8_t,3> skySrgb8; std::optional<Hdri> hdri; }`
mirrors `attachStudioEnvironment`'s arguments. `DocNode`, `DocLight`, `DocCamera`, `DocAnimation {
double sampleRate; uint32_t keyCount; channels }`, `ObjectPose { translation, eulerDegrees, scale
}`, `DocOverride { node; name; optional enabled; optional pose }`, `DocGenerator { std::string
name; ordered (name, double) params }`, `SceneDocument`. `readSceneDocument(path) ->
AssetResult<SceneDocument>`, `sceneDocumentJson(doc, bufferUri)`, `sceneDocumentBuffer(doc)`,
`saveSceneDocument(doc, path) -> AssetResult<void>` (temporaries, then renames),
`sceneDocumentHash(path)` (`.gltf` then `.bin` bytes). `Orientation.h`: conversions both ways,
`unwrapYaw(previous, yaw)`, `exactRotationForCamera(yaw, pitch, previousYaw)` and
`exactRotationForDirection(dir)` returning `std::optional<glm::quat>` after a ±4-ulp search per
component, `encodeStrength`/`decodeStrength` with a power-of-two intensity.
- [x] Tests first: the golden reads and rewrites byte-identically; each missing or mistyped
  `LMX_scene` field fails naming its pointer; a key time other than `float(k / sampleRate)` fails
  naming the animation and key; a short `.bin` fails naming the buffer; a point light without
  `range` is skipped with one warning while directional lights load; `encodeStrength` round-trips
  10,000 random strengths exactly; the searches succeed on Sponza-like samples and return
  `nullopt`, never a wrong value, on failure; a save onto a read-only target leaves the old files
  byte-identical (Review focus 4). Writer tests emit every output into `<test build
  dir>/SceneDocuments/`. Commit `asset: read and write scene documents (UX3)`.

### Task 5: Validator, canonical check and CI (UX3.1)
**Files:** `xmake/setup.lua`, `THIRD_PARTY_NOTICES.md`, `.github/workflows/ci.yml`; create
`Tools/Scenes/validate_documents.py`, `Tools/tests/test_validate_documents.py`,
`Tests/Scenes/SceneDocumentCanonicalTests.cpp`.
- [x] Setup pins `gltf_validator-2.0.0-dev.3.10-macos64.tar.xz` into `ThirdParty/glTF-Validator`
  in `slangc`'s pattern, clearing quarantine; archive and binary SHA-256:
  `bce89ceea00b4d3191a8779018f41744b84a4539b127465d63ba83ad0f243fef`,
  `4751098c84469231c4e06e2ba0f2fe472f3143a35725c7a168b595d2110d7eef`. The script validates
  `Assets/Scenes/*.scene.gltf` and `SceneDocuments/`, fails on any error and names Rosetta when the
  binary cannot start; its test parses canned reports. The canonical test requires each catalog
  document to equal its own rewrite. CI's build-test job installs Rosetta when absent and runs the
  script after unit tests. Commit `tool: validate scene documents (UX3)`.

### Task 6: One-time exporter and eight documents (UX3.1, Opus)
**Files:** create `Tools/SceneExport/{main.cpp,xmake.lua}` (non-default target, contract-listed for
this commit only), `Assets/Scenes/<id>.scene.{gltf,bin}` ×8. **Consumes:** Tasks 3, 4, C++ catalog.
- [x] Load each live scene through `SceneLibrary` and write: glTF scenes as one asset node (`uri`,
  SHA-256) with the scene camera, rails at `sampleRate` 60 with exact rotations and unwrapped yaw,
  and Sponza's 16 lights under a `Local Lights` group in rig order; labs as one generator node with
  default parameters; everywhere the key/fill/rim rig with roles, `castsShadow` on key, strengths
  through `encodeStrength` and exact directions; MaterialLab's rig `enabled: false` beside its HDRI;
  the default look; the scene label as the glTF scene `name`. Write `exporter-report.txt` listing
  each orientation as exact or unmatched, kept in `../Luminex-evidence/ux3/`; fail if the studio
  HDRI is missing. Commit `tool: export the catalog as scene documents (UX3)`; tag `ux3-exporter`.

### Task 7: Instantiation, binding and document catalog (UX3.1, Opus)
**Files:** create `Source/Engine/Scene/SceneInstantiate.{h,cpp}`, `Source/Scenes/
{SceneDocuments.h,SceneDocuments.cpp,Generators.cpp}`; modify `SceneLibrary.{h,cpp}`, `Scene.h`
(`asset::SceneLook look`; `DirectionalLight::enabled`, zero strength in the view when false),
`SceneSession`, `AppOptions.cpp`, the labs, `CatalogScenes.h`, `ScenePanel.cpp`, the tests naming
deleted loaders or `catalogIndex`; delete `SponzaLightRig.*`, `SponzaCameraTour.*`,
`GltfScenes.cpp`, San Miguel's rail, the C++ cameras and `Tools/SceneExport`.
**Produces:** `NodeBinding { std::vector<size_t> objects; std::optional<LightId> light;
std::optional<uint32_t> directional; bool camera; }`; `SceneBinding { nodes; objectNode (per
object; kGeneratedNode = UINT32_MAX when generated); lightNode (LightId value → node);
std::optional<uint32_t> localLightGroup; }`; `LoadedScene { std::unique_ptr<Scene> scene; binding;
asset::SceneDocument document; path; std::string hash; }`; `EnvironmentHook =
std::function<AssetResult<void>(Scene&)>`; `SceneGenerator = std::function<AssetResult<void>(
Scene&, const asset::DocGenerator&, const EnvironmentHook&)>`; `SceneGeneratorLookup =
std::function<const SceneGenerator*(std::string_view)>`; `instantiateSceneDocument(Device&,
asset::SceneDocument, path, const SceneGeneratorLookup&) -> AssetResult<LoadedScene>`. In Scenes:
`SceneId { std::string key; }` (catalog functions take `const SceneId&`) with `isCatalog()`,
`sceneIdFromPath`, `SceneLibrary::loaded(id) -> LoadedScene*`, `SessionDocumentState {
std::vector<bool> nodeEnabled; std::optional<engine::SceneCamera> sceneCamera; }` and
`initialDocumentState(const LoadedScene&)`.
- [ ] Tests first (pure): effective enabled is the AND over ancestors; an override name mismatch
  names both; a stale asset hash names `/nodes/<n>/extensions/LMX_scene/asset/sha256`; a missing
  HDRI names its `uri`; `uri`s resolve under `Assets/` wherever the document is (Review focus 2);
  `--scene <path>` parses; `--local-light-rig` is a no-op without a group; a pose override on an
  animated node fails. GPU (`[gpu][scene-doc]`): every catalog document loads; Sponza has 16
  lights and a 7,201-key rail.
- [ ] Keep the parent's order: nodes in document order, asset tracks rebased by the objects before
  them and asset clips setting the duration; the environment where each loader attached it (a
  generator calls `EnvironmentHook` where its lab called `attachNeutralEnvironment`); then document
  lights. Commit `scene: load the catalog from documents (UX3)`.

### Task 8: The look per scene (UX3.1, Opus)
**Files:** `EditorRenderSettings.h`, `EditorRenderDefaults`, `EditorShell.cpp` (`sceneView`),
`Headless/{Screenshot,Measurement}.cpp`, `RenderingTopics.cpp`, `ExposureReset`, tests.
- [ ] Tests first: exposure, bloom and shadow fields leave `EditorRenderSettings`; the view takes
  them from the active scene's look; a scene switch applies that scene's look and resets exposure
  when it flips auto exposure; the Exposure, Bloom
  and Shadows resets restore the loaded document's look; headless and editor views agree for one
  look; `LMX_SCREENSHOT_NO_BLOOM` still works. Rendering topics edit the active look with no
  visible change. Commit `editor: take the look from the scene (UX3)`.

### Task 9: Evidence formats and parity driver (UX3.1)
**Files:** `CaptureMetadata.{h,cpp}`, `MeasurementRun.h`, `MeasurementReport.cpp`,
`Tools/TemporalCompare/compare.py`, `Tools/Bench/{visibility_paired,lighting_paired}.py`; create
`Tools/Screenshots/parity_rounds.py`, `Tools/tests/test_parity_rounds.py`; tests.
- [ ] Tests first: manifest 3 and measurement schema 5 carry `sceneDocument: {path, sha256}` from
  `sceneDocumentHash`; a path-opened document records the path as given; PNG `lmx:frame` bytes
  equal the parent's; `localLightRig` defaults from the document's group. Readers accept manifests
  1–3 and reports 2–5 (lighting: parent 3 or 4, candidate 5), with `--selftest` covering them.
- [ ] `parity_rounds.py --parent-app --candidate-app --rounds 8 --output` alternates `parity.py`,
  fails a case whose candidate hash no parent round produced and writes `summary.json`; `--selftest`
  (listed in `ToolsTests.cpp`). Commit `app: record scene documents in evidence (UX3)`.

### Task 10: UX3.1 gate (main thread)
- [ ] At `--temporal off`, parent versus head BMP SHA-256 for eight scenes at `--frames 1` and
  Sponza and San Miguel at 600 and 3600; equal `LMX_GRAPH_DUMP`s; `parity_rounds.py` over fifteen
  cases; save-load-save; the validator; clean Metal validation. Record results in a new
  `docs/milestones/ux/ux3-validation.md`. **Owner checkpoint:** the parity report.

### Task 11: Fixtures join the labs (UX3.2)
**Files:** `Assets/Scenes/{material-lab,temporal-lab}.scene.gltf`, `SceneAnimation.{h,cpp}`
(`RigidTrack::loopDuration`, 0 meaning the scene clock), `MaterialLab.cpp`, `TemporalLab.cpp`, the
catalog, `AppOptionsTests.cpp`, `EngineSceneTests.cpp`, `ScenePlaybackTests.cpp`.
- [ ] Tests first: a track with `loopDuration` 2 samples at 2.5 s as at 0.5 s; asset clips carry
  their own duration. Add the helmet as an asset node beside MaterialLab's spheres with an axis
  annotation, and the truck beside TemporalLab's movers; retire both ids and documents; loader
  tests keep loading both assets. Commit `scene: fold the helmet and truck into the labs (UX3)`.

### Task 12: Re-baseline and matrix (UX3.2)
**Files:** `Tools/Screenshots/{reference.json,parity.py,compare.py}`, `docs/roadmap.md`, validation.
- [ ] `reference.json` schema 2 covers Sponza, MaterialLab and TemporalLab × five with a
  `documents` hash map; `parity.py` checks the hashes first and names a drifted scene. Capture
  parent and candidate images for both labs in five modes into the evidence folder; list every
  retired and new hash; Sponza still matches. The roadmap's "Sponza and Helmet integration checks"
  become Sponza and MaterialLab. **Owner checkpoint (blocking).** Commit `tool: re-baseline (UX3)`.

### Task 13: Disabled rows in classification (UX3.3, Opus)
**Files:** `SceneTables.{h,slang}` (`kInstanceDisabled = 4u`), `SceneFrame.cpp`, `Scene.h`
(`SceneObject::enabled`), `Visibility.{h,cpp}`, `VisibilityTables.h` (21 counter words),
`VisibilityClassify.slang`, `GpuVisibilityReadback.cpp`, `RendererVisibility.cpp`,
`OcclusionReference.cpp`, `SelectionOutline.cpp`, tests.
**Produces:** `VisibilityReason::CullingOff` (renamed) and `AuthoredOff`,
`VisibilityCounters::disabled`, `Scene::setObjectEnabled(size_t, bool)` bumping the coverage epoch.
- [ ] Tests first: CPU classification rejects a flagged row as `AuthoredOff` even with culling off
  or `viewUnculled`, counting it only as `disabled`; GPU (`[gpu][visibility]`) agrees with CPU on a
  population with disabled rows and emits no row or command for them; the shadow view and
  `OcclusionReference` skip them; a GPU image test disables and re-enables a Sponza pillar and
  regains the first digest at temporal off; with occlusion and its check on, toggling that pillar
  over frames reports no mismatch. Commit `render: exclude disabled instances from views (UX3)`.

### Task 14: Enabled state in the session and measurement (UX3.3, Opus)
**Files:** `SceneSession.{h,cpp}`, `TemporalEditorState`, `MeasurementRun.h`,
`MeasurementReport.cpp`, `EditorMeasurement.cpp`, `ScenePanel.cpp` (its inline reason strings move
to `EditorSelection.{h,cpp}`), tests.
**Produces:** `SceneSession::setNodeEnabled(uint32_t, bool)`, `setObjectEnabled(size_t, bool)`,
`nodeEnabled`, `isGenerated(EditorSubject, size_t, LightId)`, `documentState() -> const
scenes::SessionDocumentState&`, `editGeneration() -> uint64_t`; `editObject` on a bound node poses
every instance of that node; `visibilityStatusLabel(...)` → `Disabled`, `Culled: frustum`,
`Culled: occluded`.
- [ ] Tests first: a group toggle updates descendants and keeps their own flags; generated toggles
  stay session-only; toggles request a temporal reset; schema 5 records the starting population
  and per-frame `disabled`; Measure refuses enabled edits. Commit `scene: toggle nodes (UX3)`.

### Task 15: Save function and dirty state (UX3.3, Opus)
**Files:** create `Source/Scenes/SceneDocumentExport.{h,cpp}`, `Tests/Scenes/
SceneDocumentExportTests.cpp`.
**Produces:** `exportSceneDocument(const engine::LoadedScene&, const engine::Scene&, const
SessionDocumentState&) -> asset::SceneDocument`; `documentDirty(const asset::SceneDocument& loaded,
const asset::SceneDocument& exported) -> bool`, comparing writer outputs as bytes.
- [ ] Tests first: an unedited scene exports byte-identically; edit-then-restore and Play, Step,
  Stop stay clean; object, group and light toggles, a light edit and an exposure change are dirty
  and survive save and reload; generated edits and animated-node poses are never written; a CLI rig
  override stays clean (Review focus 3); a non-canonical file saves canonically (Review focus 1).
  Commit `scene: derive the saved document from the live scene (UX3)`.

### Task 16: Hierarchy tree and Inspector (UX3.4)
**Files:** create `Source/App/Model/Scene/SceneTree.{h,cpp}`, `AppSceneTreeTests.cpp`; modify
`EditorSelection.h` (`EditorSubject::Group`, `Environment`; `RenderingCategory` loses Exposure,
Bloom, Shadows), `ScenePanel.cpp`, `Inspector*.cpp`, `InspectorInternal.h`, `RenderingTopics.cpp`.
**Produces:** `SceneTreeRow { EditorSubject subject; size_t index; engine::LightId lightId; uint32_t
node; uint32_t depth; std::string label; bool group, generated, enabled, effective; }`;
`buildSceneTree(scene, binding, documentState, filter, const std::set<uint32_t>& collapsed)`.
- [ ] Tests first: document order; a match keeps its ancestors; counts include disabled rows;
  generated children list under their lab. Draw muted rows with an "off" marker, culled cues as
  today and a `dirty` argument's `*` on the root. Commit `editor: show the document tree (UX3)`.
- [ ] Group, object and light headers carry Enabled; generated subjects read "Generated by <lab> ·
  not saved"; the Environment subject owns Exposure, Bloom and Shadows with document resets and
  shows Sky and IBL read-only. Commit `editor: edit enabled state and the look (UX3)`.

### Task 17: Document workflow (UX3.4, Opus)
**Files:** create `Source/App/Model/Scene/DocumentWorkflow.{h,cpp}`, `AppDocumentWorkflowTests.cpp`;
modify `EditorMenus.cpp`, `EditorShell.{h,cpp}`, `main.cpp` (quit, close), `SceneSession`.
**Produces:** enums `DocumentAction { Open, OpenCatalog, Save, SaveAs, Revert, Quit }`,
`ConfirmChoice { Save, Discard, Cancel }`, `WorkflowStep { Idle, Confirm, ChoosePath, Ready }`;
`PendingDocumentWork { action; optional SceneId target; optional path; bool saveFirst; }`;
`DocumentWorkflow` with `request(action, target = {})`, `confirm(ConfirmChoice)`,
`pathChosen(std::optional<path>)`, `takeWork() -> std::optional<PendingDocumentWork>`, `step()`,
static `unavailableReason(action, bool stopped, bool measuring) -> std::optional<std::string>`.
- [ ] Tests first: each action on clean and dirty documents; Cancel changes nothing; a failed save
  stays dirty and aborts the pending switch; Quit waits for an open dialog (Review focus 5); Save,
  Save As and Revert are unavailable while playing or measuring. Wire Open… and Save As… through
  `SDL_ShowOpenFileDialog`/`SDL_ShowSaveFileDialog` with results queued under a mutex, ⌘S and ⌘⇧S,
  the modal confirmation, notices, the title `Sponza* — Luminex`, View > Set Scene Camera from View
  (`SceneSession::setSceneCamera`); recompute `documentDirty` when `editGeneration()` changes.
  Commit `editor: open, save and revert scene documents (UX3)`.
- [ ] **Owner check (UX3.4):** the UX1 completion tasks on the new layout; disable an object, edit
  a light and exposure, save, relaunch, find all three; Revert; open a schema 4 workspace. Record
  unverified gestures as unverified. Only then does W6 start.

### Task 18: Application icon (UX3.5)
**Files:** `Assets/Icons/{luminex-icon.svg,luminex-mark.svg,luminex-icon-1024.png}`; `Source/App/
xmake.lua` (stage `Icons/`, `add_files("Shell/*.mm")`, `add_frameworks("AppKit")`); create
`Shell/AppIcon.{h,mm}` with `applyApplicationIcon(const std::filesystem::path& png)`, called only
from `runWindowed`.
- [ ] **Owner checkpoint (blocking):** final artwork. A missing PNG logs one warning; a fresh launch
  shows the icon in the Dock and switcher. Commit `app: show the application icon (UX3)`.

### Task 19: Decision and operator documents (UX3.5)
- [ ] ADR 0028 (document contract, enabled semantics, amendment to ADR 0021) `Proposed`;
  `docs/architecture/{engine,app}.md`, new `docs/guides/scene-documents.md`, `AGENTS.md` commands
  and architecture lines, README's scene list; written with the `humanizer` skill loaded; policy
  passes. Commit `docs: describe scene documents (UX3)`.

### Task 20: Acceptance and closure (main thread)
- [ ] Every slice gate together on the head; `MTL_DEBUG_LAYER=1 xmake test -P . Tests/gpu`;
  evidence in `../Luminex-evidence/ux3/`. On owner acceptance: ADR 0028 `Accepted`, record
  `Implemented` with dated behaviour and limits, plan removed, roadmap and `AGENTS.md` updated, PR.
