# Engineering Conventions

**Status**: Accepted

These rules define how renderer changes become part of the maintained baseline. More specific
rules live in `cpp-style.md`, `shader-style.md`, `commits.md`, and `documentation.md`.

## Change shape

- Land one observable outcome at a time. Keep implementation, tests, nearby documentation, and
  required asset metadata in the same change.
- Prefer a vertical slice through existing boundaries to a speculative abstraction. An interface
  must have a current caller and at least two credible implementations or a concrete portability
  constraint.
- Keep `main` runnable. Incomplete large features remain disabled or explicitly selectable; they do
  not leave half-adopted contracts in the default frame.
- Remove obsolete paths when their replacement takes ownership. Do not retain parallel APIs without
  a named compatibility requirement and removal condition.

## Ownership and lifetime

- Every resource-owning type has one visible owner and a stated destruction boundary. Borrowed
  views cannot outlive the owner that produced them.
- Cross-frame GPU data states its frame slot, recycle condition, and CPU/GPU synchronization rule.
  Three frames in flight is a checked invariant, not scheduling folklore.
- Public RHI headers contain no backend types. Backend-specific handles and workarounds remain in
  the backend implementation.
- CPU asset decode and validation complete in the Engine domain. GPU creation and upload happen on
  the render-owning thread, with domain errors translated at the App boundary.
- Module ownership and the dependencies each unit may take are defined by the
  [module contract](modules.md) and checked by policy, not by review.

## Graphics contracts

- Names or nearby contracts identify coordinate space, handedness, units, normalization, and color
  encoding whenever ambiguity could change a result.
- Authored colors enter linear-light math through an explicit decode. Display encoding happens once
  at the output boundary; hardware clear behavior is documented separately where applicable.
- CPU and shader structures use fixed-width fields, explicit alignment, and compile-time size/offset
  checks. A shader-layout change updates both sides and an ABI test in one change.
- Render passes state attachment formats, load/store behavior, resource state, and debug label.
  GPU captures must identify meaningful passes rather than a generic encoder.

## Failures and contracts

- Return `Result<T>` for expected boundary failures such as invalid content, missing files, shader
  compilation, or GPU object creation. Error values belong to the domain that can explain them.
- Use `LMX_ASSERT` for programmer misuse and impossible internal states. Assertions do not replace
  validation of external assets or user input.
- Preserve causal detail when translating errors. A user-facing message says what failed and how to
  recover without exposing implementation chronology.

## Validation evidence

Choose evidence by risk, not file extension:

| Change | Minimum evidence |
|---|---|
| Documentation or policy | `xmake policy`; relevant link/manual review |
| CPU behavior | format, focused test, full unit suite |
| RHI or shader ABI | build, unit tests, GPU smoke test with Metal validation |
| Rendered output | fixed-camera image comparison and validation-clean run |
| Synchronization or lifetime | stress/recycle test and labeled GPU capture |
| Performance claim | repeatable command, device/configuration, multiple samples, raw measurement |

Review the smallest useful diff. Generated evidence and measurements do not become source comments;
record durable conclusions in an ADR, guide, milestone record, or postmortem.

## Render stages

A stage creates GPU objects at construction and declares passes each frame. Shared mechanics live
in `Render/Common`; shader mirror structs, binding slots and rendering policy stay with the stage.

1. Provide `static rojoRHI::Result<std::unique_ptr<X>> create(rojoRHI::Device&, ...)`, keep the
   constructor private and complete initialization in that one call. Additional creation arguments
   describe formats, owned extents or readback requirements.
2. Use one `declare(RenderGraph&, rojoRHI::CommandList&, [const SceneView&,] const XInputs&)`,
   returning the graph handle or outputs it produces, or void when it writes an input target.
   More than three arguments after commands require an inputs struct; mutable status remains an
   explicit separate output argument when needed.
3. Only extent-owning stages expose resize. Document whether equal extents preserve resource
   identity and history or recreate them; callers' reset semantics decide this policy. Use three
   paced slots for frame data and two entries for ping-pong history.
4. Propagate expected failures as `auto x = ...; if (!x) { return std::unexpected(x.error()); }`.
   Keep a local scope when it preserves the old result object's destruction boundary.
5. Label every library, pipeline and resource. New pipelines use `lmx.render.<name>Pipeline` and
   new passes `lmx.pass.<family>.<step>`. Existing capture labels remain frozen.
6. Reuse the free functions and templates in Common for repeated setup, resource resolution,
   dispatch, paced retirement and draw encoding. No stage base class or virtual dispatch is needed.

Named exceptions: VendorTemporalScaler is an adapter with an infallible constructor; its fallible
prepare reports native fallback, and declarePack remains beside declare. ExposureStage keeps
separate declareSeed and declareMetering around the scene. OcclusionReference::declare returns
Result<void> because it allocates per frame. Existing pipeline labels under `lmx.visibility.*`,
`lmx.light.*`, `lmx.selection.*`, `lmx.occlusion.reference.*` and `lmx.pipeline.temporal.vendor.*`
remain unchanged. SelectionOutline and OcclusionReference retain their independent direct-item
loops; they share scene-table binding, while draw-run encoding belongs to Scene and Shadow.

R3.5 measured adoption exception: Scene retains `encodeDrawRuns` and all four stages retain `bindSceneTables`; Shadow keeps its explicit run loop after the fixed fallback selected the measured Scene-only candidate. The [validation record](../milestones/r/r3.5-validation.md) owns the frozen paired decision.
