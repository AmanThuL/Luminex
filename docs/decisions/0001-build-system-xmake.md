# ADR 0001: Build system — xmake

**Status**: Accepted (2026-08-07) · **Spec**: ../specs/2026-08-07-luminex-upgrade-design.md (D1)

## Context
Lumine used Premake. Research (Aug 2026) found Premake still beta with decade-old Xcode/Metal gaps;
CMake excluded by owner preference; xmake is the most actively maintained option (~5–6 week cadence)
with native Apple toolchain support and xrepo package management.

## Decision
xmake is the single build tool: builds, deps (xrepo), shader rules, format task, ThirdParty setup.

## Consequences
No generated Xcode project — GPU debugging goes through programmatic MTLCaptureManager captures
(.gputrace → Xcode). Smaller Western community: occasionally translate CMake-world instructions.
Premake remains the documented fallback (both are Lua).
