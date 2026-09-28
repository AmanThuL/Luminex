# UX3 final integrated validation

**Status**: Implemented (owner acceptance pending)

This closes the executor plan for [UX3](ux3.md). All final automated runs completed on
2026-09-29. Build, contract, Metal, document and catalog-smoke checks pass. The three image
gates remain failed. The [original validation](ux3-validation.md) and
[editor/icon validation](ux3-editor-validation.md) retain earlier failures and limits.
The lab re-baseline, provisional artwork and final behavior await owner acceptance;
[ADR 0028](../../decisions/0028-scene-document-contract.md) remains Proposed. No merge is authorized.

## Tested source and evidence

The annotated tag `ux3-validation` names implementation revision
`7875edd3add62e2b26eb40b26c398e59e8128310`. The later documentation-only closure updates this
record and removes the completed plan; the measurements below belong to that tagged revision.
Tag `ux3-exporter` remains at `d5f440abe9f7f6f81548b2c3d1f675a7acaac086` and retains the
original migration implementation.

Evidence follows the [archive convention](../../guides/evidence-archive.md) and lives under
`../Luminex-evidence/ux3/`, relative to the repository root. The
`task20-final-identity.json`,
`task20/common-manifest.json` and
`task20/independent-final-review.md`
record source, binary, shader, fixture and reference identities. Review passes for completeness,
provenance and result interpretation; it does not pass the image or native visual gates.

| Identity | Value |
|---|---|
| Frozen parent source | `f181d8e50a3448938fec51bdd8bfc3ef44297a65` |
| Candidate App SHA-256 | `1b2e655d1b696d129eaa7132fb30beed27fe1410c3adbc0d5ce502ef47ed481a` |
| Parent App SHA-256 | `3b41d4b8401b1cd99887f66cc86d6da01f1addbb48550e5578af1f9d9de8487f` |
| Common manifest SHA-256 | `deb57a8e019fb38a46fe4f07dd233180d9ade133fef8a9852865ebf95a054673` |
| Unchanged RojoRHI pin | `8da2a79e82ef66ed67d9642f0f3c5a74d20c33a8` |

Release builds ran on Apple M3 Max, macOS 26.7 (25G229), Xcode 26.6 (17F113).
GPU work held the shared lock with `MTL_DEBUG_LAYER=1`; screenshot runs left
`LMX_SCREENSHOT_NO_BLOOM` unset. The source stayed frozen through core, CPU and GPU phases.
Independent rehashing covered 927 tracked entries, 319 frozen inputs and 129 core build
products before and after the extras, with no mismatches. Both extras phases reference the
same manifest and candidate App. No performance conclusion follows from these runs.

## Completed gates

The core command list is in
`task20-final-gates.json`.
All eleven commands pass: `xmake -P . -j 6`, `xmake test -P . Tests/unit`,
`xmake format -P . --check`, compile-database generation, six direct root checkers and
`MTL_DEBUG_LAYER=1 xmake test -P . Tests/gpu`. The separate source-freeze assertion also passes.
Direct root checkers avoid the nested-worktree `xmake policy` root-resolution issue.

During the later documentation closure, project policy exited 1 before the repair landed
(`task20-closure-04.log`): three Status values used unsupported semicolon syntax, and six
Markdown links escaped the repository to the evidence directory. The Status values now use
`Implemented (owner acceptance pending)`, and evidence references use exact paths under the
stated evidence root. The focused policy rerun, including `--commits origin/main..HEAD`, passes;
`task20/closure-edit-checks.json` retains the initial findings and repair checks. This recovery
does not claim completion of the separate eleven-command closure run.

| Gate | Final result | Evidence under the evidence root |
|---|---|---|
| Core build, full CPU suite, format, six root checkers and Metal GPU suite | PASS: 11 commands plus source-freeze assertion | `task20-final-gates.json` |
| CPU extras | PASS: 10 commands and 2 byte audits | `task20/cpu/summary.json`, `task20/cpu/commands.json` |
| Python tools | PASS: 246 tests | `task20/cpu/python-suite.log` |
| TemporalCompare | PASS: 13 tests, 1 skipped | `task20/cpu/temporal-compare-suite.log` |
| Khronos validation | PASS: 84 documents, 6 catalog plus 78 writer outputs | `task20/cpu/document-validator.log` |
| Original document read/save/read/save | PASS: 8/8 pairs equal original bytes and both saved generations | `task20/cpu/roundtrip-original-summary.json` |
| Current catalog read/save/read/save | PASS: 6/6 pairs equal original bytes and both saved generations | `task20/cpu/roundtrip-current-summary.json` |
| Six current scenes, temporal Off/frame 1 | PASS: 6 captures, valid 1280×720 BMPs and graph dumps | `task20/gpu/current-six/summary.json` |
| Original eight scenes plus Sponza/San Miguel frames 600/3600 | FAIL: 1/12 exact images; PASS: 12/12 graph dumps | `task20/gpu/original-off/summary.json` |
| Original fifteen-case parent-union comparison | FAIL: 5/15 across 8 alternating rounds, 16 batches and 240 captures | `task20/gpu/original-matrix-rounds/summary.json` |
| Current schema-2 reference matrix | FAIL: 10/15 exact, both labs pass all modes; Sponza fails all five | `task20/gpu/current-matrix/parity.json` |

The commands in `task20/cpu/commands.json` also pass shader imports, module link dependencies,
RojoRHI unit tests and the frozen checkpoint inventory.
The roundtrip helper links the frozen Asset/Core archives and calls the actual APIs.
Independent review recomputed all 28 JSON/bin file triples: original, first save and second
save agree. This establishes byte equality for these fixtures. The TemporalCompare log records
one skip; it does not provide the reason.

## Image results and interpretation

All renderer capture subprocesses exit 0. The
`task20/gpu/summary.json` has fifteen rows:
six smoke commands and six smoke verifications pass; three image-comparison gates exit 1.
The final result therefore retains three failed image gates, with complete captures.

The temporal-Off comparison uses fresh candidate captures of the archived eight documents and
retained frozen-parent captures. Only MaterialLab frame 1 has identical BMP bytes. All twelve
graph dumps match, but `LMX_GRAPH_DUMP` records the first compiled frame, so the long captures do
not establish graph equality at their frame-600/3600 endpoints.

The original matrix alternates parent/candidate order across eight rounds. Only MaterialLab's
five modes have every candidate hash in the corresponding parent hash union. The ten
Sponza/Helmet cases fail. Each of the sixteen batches separately scores 0/15 against the
historical reference, including every parent batch. That historical mismatch does not change
the parent-union result. The current matrix has ten lab matches and five Sponza failures against
the unchanged schema-2 reference. No threshold, tolerance or reference was changed for Task 20.

Independent review hashed the retained captures and scanned 292 GPU evidence logs plus the
core unit/GPU logs for Metal/validation errors, error-level lines, assertion failures and crash
signatures; none matched. This scan describes those runs and their recorded process results.

## Retained limits and acceptance

The exporter still has 496/10,845 orientation misses. The parent rig's non-unit directional
values and exhausted rail-key search remain recorded; they do not explain every pixel mismatch.
The original image gates stay failed despite the later lab re-baseline. The unchanged Sponza
historical hashes also remain failed.

Task 11's independent-loop STEP-scale P2 remains unresolved: ancestor/child phases can combine
into an indecomposable pose. It is a source-reviewed counterexample, not an executed failure in
the passing suite; the catalog Truck does not use that pattern. Runtime document-animation and
generator-transform restrictions remain as recorded in the original validation. Ordinary Save
rollback does not establish crash atomicity, and post-write verification failure can leave a
completed disk save without in-memory adoption.

Native verification remains 62/74 gestures across maximized and 1280×720 windows, with twelve
unverified rows. The ledger combines identified launches and repair rechecks; the full workflow
was not replayed on `7875edd`. Save/relaunch/Revert and schema-4 workspace restoration passed at
both sizes. Dock and switcher appearance remain unverified, and the FACET artwork is provisional.
The evidence-only QA app adapter is not product packaging. See the
[editor/icon record](ux3-editor-validation.md) for each missing gesture and launch identity.

Owner decisions remain pending for the lab re-baseline, provisional artwork and final behavior.
Implementation closure does not amend the design's exit gates, accept the retained failures,
advance ADR 0028 or adopt a new rendering tolerance or performance claim.
