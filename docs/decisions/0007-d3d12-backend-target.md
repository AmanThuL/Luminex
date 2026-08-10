# ADR 0007: D3D12 is the intended second backend

**Status**: Accepted (2026-08-10) · **Roadmap**: ../roadmap.md (M5.1)

## Context
Metal 4 is the only implemented backend, and Apple Silicon macOS is the only current development and
validation environment. ADR 0002 named Vulkan and D3D12 as future backends #2 and #3 so that a
second explicit API would pressure-test the RHI before later renderer layers grew around Metal-only
assumptions.

Linux support is not a project goal. A production Vulkan backend would therefore add permanent
implementation, conformance, debugging, and hardware-validation obligations without serving a
target platform. D3D12 remains relevant as the native Windows API and as a materially different
translation target. Vulkan specifications and extensions can provide design evidence without a
shipping backend.

## Decision
Metal 4 remains the first, current, and primary backend, with no Metal 3 fallback. D3D12 is the
intended second production backend; its implementation is scheduled only after a Windows build,
run, and GPU-validation environment exists. Vulkan and Linux support are not planned.

Backend-neutral conformance tests continue to grow on Metal first. Vulkan specifications,
extensions, samples, and optional disposable prototypes may challenge interface decisions, but they
do not create a production or CI commitment and must not make the public interface Vulkan-shaped.
The M5.1 experiment may revisit the RHI's caller-facing model, but ADR 0004 remains in force until
that experiment's exit ADR explicitly adopts or rejects a replacement.

This decision supersedes only the future-backend ordering in ADR 0002. Its Metal 4-first decision
and all other foundation decisions remain unchanged.

## Consequences
A second backend no longer blocks Metal-side renderer milestones while Windows validation is
unavailable. When D3D12 work is scheduled, it must pass the shared semantic suite and reproduce the
maintained frame without redefining scene, render-graph, temporal, lifetime, or failure semantics.
Vulkan remains available as a third design oracle if Metal 4 and D3D12 cannot expose enough contrast
for a particular M5.1 question.
