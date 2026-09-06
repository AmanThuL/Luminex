# Native submission host

The four supported modes share the same Metal 4 device setup, three frame slots, resource
capacities, binding path, and pinned Slang Scene/Prepare artifacts. Every replay sets `debug=0`.
No Metal implementation macros are defined here: the linked RHI owns their definitions.

Each slot owns its allocator, command buffer, argument table, mutable buffers and attachments.
The queue's exact last completion value protects CPU writes and allocator reset. Indirect
compute writes are explicitly ordered before vertex/fragment consumers. All reachable buffers
and textures belong to the queue residency set. Destruction drains submitted work, removes the
residency set, and destroys every native member inside an autorelease pool.

Verification commits additionally register completion-feedback callbacks. A CPU-only shared
ledger records every raster and readback submission, accepts out-of-order callback delivery,
and permanently retains the first GPU error. Slot waits and drain require delivery through the
requested sequence; any callback error is a failed result even when the shared event signals.
Successful verification still requires the normal event retirement proof. Teardown waits for
all submitted callbacks, including after errors, before releasing native resources. Complete
feedback for a failed workload permits teardown without waiting again for its failed event
signal. Missing callback delivery remains a fatal teardown condition: it does not authorize
releasing potentially in-flight resources.

Callbacks capture only a shared ledger plus immutable case/suite/mode/slot/frame/submission/kind
metadata. They never capture the host or slot by reference. Native error text is extracted inside
a callback-local autorelease pool, which drains before delivery is published; no NSError or other
native object escapes into the ledger. Errors log their domain, code and description immediately.
The ledger has no native ownership edge and therefore cannot form a queue/handler ownership cycle.
Its callback bitmap is reserved before warmup and included in requested CPU storage. When
`verify=false` and `diagnostics=false`, no ledger, commit options, callbacks, callback locks, or callback delivery waits
are created or executed. Feedback times are not exposed as a GPU-span feature.

`diagnostics=true` is an explicitly unscored alternative, incompatible with verification and
capture. It uses the same completion ledger but no readback submissions, canaries, image oracle,
or per-frame wait-idle. All three slots remain in use; exact-slot reuse waits additionally observe
that slot's feedback. Setup, warmup/replay boundaries, each submission identity and callback errors
are flushed to stderr. This observer can change CPU overhead and scheduling and is not performance
evidence. The CLI writes diagnostic-start.json before GPU work and diagnostic.json on a returned
outcome; it never writes a measurement result.json. A retired diagnosis is not an image-parity pass.

`Tests/VerificationFeedbackTests.cpp` exercises delayed/out-of-order delivery, persistent GPU
errors, still-pending callbacks after an error, shared callback ownership after submitter release,
and duplicate identities without linking Metal. These cases are included in
`GpuSubmissionTests/unit` and can be selected with `[feedback]`.

The scored lane is `headline`. `gpu-span` and `stages` fail with an explicit unavailable reason;
the host emits no timestamp markers. A command-buffer timestamp's placement does not prove a
workload boundary, so neither pass sums nor nominal boundary timestamps are reported as GPU span.

CPU work starts after the slot wait and ends after commit plus the retirement signal. It includes
allocator reset, input copies, visibility scan/culling, compaction, parameter writes, bindings,
and draw/dispatch encoding. CPU E uses a double-precision six-plane loop over inputs validated
before warmup; S scans the precomputed bitmap. E GPU receives no oracle bytes. CPU-indirect
packs visible records at the front and clears the remaining candidate capacity every frame;
`copiedBytes` includes all N records. This complete-capacity clearing is a charged conservative
implementation cost. Direct and batched also write their compact ID lists to shared memory.
Stage label strings and compaction storage are allocated before warmup. The frame autorelease
pool is opened before the slot wait and drained after timing; transient encoder release/pool
teardown is outside `cpuWorkNs` but inside sustained throughput for all modes.
Each submission queries `CommandAllocator::allocatedSize()` and updates its high-water mark
after stopping the CPU-work timer. This bookkeeping is inside the throughput window for every
mode. No allocation query is presented as part of GPU work or as resident-memory measurement.

Warmup visits all eight input phases repeatedly to reserve allocator capacity, drains fully,
then measured frames replay the specified 256-frame sequence. The normal 32-frame warmup
visits each phase in each slot; later native allocation growth invalidates the run. Throughput
starts after `endCommandBuffer`, immediately before the first measured queue commit, and ends
at final retirement, including
finite-window fill/drain effects. `setupMs` covers native object creation and shader/pipeline
loading, outside warmup, excluding input/oracle generation. `drainMs` covers the final retirement
wait already included in throughput. Verification throughput includes validation overhead and
is unscored. Scored runs reject validation/capture environment flags.

`requestedBytes` counts all three slots, shared colors, replay input vector capacities, result
capacity and validation images. `allocatedBytes` separately sums queried Metal resource sizes
and command allocator high-water allocations. Opaque device, library, pipeline and argument
table allocations are excluded and not presented as measured physical or resident memory.
`residentBytes` is unavailable. Requested storage over 256 MiB fails the case.

Validation waits for retirement, checks changing guard words before/after every mutable buffer,
decodes GPU argument records into visible IDs, checks every record's vertex range and instance
count, and checks complete CPU indirect capacity. Only after retirement does an untimed copy
read back RGBA8. Every frame is compared with direct rendering for its input phase: exact ID
sets, identical black-versus-covered pixels, and maximum channel difference 1/255. Targets clear
to black with alpha one and reversed depth zero. No correctness-only GPU writes are needed to
inspect the exact scored ordinary argument buffers.

Capture is started once, for the first measured verification frame in `runNative`, and stopped
after its retirement. `renderNativeFrame` never starts a capture. All labels start with
`lmx.submission.`. Beside `example.gputrace`, the host writes `example.gputrace.capture.json`,
its own schema with the captured slot/completion/frame, bindings, resource labels, GPU addresses,
data offsets, strides, requested/allocated sizes and attachment formats. Mutable buffer addresses
have a 256-byte prefix guard; use the sidecar's data offset when inspecting arguments. This is
not the production capture-schema format and requires no production tool modifications. Existing
capture or sidecar paths are never overwritten.

The native bundle's raw buffer blobs are replay initial state, not a guaranteed post-dispatch
snapshot. `Tools/inspect_capture.py` checks allocated blob lengths, canaries at requested offsets,
serialized labels, and possible visibility phases independently. In the initial sparse capture,
the argument blob matches warmup phase 6, while the captured dispatch consumes phase 0. This is
not a scored-retirement mismatch: the separate validation replay reads and checks phase 0 after
retirement. Do not claim final generated arguments were inspected in that trace without a replay
inspector. Preserve the capture gate as unavailable when that inspection is missing. Native trace
deduplication uses internal file symlinks; retain originals and use dereferenced copies when
importing into the stricter self-contained validation/collection bundle.

## Bounded ICB probe

Checked with the repository's pinned Slang `2026.14.1` on 2026-09-06. From the experiment worktree:

```sh
rtk proxy ThirdParty/slang/bin/slangc -version
rtk proxy ThirdParty/slang/bin/slangc Experiments/GpuSubmission/Shaders/Probe.slang -target metal -o /tmp/LuminexSubmissionOpaqueProbe.metal
```

The probe follows the native opaque-resource construction pattern: an argument block contains
one `command_buffer`, a thread constructs a local `render_command` from that object and a command
index, resets the command, and encodes a triangle with the five-argument `draw_primitives` call.
It uses Slang `ParameterBlock` syntax for the argument block. It does not model opaque command
storage as a byte array or a structured buffer of `render_command` values. Argument-block layout
and binding are still unverified because type checking fails before Metal lowering.

The exact result is exit **255**, with **E30015** for `command_buffer` at line 6,
`render_command` twice at line 17, and `primitive_type` at line 19. No MSL artifact was emitted.
Probe SHA-256: `e7126a337c5825eb96485a82ed3938c6f3289bd7dd99ae7db07cfd561c824e65`.
This establishes failure of direct first-class native-type use in this pinned compilation, not
failure of every possible Slang ICB implementation. The earlier storage-array probe's retained
log does not describe this source; retain a fresh compiler log for this revision.

### Inspected declarations and remaining interop route

The release ships the official target and interop guides under
`ThirdParty/slang/share/doc/slang/user-guide/`. The pinned
[Metal target guide](https://github.com/shader-slang/slang/blob/v2026.14.1/docs/user-guide/a2-02-metal-target-specific.md)
documents resource mappings, `ParameterBlock` argument buffers, and automatic Metal headers.
It does not list an ICB resource mapping. A case-insensitive search for command-buffer,
render-command, and indirect-command spellings found no ICB wrapper in the shipped
`lib/slang-standard-module-2026.14.1/**/*.slang` modules or the upstream pinned
[core declarations](https://github.com/shader-slang/slang/blob/v2026.14.1/source/slang/core.meta.slang)
and [HLSL declarations](https://github.com/shader-slang/slang/blob/v2026.14.1/source/slang/hlsl.meta.slang).
This names the search scope; it is not a universal absence claim.

There **is a documented interop mechanism** in the pinned
[target-code interop guide](https://github.com/shader-slang/slang/blob/v2026.14.1/docs/user-guide/a1-04-interop.md):
`__target_intrinsic` can map a Slang struct to a target type, `__intrinsic_asm` defines target
expressions, `__requirePrelude` supplies target declarations/includes, and `__target_switch`
supports `metal`. The guide identifies these as internal compiler features with incomplete
checking and possible breaking changes. These are general extension points, not a verified ICB
wrapper. They are a concrete route for future investigation, so unknown first-class type names
cannot justify declaring Metal ICB generation impossible in Slang.

An interop follow-up would need to establish opaque-type layout/resource classification,
argument-block binding, constructor/method lowering and any required Metal header inclusion,
then compile the emitted result and validate generation/execution, reset ordering, residency,
and three-slot reuse. This bounded probe does not inject target-language snippets, compile a new
compiler, or attempt that runtime path. Native ICB creation/execution and those correctness gates
remain unresolved; the host entry point in the pinned Metal headers is not runtime evidence.
Overall ICB status is **unresolved/unavailable for this experiment**, with the ordinary routes
unaffected. `Probe.slang` remains outside scored shader targets and artifact hashes.
