# C++ Style

**Status**: Accepted

Formatting is owned by `.clang-format` (`xmake format`). This file covers what a formatter cannot.

- **Naming**: `PascalCase` types & files; `camelCase` functions/variables; private data members use the
  `m_` prefix (`m_device`), public/aggregate members do not (no `s_` prefix anywhere);
  `kPascalCase` compile-time constants; `LMX_` macros; `lowercase` namespaces (`lmx`, `lmx::rhi`).
- **Files**: one primary type per header; `PascalCase.h/.cpp` named after it. `#pragma once`.
- **Includes**: own header first, then project (`"Core/..."`), then third-party, then std. Blank line between groups.
- **Errors**: use the owning domain's `std::expected` alias at creation and loading boundaries
  (`rhi::Result<T>` for GPU work, `engine::AssetResult<T>` for assets); use `LMX_ASSERT` for contract
  violations and never silently swallow failures (spec §6).
- **C++23**: prefer `std::expected`, `std::span`, `std::string_view`, ranges, `std::print` in tools.
  No RTTI-dependent design; exceptions only from third-party boundaries.
- **GPU objects**: always set a debug label at creation.

## Function boundaries in implementation files

Every hand-written named function definition in a project-owned `.cpp` file starts with a separator
whose physical line is exactly 120 ASCII characters. The canonical namespace-scope form is `//`
followed by 118 `=` characters:

```cpp
//======================================================================================================================
```

- Keep source code at `.clang-format`'s 100-column limit. The separator is the only routine
  120-column exception.
- Put one blank line before the separator. Put the separator before the earliest token belonging to
  the definition, including an attribute, `template`, or `requires` clause. Function-specific
  rationale may sit between the separator and signature without an intervening blank line.
- Apply it to free and anonymous-namespace functions, out-of-line members, constructors,
  destructors, conversions, operators, templates and specializations, `main`, and out-of-class
  `= default` or `= delete` definitions. Each overload gets its own separator.
- Avoid in-class function definitions in `.cpp` files. If an unavoidable local class defines one,
  indent the separator with the function and reduce its `=` padding so the physical line remains
  exactly 120 characters.
- Do not add separators to declarations, explicit instantiations, lambdas, control-flow blocks, or
  function-like macro definitions. Treat Catch2 `TEST_CASE` and related macros as test-unit
  boundaries and give each invocation the canonical separator.
- The `=` ruler belongs only to function and test-unit boundaries. Remove decorative hyphen rulers
  and do not create titled or alternate ruler styles.

A Clang-based policy check, using `compile_commands.json`, rejects missing, duplicate, malformed,
and orphan separators. It must not use a regular expression to guess C++ function definitions.

## Comment policy

- Public headers own API contracts: ownership, lifetime, threading, valid ranges, units, coordinate
  and color spaces, failure behavior, and externally visible side effects.
- Implementation comments are sparse. Use them only for GPU/RHI state or synchronization invariants,
  cross-frame ownership, non-obvious math or space conventions, documented API/driver workarounds,
  performance tradeoffs, or why an apparently redundant operation is required.
- Put a comment next to the smallest block it explains. Prefer a stable specification or issue link
  for platform workarounds. Delete comments that merely translate the code into English.
- Long functions may use short noun-phrase phase labels such as `// Shadow pass`. Do not number them
  or decorate them; the `=` visual language remains exclusive to function boundaries.
- Never record work-item numbers, milestone execution, reviews, prompts, orchestration, commit hashes,
  source line numbers, or implementation chronology in source comments. Do not write `Step 1`,
  `Next`, `for now`, `as requested`, or similar walkthrough prose.
- Do not keep commented-out code or bare `TODO`/`FIXME` markers. A tracked exception uses
  `TODO(<issue-id>): <action and reason>` and must describe real remaining work.
