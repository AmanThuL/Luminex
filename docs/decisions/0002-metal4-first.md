# ADR 0002: Metal 4 native as the first RHI backend

**Status**: Accepted (2026-08-07) · **Spec**: ../specs/2026-08-07-luminex-upgrade-design.md (D2)

## Context
The only dev machine is Apple Silicon macOS, so a native backend gets Xcode GPU debugging of our own
calls (no MoltenVK/KosmicKrisp translation layer in between). MTL4's allocator/argument-table/residency
model is the most modern of the three current APIs and keeps the RHI honest; open-source C++ Metal 4
renderers are rare, which helps the portfolio angle. metal-cpp became an official Apple repo with
complete MTL4 coverage in June 2026, removing the old Obj-C++ glue barrier.

## Decision
Metal 4 (via metal-cpp) is `lmx::rhi`'s first backend — not Vulkan-via-MoltenVK/KosmicKrisp. Vulkan and
D3D12 return later as backends #2/#3 behind the same interfaces.

## Consequences
No Windows/Linux support until a Vulkan backend lands. `MTLGPUFamilyMetal4` is checked at device
creation and is a hard requirement — no Metal 3 fallback. RHI concepts are derived from MTL4 first
(argument tables, residency sets) and must be re-validated for portability once Vulkan returns.
