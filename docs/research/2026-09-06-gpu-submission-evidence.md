# GPU work-submission implementation and interrupted attempt

**Status**: Frozen — non-normative

**Date:** 2026-09-06. This record concerns the isolated native Metal 4 workload, not production
renderer performance. The [roadmap](../roadmap.md) owns sequencing and
[ADR 0012](../decisions/0012-gpu-submission-defer.md) records the proposed adoption disposition.

## Disposition

**M5.6 is not complete.** Its four-mode implementation and pre-collection checks are in place,
but stage 5 acceptance is blocked after global GPU restarts and retirement failures. The partial
campaign is not accepted performance evidence. The decision proposal is defer; no production
adoption follows. GPU-span and capture gates are also incomplete, and ICB is unresolved.
Production remains at the M5.5 rendering/runtime baseline; the active plan stays In progress.

The planned 1,920 supported pairs were interrupted: 1,043 structurally valid pairs, two failed
processes and one operator-interrupted pair remain, alongside 2,028 explicit unavailable records.
The remaining 2,686 scheduled jobs were not entered. The full-schedule report is complete=false:
85 structurally complete cells, 169 unavailable and 226 invalid. Missing jobs are not fabricated
as completed unsupported probes. No break-even or speedup finding is accepted from this attempt.

| Failure record | Case | Suite / pair / order | Outcome |
|---|---|---|---|
| p02932 | n16384-t32-v50-b1 | S / gpu-args,batched / AB | Retirement/teardown assertion; exit -6 |
| p03037 | n16384-t32-v50-b1 | E / gpu-args,direct / BA | Retirement/teardown assertion; exit -6 |
| p03073 | n16384-t32-v50-b1 | E / gpu-args,batched / BA | Operator-interrupted at safety stop; no receipt |

Kernel logs show global restarts at 18:13:52.280, 18:16:01.747 and 18:18:12.402 (Asia/Shanghai).
The last overlaps the safety-stop interval; its precise causal relation to interruption is unknown.
The process sample waits in IOSurfaceSharedEvent. Neither pair order nor these logs identifies
the faulting mode or proves defective hardware. All attempts are retained; no pair was replaced.

## Frozen comparison

The [design](../specs/2026-09-06-m5.6-gpu-work-submission-design.md) preceded collection. Its
20-case matrix varies N=1,024/16,384/65,536, T=2/32, visible fraction 0.1/0.5/1, B=1; two extra
cases use B=16/64 at N=16,384/T=2/visibility=0.5. Diagnostic quad geometry, shader artifacts,
synthetic linear colors and 1024-square targets are shared. Bins are not heterogeneous materials.
The 256-frame sequence exchanges visible IDs every 32 frames; tiny objects are still checked by
ID even when they do not cover a pixel. Input generation is untimed; per-frame transfers are not.

| Mode | Work included | CPU draw calls per frame |
|---|---|---|
| direct | CPU visibility/ID preparation and direct submission | V |
| cpu-indirect | CPU visibility, clear full N-capacity argument range, pack visible records | V |
| gpu-args | GPU argument preparation, including zero-count invisible records | N |
| batched | CPU visibility and per-bin ID compaction, instanced shader lookup | Nonempty bins |

Suite S consumes a precomputed visibility bitmap; suite E includes CPU or GPU frustum tests.
GPU E receives bounds/transforms/planes, not the CPU oracle bitmap, ID list or visible count.
The CPU E control performs a direct double-precision six-plane loop over validated input, rather
than revalidating the camera for every object. Native modes use the same mapped storage/bindings;
the unchanged public RHI adapter is an untimed graph/image/argument correctness anchor only.

Each supported comparison requires 12 fresh-process pairs, alternating AB/BA; each mode gets 32 warmup
and 256 measured frames. Each repetition reduces frame durations to a median; 10,000 fixed-seed
bootstrap resamples operate on the 12 paired relative deltas, not on individual frames. Positive
duration deltas mean less time; positive throughput deltas mean more completed frames per second.
All losses, unavailable rows and failed attempts remain in the corpus; no outlier removal occurs.

CPU work starts after the exact slot wait and includes copies, culling, packing, allocator reset,
bindings, encoding and queue commit/signal. Sustained throughput spans first measured submission
through last measured retirement, including finite-window fill/drain and slot waits. It is not
reciprocal CPU encoding time or frame latency. Autorelease-pool teardown and allocation high-water
queries are outside CPU work but inside throughput equally across modes. Compilation/setup is
reported separately. No GPU timestamp, readback or validation callback ledger enters headline runs.

## Environment and identities

Apple M3 Max (40 GPU cores), 64 GB unified memory, Mac15,9; macOS 26.5.2 (25F84), Darwin 25.5.0;
Apple clang 21.0.0 (clang-2100.1.1.101), pinned Slang 2026.14.1 and metal-cpp 26.4. Release build,
Metal 4 only, three frames in flight. Offline MetalToolchain was absent; the generated MSL was
compiled at runtime during setup, outside steady-state timing. Process startup does not imply
a cold driver shader cache. No other Luminex GPU tests/builds were run alongside collection.

Per-process environment identities verify validation/capture/shader-validation flags are off.
The driver retains AC/power and thermal snapshots before/after its session, not continuous
telemetry. Device residency is unmeasured/null. Requested CPU/GPU storage and queryable GPU
resource/allocator allocations are distinct; opaque pipeline/table/device costs are excluded
from the latter and are not described as total physical memory.

| Artifact | SHA-256 |
|---|---|
| Final executable | `7dcc36ffeb60eb469b7b1c1f7175abbc78a30889252e7af54e3421bafa199a42` |
| Shader bundle | `ae045f9a07b7a002e899f9e6b25c3936c4ef537bd0b328311d347304ec10bd1c` |
| Protocol | `9bd5fad28c86fda168eadfda4cc0097beeeb6bc425bfc7b5c45c5265fa68060c` |
| Paired driver | `94e65b4ff671ee1282894b98ece6a3ccdf32a02b5ea5a2f261e714cf4fd4d36a` |
| Result schema | `e59e0e3669b5b8ff4d4ab333c068d68239ca9b5ee603d4e4415d4be6beb50a60` |

Manifest files have cryptographic outer hashes. Their inner diagnostic FNV fingerprints are
explicitly noncryptographic. The collection freezes executable, driver, schema, capabilities,
environment and referenced validation contents before its first pair. Exploratory large-pair
results use an earlier executable and are not scored. The final two-pair smoke is also unscored.

## Correctness, lifetime and observations

Pre-collection scored-artifact replay passed all 160 case/suite/mode combinations for 256 frames each,
with exact visible IDs, exact coverage and at most 1/255 channel error against native direct.
The public RHI reference checks all eight input phases, graph dependencies/barriers and retired
images. A separate 900-frame E replay passed in all four modes on the changing-visibility
N=16,384/T=2/B=64 case. Canary checks cover mutable buffers and slots are reused only after exact
retirement. No post-warmup allocation growth was accepted.

Verification enables Metal API validation, but not shader validation. Its synchronous readback
submissions and feedback waits reduce frame-slot overlap, so these passes do not certify the
uninterrupted no-verify schedule that later failed. No GPU tests were rerun after the safety stop.

| Validation | Cases | Assertions |
|---|---:|---:|
| Production unit | 405 | 77,439 |
| Production GPU, Metal validation | 97 | 11,395 |
| Frozen checkpoint A, Metal validation | 19 | 1,856 |
| Experiment CPU | 21 | 37,011 |
| Experiment GPU, Metal validation | 6 | 54,105 |

Production App and Tests were rebuilt. Production format/policy/public-header/layout checks
passed; experiment code has its own focused format/compiler-backed policy check. The portable
Python suites cover CLI rejection (6 tests), statistical/adoption rules (33), evidence assembly
(38) and safe archives (16). These CPU-only tests passed again after the stop. Production App/Tests
also built again; that is a build result, not post-reset runtime certification.

**Retained failure:** an earlier long verification process stopped after 84 complete groups and
encountered an IOGPU fault at N=16,384/T=32/visibility=0.5/B=1, E/direct. Kernel diagnostics and
the timed-out process sample are retained. A scoped rerun passed. Verify-only completion-feedback
bookkeeping was added to surface callback errors, order-independent completion and safe teardown;
the final full replay and stress passed. The root cause is unknown. Successful reruns do not
prove the driver fault was fixed; this is a robustness limitation, not a discarded outlier.

Independent post-stop source audits found no demonstrated allocator/slot/argument-table lifetime
violation, shader ABI mismatch or input-generation overflow. Virtual vertices, packed 32-byte
instances, 192-byte Params at 256-byte strides and 16-byte arguments remain bounded if runtime
inputs/addresses are valid. The vertex shader trusts object/bin indices; canaries cannot prove
absence of out-of-bounds reads. Fault-time GPU memory and downstream machine code were not
inspected. These conditional risks are not a root-cause diagnosis or a justification for a
speculative fix. New diagnosis must preserve failing overlap and log mode/slot/submission identity.

**Capture limitation:** empty, sparse and dense captures ran validation-clean with the final
binary. Serialized labels, decoded color targets and source/emitted-MSL binding/residency paths
were inspected. The sparse raw argument blob matches warmup phase 6, not captured phase 0:
it is replay initial state rather than inspected post-compute output. Allocation-rounded blob
lengths and guards at requested offsets matter to decoding. Dense all-visible values cannot
distinguish these phases. Separate actual post-retirement arguments passed verification, but do
not satisfy the missing trace-replay inspection. Therefore capture remains unavailable. Depth
uses DontCare store; undefined retained depth contents are not treated as evidence of bad geometry.

**Counter limitation:** GPU-span/stage instrumentation is not implemented/proven; no markers are
emitted. Workload-boundary and marker-overhead comparisons remain unavailable, not zero. This
does not establish that the hardware cannot supply useful timing.

**ICB limitation:** the bounded probe with pinned Slang reports E30015 for `command_buffer`,
`render_command` and `primitive_type`. Native host entry points in headers do not prove shader
lowering, reset/inheritance/residency correctness or runtime execution. No compiler upgrade,
vendored patch, hand-written MSL or target-code injection was used. The experiment's Metal README
retains exact diagnostics and scope: this is unresolved, not proof that every Slang encoding is
impossible. ICB has no implemented production graph coverage here.

## Reproduction and custody

The external evidence bundle is identified by basename `luminex-m5.6-evidence.ltlLq0`; it is local
and is not claimed to be uploaded to a public artifact service. Its final artifact index records
per-file sizes/hashes and native trace aliases. Native trace aliases are retained unchanged;
`portable-captures/` contains new dereferenced copies for strict collection import.

`report-interrupted/` and `report-interrupted-reproduced/` contain byte-identical versions of all
seven outputs (frames/pairs/summary JSONL, summary JSON/CSV/Markdown and runs CSV), regenerated
independently from the retained raw prefix. Both correctly return incomplete. Key SHA-256 values:

| Report artifact | SHA-256 |
|---|---|
| summary.json | `b564b6a7ecc3abd6ff597f69a9172939cb0f4522ad893598ce5d93e1ce56bd6c` |
| summary.csv | `a2558213f3867d85c45a71ac195aa98e2ed00cb3e2db32baa3bf903277db77d7` |
| pairs.jsonl | `6df59507b625a55edc65a13c7913181acf31c57848ec5996cdc3f51bc39c3ed8` |

`artifact-index.json` inventories the external bundle, including failure/system/sample logs,
collection-interruption.md, source-audit-after-stop.md and all raw prefixes. The attempt snapshot
tag is `m5.6-gpu-submission-attempt-2026-09-06`; it is not the reserved final evidence tag.

Use a separate checkout of the attempt tag, run `xmake setup`, then build
`GpuSubmissionBench` and `GpuSubmissionTests`. The experiment README documents verification,
capture limitations, evidence assembly and collection. With retained raw data:

```sh
python3 Experiments/GpuSubmission/Tools/paired.py --summarize <bundle>/collection --output <new-report>
```

Do not restart GPU collection on a shared device without arranging an isolated test session/host.
Rebuilt executables may have a different binary identity. For a fresh measurement, rerun validation
and freeze all identities, then collect into a new directory; do not splice old verification into
a new executable or replace unfavorable pairs. The immutable attempt tag never receives feature
work. Stage 5 acceptance, stage 6 closure, final evidence tag and conclusions-only production
integration remain pending. Experimental source, targets and dependency changes stay off main.

## Project-fit update

The earlier [ranking](2026-09-06-graphics-paradigm-project-fit.md) remains a dated assessment.
This lab implemented the first execution slice and exposed reliability/compiler/observation gates;
it did not implement general GPU autonomy. The next production increment remains M6. M7 starts
with a maintained CPU/batched benchmark and oracle, not an assumed ICB or GPU-argument default.
The tiny-network neural shader interop study remains the next optional research direction;
material distillation still depends on a useful inference path, and splat/hybrid projects retain
their separate asset/conformance/compositing costs. Another GPU or rented host is not ruled out,
but buying hardware does not replace reproducible workloads, compiler proof or adoption gates.
