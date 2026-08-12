# ADR 0010: The RHI adopts an address-first per-frame data path; the object model stays

**Status**: Accepted (2026-08-12) · **Roadmap**: ../roadmap.md (M5.1) ·
**Evidence**: ../research/2026-08-12-execution-model-evidence.md ·
**Spec**: ../specs/2026-08-12-m5.1-rhi-execution-model-design.md

## Context

M5.1 ran a pre-registered experiment comparing the maintained object-shaped RHI against a
GPU-address-first prototype distilled from Sebastian Aaltonen's "No Graphics API" model, on one
frozen representative graph and frozen stress workloads, judged against gates and
change-proportional thresholds frozen before any measurement. The spec's rules: gates first;
material means a ≥25% repeatable difference beyond a 95% confidence bound (or an exact ≥25% count
difference); replacement requires broad wins with no material regression in any other core
dimension; ties and inconclusive results retain the incumbent; the smallest production change
that captures a proven win is preferred.

## Decision

**Partial reshape.** The production RHI adopts an address-first per-frame data path — root-data
delivery in the spirit of the prototype's `pushRoot` (caller-visible GPU addresses for per-draw
and per-pass blocks, suballocated from per-frame rings, no per-draw buffer objects and no
per-draw descriptor churn) — as a bounded change to the binding and uniform-delivery contract
area, preserving checkpoint A semantics. The object-shaped resource, pass, barrier, residency,
and pipeline model is retained, and the render graph keeps logical ownership.

This milestone migrates nothing. The adoption requires a separately accepted M5.x migration plan,
completed before M6, leaving no parallel API. The migration is a clean implementation in
production code; the experiment is not promoted.

**Disposal (frozen pre-measurement, spec section 5):** `Experiments/NoApi/` is retained
permanently as a non-default, `Frozen — non-normative` research artifact. It accepts no feature
growth and is not a supported API. Last verified environment: Apple M3 Max, macOS 26.5.2, Xcode
26.6, release configuration, all pipelines runtime-MSL.

## Evidence against the frozen thresholds

Wins (material, evidence section 2.3):

- **CPU encoding** — the core result: −96.2% on the representative graph, with a tight confidence
  interval excluding zero across 12 valid paired fresh-process repetitions. The two binding stress
  scales regress by 8.5% and 10.8%; both are below the frozen 25% material threshold.
- **Binding traffic (calls)** — −60.0% on the graph for the full prototype. Much of this comes from
  bindless/resource plumbing outside the adopted area, so it is not relied on for partial adoption.
- **Allocation (calls)** — the incumbent's per-frame data delivery costs 1,025 buffer creations:
  1,024 per-draw uniform buffers beyond its fixed 256 KiB ring capacity (evidence 7.1) plus one
  recreated upload-staging buffer because `rhi::Buffer` has no public write path. The prototype
  makes 9 for the whole run.

The bounded reshape qualifies on the graph's material CPU win: its dominant allocation/copy/bind
cliff is localized to per-frame data delivery, which is exactly the contract area being changed.
S-BIND does not confirm a general encode-time win, but its 8.5% and 10.8% CPU regressions are
immaterial. Its material call/byte regressions come from re-pushing two static roots and are not a
required property of the bounded change. Production must combine ring allocation, copying, and
address binding so one data-delivery operation does not become the prototype's two-call root path;
it also retains object-shaped resource binding rather than the prototype's bindless path.

Regressions that block replacement (spec section 10: replacement tolerates no material regression
in a core dimension):

- **Binding traffic on S-BIND** — +100% calls and nonzero per-frame root bytes against an
  incumbent that prebuilds its static data. A wholesale prototype adoption would import this
  regression; the bounded reshape requires a combined production data-delivery operation that
  preserves static reuse.
- **API surface** — the address-first model needed more, not less: +58% concepts, +33%
  operations, +108% caller-side arguments over the interface subsets the same workloads exercise.
  The model's supporting machinery (allocator, ring, handles, capabilities, result plumbing)
  outweighs the objects it removes at this project's scale.
- **Barriers** — mixed: −44% primitives on the composed graph, but +50–150% on isolated hazard
  cases, because stage-pair barriers cannot scope to a resource. A barrier-model replacement
  would import that regression; the reshape leaves the barrier model alone.

The S-BIND regression is avoidable within the bounded contract area through the required combined
operation; the API-surface and barrier regressions remain outside that area. The reshape preserves
static-data reuse and object-shaped resource bindings, adds only the per-frame/root-data path, and
does not touch the barrier model. The affected area's D3D12 mapping is native
(`Map`/`GetGPUVirtualAddress`, root CBV/SRV per draw) with only bounded, recorded emulation
(evidence 4.1), satisfying the partial-reshape condition; the D3D12 columns that carry
`unavailable`/`unknown` (specialization structs, capture address replay) sit outside the adopted
area and behind retained incumbent contracts.

Gates (evidence 2.4): all five pass, with one interpretation flagged rather than decided
silently: correctness parity is 30 of 32 frames byte-identical with a recorded, quantified 3-byte
single-ULP code-generation deviation whose mechanism is isolated (identical arithmetic, different
operand provenance; evidence 5.11/6.11). The roadmap's claims-scoped wording — "reproduces the
output … it claims to cover" — is satisfied; a strictly literal byte-identity reading would not
be. The owner disposition below accepts the claims-scoped reading; the strict-reading
counterfactual — under which the decision falls back to retain — is preserved here for the
record.

## Owner disposition (2026-08-12)

The project owner accepted this ADR and recorded the following, resolving the flagged points:

- Gate 1 is accepted under the roadmap's claims-scoped interpretation. The result is recorded
  accurately as 30 of 32 frames byte-identical with three bytes of isolated single-ULP
  code-generation rounding — never as strict byte identity — and the strict-reading
  counterfactual above is preserved in the record.
- The maintained RHI remains the production architecture. Only the address-first
  per-frame/root-data delivery path is adopted, through a separately accepted M5.x migration
  completed before M6.
- The object-shaped resource, pass, pipeline, residency, and barrier models and the render
  graph's ownership are retained.
- Static root data remains reusable; the migration must combine ring allocation, copying, and
  address binding rather than import S-BIND's two-call re-push behavior or bindless resource path.
- `Experiments/NoApi/` stays frozen, non-default, and non-normative; it must not become a
  supported parallel API.

## Consequences

- An M5.x migration plan must specify the production form of the address-first per-frame data
  path (ring capacity policy included, closing evidence 7.1's overflow cliff), keep checkpoint A
  green, avoid the frozen S-BIND traffic regressions for static data, and finish before M6 with no
  parallel API left.
- Incumbent findings recorded for later, outside this decision's scope: no per-mip raster
  attachment selection (evidence 7.3), undefined behavior on destroyed-handle use (7.4), no
  Metal-reported allocation-size query.
- Resident-memory comparison is inconclusive: the incumbent exposes no Metal-reported allocation
  sizes, and the frozen metric did not permit substituting logical requested bytes. The unscored
  requested-byte proxy is +76% for the prototype (evidence 3.3); it did not contribute to this
  decision. The model's scored API-surface growth remains counter-evidence at this codebase's
  scale, while the encode-cost claim is strongly confirmed.
