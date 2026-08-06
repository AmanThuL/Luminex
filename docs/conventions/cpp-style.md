# C++ Style

Formatting is owned by `.clang-format` (`xmake format`). This file covers what a formatter cannot.

- **Naming**: `PascalCase` types & files; `camelCase` functions/variables; private data members use the
  `m_` prefix (`m_device`), public/aggregate members do not (no `s_` prefix anywhere);
  `kPascalCase` compile-time constants; `LMX_` macros; `lowercase` namespaces (`lmx`, `lmx::rhi`).
- **Files**: one primary type per header; `PascalCase.h/.cpp` named after it. `#pragma once`.
- **Includes**: own header first, then project (`"Core/..."`), then third-party, then std. Blank line between groups.
- **Errors**: `lmx::rhi::Result<T>` (`std::expected`) at creation/loading boundaries; `LMX_ASSERT` for
  contract violations; never silently swallow failures (spec §6).
- **C++23**: prefer `std::expected`, `std::span`, `std::string_view`, ranges, `std::print` in tools.
  No RTTI-dependent design; exceptions only from third-party boundaries.
- **Comments**: explain *why* and constraints the code can't show — not what the next line does.
- **GPU objects**: always set a debug label at creation.
