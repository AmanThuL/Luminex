# Luminex M1 — Foundation + Metal 4 Triangle Through RHI — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `xmake && xmake run App` opens an SDL3 window rendering a Metal 4 triangle through `lmx::rhi`, with tests, formatting, docs, ADRs, CLAUDE.md, and CI in place.

**Architecture:** Thin RHI (`lmx::rhi`) with abstract interfaces and a single Metal 4 backend (metal-cpp, 3 frames in flight, argument tables, residency set, MTLSharedEvent pacing). SDL3 owns windowing and hands the RHI a `CAMetalLayer*`. Shaders authored in Slang, compiled Slang→MSL→`xcrun metal -std=metal4.0`→metallib by an xmake rule.

**Tech Stack:** xmake, C++23 (Apple Clang/Xcode 26), SDL3, metal-cpp (official Apple repo, pinned), Slang (pinned binary), glm, spdlog, Catch2 v3, clang-format.

**Spec:** `docs/specs/2026-08-07-luminex-upgrade-design.md` — read it first; its decisions are binding.

## Global Constraints

- C++23 (`set_languages("c++23")`), Apple Clang from Xcode 26. No C++ modules.
- macOS 26+, Apple Silicon only. `MTLGPUFamilyMetal4` is a hard requirement — **no Metal 3 fallback**.
- 3 frames in flight, exactly (matches Apple's Metal 4 triangle sample).
- No Metal/Apple types in any public RHI header (`Source/RHI/*.h`). Backend types live under `Source/RHI/Metal4/`.
- Creation APIs return `lmx::rhi::Result<T>` (`std::expected`); contract violations are `LMX_ASSERT` (fatal), never error codes.
- xrepo deps for M1 only: `libsdl3`, `glm`, `spdlog`, `catch2`. (imgui/stb/nlohmann_json are M2+ — do NOT add yet; YAGNI.)
- ThirdParty/ holds only fetched-not-committed deps: metal-cpp + slang binary, pinned to exact versions in `xmake.lua`.
- Commits: imperative mood, English, no AI co-author trailers.
- Every commit compiles and passes `xmake test` (CPU tests at minimum).
- All GPU objects get debug labels at creation.

## Amendments

**A1 (2026-08-07, Task 0 blocker):** Apple's MobileAsset catalog currently refuses the Metal Toolchain download for Xcode 26.4.1 build 17E202 (server requests 17E188 — catalog lag). The offline `metal` CLI is therefore unavailable; the OS *runtime* compiler works (probed: `makeLibrary(source:)` OK, `MTLGPUFamilyMetal4` supported on this M3 Max). **Amended shader pipeline:** build rule always emits readable `.metal` (Slang→MSL); it additionally precompiles `.metallib` only when `xcrun metal` exists. `Device::loadShaderLibrary(pathNoExt)` resolves `<pathNoExt>.metallib` first, else `<pathNoExt>.metal` (runtime compile, Metal-4 language version). Retry `xcodebuild -downloadComponent MetalToolchain` at later task boundaries; when it lands, the offline path activates automatically. Tasks 6, 7, 9, 11, 12 below are written post-amendment.

## Subagent & Model Policy (per Rudy)

| Model | Used for | Tasks |
|---|---|---|
| **Fable 5** | Main thread — the brain: orchestration, moderation of all subagent output, spec/plan authorship, between-task review, final verification | orchestration + reviews (no dispatched tasks) |
| **Opus 5** | Design-sensitive / thin-documentation work: Metal 4 backend, RHI surface, Slang build rule, frame loop, GPU smoke test | 6, 7, 8, 9, 10, 11, 12 |
| **Sonnet 5** | Standard implementation: toolchain setup, repo demolition, xmake scaffolding, Core module, docs/ADRs/CLAUDE.md, CI, polish | 0, 1, 2, 4, 5, 13, 14 |
| **Haiku 4.5** | Simplest mechanical ops only: applying clang-format across the tree, checkbox/status bookkeeping | 3 (apply step), plan checkbox updates |

Every dispatched subagent must be told which model tier its task was assigned and this table must be mirrored in CLAUDE.md (Task 4).

**Metal 4 references for Tasks 8–12** (executors: consult before coding):
- Apple sample: https://developer.apple.com/documentation/metal/drawing-a-triangle-with-metal-4 (zip: https://docs-assets.developer.apple.com/published/afbc1ba209ed/DrawingATriangleWithMetal4.zip)
- Core API article: https://developer.apple.com/documentation/metal/understanding-the-metal-4-core-api
- Pure-C++ MTL4 triangle: https://github.com/javiersalcedopuyo/metal-4-with-only-cpp and https://dev.to/javiersalcedopuyo/writing-a-hello-triangle-with-metal-4-and-exclusively-c-3kgm
- Production MTL4 RHI: WickedEngine `WickedEngine/wiGraphicsDevice_Metal.cpp`
- Apple agent skills: https://github.com/apple/game-porting-toolkit → `game-porting-skills/skills/` (`translating-to-metal4-api`, `managing-metal4-synchronization`, `managing-metal-cpp-lifetimes`)
- Exact metal-cpp signatures: read the vendored headers in `ThirdParty/metal-cpp/Metal/` (`MTL4*.hpp`). **The code in this plan is faithful to the dossier but executors MUST verify method names against these headers — they are the ground truth.**

---

### Task 0: Toolchain setup (Sonnet 5)

**Files:** none (environment only — nothing to commit)

**Interfaces:**
- Produces: working `xmake`, `xcrun metal` (Metal 4 capable), verified host (macOS 26.5.2, Xcode 26.4.1, arm64 — pre-audited).

- [ ] **Step 1: Install xmake**

```bash
brew install xmake
xmake --version   # expect v3.x
```

- [ ] **Step 2: Download the Metal toolchain component** (macOS 26 ships it separately; pre-audit showed it missing)

```bash
sudo xcodebuild -license accept 2>/dev/null || true
xcodebuild -downloadComponent MetalToolchain
```
This can take several minutes. If `sudo` prompts block automation, surface the command to Rudy to run via `! xcodebuild -downloadComponent MetalToolchain`.

- [ ] **Step 3: Verify Metal 4 compiler works**

```bash
xcrun -sdk macosx metal --version
echo 'kernel void k() {}' > /tmp/probe.metal
xcrun -sdk macosx metal -std=metal4.0 -o /tmp/probe.metallib /tmp/probe.metal && echo METAL4-OK
```
Expected: version string + `METAL4-OK`. If `-std=metal4.0` is rejected, try `-std=metal4.1`; record the accepted flag — Task 6 uses it.

---

### Task 1: Repo demolition (Sonnet 5)

**Files:**
- Delete: `premake5.lua`, `Dependencies.lua`, `Luminex.sln`, `.clang-format-ignore`, `Tools/`, `Scripts/`, `Source/` (entire old tree), `ThirdParty/` (all submodules + vendored), `.gitmodules`
- Keep: `LICENSE`, `README.md` (rewritten in Task 4), `.editorconfig`, `.clang-format` (replaced in Task 3), `docs/`
- Modify: `.gitignore`

**Interfaces:**
- Produces: clean repo containing only LICENSE, README.md, .editorconfig, .clang-format, .gitignore, docs/.

- [ ] **Step 1: Remove submodules properly**

```bash
cd /Users/rudyz/Documents/projects/Luminex
cat .gitmodules   # list actual submodule paths first — expect ThirdParty/glfw, ThirdParty/imgui, possibly others
git submodule deinit -f --all
git rm -f ThirdParty Tools Scripts Source Luminex.sln premake5.lua Dependencies.lua .clang-format-ignore .gitmodules 2>/dev/null
rm -rf .git/modules
git status   # verify: only deletions + docs/ intact
```
If `git rm` balks on any path (vendored vs submodule differences), fall back to `git rm -rf <path>` per path. Verify `LICENSE`, `README.md`, `.editorconfig`, `.clang-format`, `.gitignore`, `docs/` survived.

- [ ] **Step 2: Replace .gitignore content**

```gitignore
# xmake
/.xmake/
/build/

# fetched third-party (see `xmake setup`)
/ThirdParty/*
!/ThirdParty/README.md

# tooling
/.cache/
compile_commands.json
*.gputrace
.DS_Store
```

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "Remove legacy Premake, Vulkan test, and vendored dependencies"
```

---

### Task 2: xmake workspace + Core module + test harness (Sonnet 5)

**Files:**
- Create: `xmake.lua`, `Source/Core/Log.h`, `Source/Core/Log.cpp`, `Source/Core/Assert.h`, `Tests/CoreTests.cpp`
- Create: `ThirdParty/README.md` (one line: "Populated by `xmake setup` — see xmake.lua. Never committed.")

**Interfaces:**
- Produces: `lmx::log::init()`, `LMX_LOG_INFO/WARN/ERROR(fmt, ...)` macros (spdlog passthrough), `LMX_ASSERT(cond, msg)` (logs + `std::abort()` on failure, enabled in all configs). Targets: `Core` (static), `Tests` (Catch2, `xmake test`). Later targets follow this file's pattern.

- [ ] **Step 1: Write root xmake.lua**

```lua
set_project("Luminex")
set_version("0.1.0")
set_languages("c++23")
add_rules("mode.debug", "mode.release")
set_defaultmode("debug")
set_warnings("allextra")
set_policy("build.warning", true)

add_requires("libsdl3", "glm", "spdlog", "catch2 3.x")

target("Core")
    set_kind("static")
    add_files("Source/Core/*.cpp")
    add_includedirs("Source", {public = true})
    add_packages("spdlog", {public = true})

target("Tests")
    set_kind("binary")
    set_default(false)
    add_files("Tests/*.cpp")
    add_deps("Core")
    add_packages("catch2")
    add_tests("unit", {runargs = {"~[gpu]"}})
    add_tests("gpu",  {runargs = {"[gpu]"}})
```
(`xmake test Tests/unit` = CPU-only; `xmake test` = everything. Package names `libsdl3`/`glm`/`spdlog`/`catch2` — if resolution fails, `xrepo search <name>` for the exact name and fix.)

- [ ] **Step 2: Write the failing test**

`Tests/CoreTests.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>

#include "Core/Assert.h"
#include "Core/Log.h"

TEST_CASE("log init is idempotent", "[core]") {
    lmx::log::init();
    lmx::log::init();  // second call must not throw or duplicate sinks
    LMX_LOG_INFO("hello from tests");
    SUCCEED();
}
```

- [ ] **Step 3: Run to verify failure**

```bash
xmake f -m debug && xmake build Tests
```
Expected: FAIL — `Core/Log.h` not found.

- [ ] **Step 4: Implement Core**

`Source/Core/Log.h`:
```cpp
#pragma once
#include <spdlog/spdlog.h>

namespace lmx::log {
void init();  // safe to call more than once
}

#define LMX_LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define LMX_LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define LMX_LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
```

`Source/Core/Log.cpp`:
```cpp
#include "Core/Log.h"

#include <spdlog/sinks/stdout_color_sinks.h>

namespace lmx::log {

void init() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    auto logger = spdlog::stdout_color_mt("lmx");
    spdlog::set_default_logger(std::move(logger));
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_level(spdlog::level::trace);
}

}  // namespace lmx::log
```

`Source/Core/Assert.h`:
```cpp
#pragma once
#include "Core/Log.h"

#include <cstdlib>

// Fatal contract check — enabled in ALL build configs (spec §6).
#define LMX_ASSERT(cond, msg)                                                  \
    do {                                                                       \
        if (!(cond)) {                                                         \
            LMX_LOG_ERROR("ASSERT FAILED: {} ({}:{}) — {}", #cond, __FILE__,   \
                          __LINE__, msg);                                      \
            std::abort();                                                      \
        }                                                                      \
    } while (0)
```

- [ ] **Step 5: Run tests to verify pass**

```bash
xmake build Tests && xmake test
```
Expected: `unit` PASS (gpu entry passes trivially — no [gpu] tests exist yet).

- [ ] **Step 6: Commit**

```bash
git add xmake.lua Source/Core Tests ThirdParty/README.md
git commit -m "Add xmake workspace, Core logging/assert, Catch2 harness"
```

---

### Task 3: clang-format + format task (Sonnet 5; apply-step may be Haiku 4.5)

**Files:**
- Replace: `.clang-format`
- Modify: `xmake.lua` (append `format` task)

**Interfaces:**
- Produces: `xmake format` (in-place) and `xmake format --check` (CI mode, nonzero exit on drift).

- [ ] **Step 1: Replace .clang-format**

```yaml
BasedOnStyle: LLVM
Standard: Latest
IndentWidth: 4
ColumnLimit: 100
PointerAlignment: Left
DerivePointerAlignment: false
AccessModifierOffset: -4
NamespaceIndentation: None
AllowShortFunctionsOnASingleLine: Inline
AlwaysBreakTemplateDeclarations: Yes
InsertNewlineAtEOF: true
SortIncludes: CaseSensitive
IncludeBlocks: Preserve
```

- [ ] **Step 2: Append format task to xmake.lua**

```lua
task("format")
    set_menu {usage = "xmake format [--check]", description = "clang-format all sources",
              options = {{nil, "check", "k", nil, "check only, do not modify"}}}
    on_run(function ()
        import("core.base.option")
        local args = {"-style=file"}
        table.insert(args, option.get("check") and "--dry-run" or "-i")
        if option.get("check") then table.insert(args, "--Werror") end
        for _, f in ipairs(os.files("Source/**.h")) do table.insert(args, f) end
        for _, f in ipairs(os.files("Source/**.cpp")) do table.insert(args, f) end
        for _, f in ipairs(os.files("Tests/**.cpp")) do table.insert(args, f) end
        os.execv("clang-format", args)
    end)
```

- [ ] **Step 3: Apply and verify** (mechanical — Haiku-eligible)

```bash
xmake format && xmake format --check && echo FORMAT-CLEAN
xmake build Tests && xmake test
```
Expected: `FORMAT-CLEAN`, tests still green.

- [ ] **Step 4: Commit**

```bash
git add .clang-format xmake.lua Source Tests
git commit -m "Add clang-format config and xmake format task"
```

---

### Task 4: docs, ADRs, CLAUDE.md, README (Sonnet 5)

**Files:**
- Create: `docs/conventions/cpp-style.md`, `docs/conventions/shader-style.md`, `docs/conventions/commits.md`
- Create: `docs/decisions/0001-build-system-xmake.md`, `0002-metal4-first.md`, `0003-slang-shaders.md`, `0004-thin-rhi.md`
- Create: `CLAUDE.md`
- Replace: `README.md`

**Interfaces:**
- Produces: the documentation contract every later task follows. CLAUDE.md is the agent entry point.

- [ ] **Step 1: Write conventions** — full content:

`docs/conventions/cpp-style.md`:
```markdown
# C++ Style

Formatting is owned by `.clang-format` (`xmake format`). This file covers what a formatter cannot.

- **Naming**: `PascalCase` types & files; `camelCase` functions/variables/members (no `m_`/`s_` prefixes);
  `kPascalCase` compile-time constants; `LMX_` macros; `lowercase` namespaces (`lmx`, `lmx::rhi`).
- **Files**: one primary type per header; `PascalCase.h/.cpp` named after it. `#pragma once`.
- **Includes**: own header first, then project (`"Core/..."`), then third-party, then std. Blank line between groups.
- **Errors**: `lmx::rhi::Result<T>` (`std::expected`) at creation/loading boundaries; `LMX_ASSERT` for
  contract violations; never silently swallow failures (spec §6).
- **C++23**: prefer `std::expected`, `std::span`, `std::string_view`, ranges, `std::print` in tools.
  No RTTI-dependent design; exceptions only from third-party boundaries.
- **Comments**: explain *why* and constraints the code can't show — not what the next line does.
- **GPU objects**: always set a debug label at creation.
```

`docs/conventions/shader-style.md`:
```markdown
# Shader Style (Slang)

- One `.slang` file per pipeline/feature in `Shaders/`; entry points `vertexMain`/`fragmentMain`/`computeMain`
  marked with `[shader("...")]` attributes (no `-entry` flags at compile time).
- Globals: `gPascalCase` resources, `kPascalCase` constants. Explicit flat binding indices — they map 1:1 to
  `MTL4ArgumentTable` slots today and Vulkan descriptor sets later (spec §5).
- Build: Slang → MSL source → `xcrun metal -std=metal4.0` → `.metallib` (two-step; see ADR 0003).
  Generated `.metal` stays in the build tree for debugging — read it when a shader misbehaves.
```

`docs/conventions/commits.md`:
```markdown
# Commit Conventions

- Imperative mood, English, subject ≤ 72 chars, no trailing period, no AI co-author trailers.
- Body (when needed) explains *why*, references docs/specs or ADRs when relevant.
- Every commit compiles and passes `xmake test`. Format before committing (`xmake format`).
```

- [ ] **Step 2: Write ADRs** — each ~10 lines: Status/Context/Decision/Consequences. Content = condensed from spec §2 rows D1 (0001), D2 (0002), D5+D6 (0003), and spec §4 philosophy (0004). Example shape for 0001 (repeat the pattern, don't skip fields):

```markdown
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
```

- [ ] **Step 3: Write CLAUDE.md** — full content:

```markdown
# Luminex

Modern rendering playground / portfolio piece. Metal 4-first (macOS 26+, Apple Silicon), thin RHI,
future Vulkan/D3D12 backends. Successor to college project "lumine" (DX12).

## Golden sources
- Spec: `docs/specs/2026-08-07-luminex-upgrade-design.md` (decisions D1–D10 are binding)
- ADRs: `docs/decisions/` · Conventions: `docs/conventions/` (C++ style, shader style, commits)
- Current plan: `docs/plans/2026-08-07-m1-foundation-triangle.md`

## Commands
- Setup (once): `brew install xmake`, `xmake setup`. Optional: `xcodebuild -downloadComponent
  MetalToolchain` enables offline shader precompile (Apple catalog was refusing it 2026-08-07 —
  retry occasionally; the runtime-MSL-compile fallback works without it).
- Build: `xmake` · Run: `xmake run App` · Tests: `xmake test` (CPU-only: `xmake test Tests/unit`)
- Format: `xmake format` (check: `xmake format --check`)
- Debug: Metal validation `MTL_DEBUG_LAYER=1 xmake run App`; GPU capture: press `c` in-app
  (needs `MTL_CAPTURE_ENABLED=1`), then open the .gputrace in Xcode.

## Architecture
`Source/Core` (lmx:: log/assert) → `Source/RHI` (lmx::rhi interfaces; **no Metal types in public
headers**) → `Source/RHI/Metal4` (the only backend: metal-cpp, 3 frames in flight, argument tables,
residency set, shared-event pacing) → `Source/App` (SDL3 window + frame loop). `Source/Render` is an
intentionally empty placeholder until M2. Shaders: `Shaders/*.slang` (see shader-style.md).

## Hard rules
- C++23. No Metal 3 fallback (`MTLGPUFamilyMetal4` required). 3 frames in flight.
- Creation returns `Result<T>`; misuse is `LMX_ASSERT`. GPU objects always get labels.
- xrepo deps only as needed (M1: libsdl3, glm, spdlog, catch2). ThirdParty/ is fetched via
  `xmake setup`, pinned in xmake.lua, never committed.
- Commits per `docs/conventions/commits.md`. Every commit compiles + passes `xmake test`.

## Subagent & model policy (Rudy's standing instruction)
**Fable 5** runs the main thread — the brain that moderates everything, writes specs/plans, and
reviews all subagent output. Dispatch subagents with model tiers: **Opus 5** for design-sensitive/
thin-doc work (RHI surface, Metal 4 backend, sync, shader toolchain), **Sonnet 5** for standard
implementation (scaffolding, docs, CI), **Haiku 4.5** only for trivial mechanical ops (bulk format,
checkbox updates).

## Update policy
Refresh this file at every milestone boundary (M1 → M2 → …) and whenever a command or hard rule changes.
```

- [ ] **Step 4: Replace README.md** — portfolio-facing skeleton (real content, no placeholders): project one-liner, "Metal 4 · C++23 · Slang · xmake" badges line (plain text), Requirements (macOS 26+, Apple Silicon, Xcode 26 + Metal toolchain, Homebrew xmake), Quick start (the four setup/build/run commands from CLAUDE.md), Architecture paragraph (from spec §4 summary), Roadmap (spec §8 milestone list), Credits (lumine, Apple samples, WickedEngine, metal-cpp, Slang), License pointer.

- [ ] **Step 5: Commit**

```bash
git add docs CLAUDE.md README.md
git commit -m "Add conventions, ADRs 0001-0004, CLAUDE.md, and new README"
```

---

### Task 5: ThirdParty setup task — metal-cpp + Slang (Sonnet 5)

**Files:**
- Modify: `xmake.lua` (append `setup` task; add metal-cpp include dir constant)

**Interfaces:**
- Produces: `xmake setup` → `ThirdParty/metal-cpp/` (headers) and `ThirdParty/slang/` (with `bin/slangc`). Pinned versions live in the two locals at the top of the task. Task 6 uses `ThirdParty/slang/bin/slangc`; Task 8 uses `ThirdParty/metal-cpp` includes.

- [ ] **Step 1: Determine pin versions** (record actual values into the code below)

```bash
curl -s https://api.github.com/repos/apple/metal-cpp/tags | python3 -c "import json,sys; print(json.load(sys.stdin)[0]['name'])"
curl -s https://api.github.com/repos/shader-slang/slang/releases/latest | python3 -c "import json,sys; print(json.load(sys.stdin)['tag_name'])"
```

- [ ] **Step 2: Append setup task to xmake.lua** (substitute the two pins from Step 1)

```lua
local metalcpp_pin = "<TAG-FROM-STEP-1>"   -- e.g. macOS26.4-iOS26.4
local slang_pin    = "<TAG-FROM-STEP-1>"   -- e.g. v2026.7.1

task("setup")
    set_menu {usage = "xmake setup", description = "fetch pinned ThirdParty deps"}
    on_run(function ()
        if not os.isdir("ThirdParty/metal-cpp") then
            os.execv("git", {"clone", "--depth", "1", "--branch", metalcpp_pin,
                             "https://github.com/apple/metal-cpp.git", "ThirdParty/metal-cpp"})
        end
        if not os.isfile("ThirdParty/slang/bin/slangc") then
            local ver = slang_pin:sub(2)  -- strip leading v
            local zip = format("slang-%s-macos-aarch64.zip", ver)
            local url = format("https://github.com/shader-slang/slang/releases/download/%s/%s", slang_pin, zip)
            os.mkdir("ThirdParty/slang")
            os.execv("curl", {"-L", "-o", "ThirdParty/" .. zip, url})
            os.execv("unzip", {"-q", "-o", "ThirdParty/" .. zip, "-d", "ThirdParty/slang"})
            os.rm("ThirdParty/" .. zip)
        end
        print("setup done: metal-cpp %s, slang %s", metalcpp_pin, slang_pin)
    end)
```
(`<TAG-FROM-STEP-1>` must be replaced with the real recorded values before commit — grep the file for `<TAG` to confirm none remain. Verify the slang zip asset name against the release page assets; adjust if the naming scheme differs.)

- [ ] **Step 3: Run and verify**

```bash
xmake setup
ls ThirdParty/metal-cpp/Metal | grep -c "MTL4" && ThirdParty/slang/bin/slangc -v
```
Expected: a nonzero MTL4 header count (full Metal 4 coverage) and slangc version string.

- [ ] **Step 4: Commit**

```bash
git add xmake.lua && git commit -m "Add xmake setup task fetching pinned metal-cpp and slang"
```

---

### Task 6: Slang→metallib build rule + Triangle shader (Opus 5)

**Files:**
- Create: `Shaders/Triangle.slang`
- Modify: `xmake.lua` (add `slang2metallib` rule; wire into a new `Shaders` phony-consumer or directly into the future App target — expose rule only, App consumes it in Task 10)

**Interfaces:**
- Produces: rule `slang2metallib` — for each `.slang` source of a target, emits `$(builddir)/Shaders/<name>.metal` (readable MSL) and `$(builddir)/Shaders/<name>.metallib`. Task 9/11 loads `Triangle.metallib` with entries `vertexMain`/`fragmentMain` reading a structured buffer at **argument-table buffer slot 0**.

- [ ] **Step 1: Write Shaders/Triangle.slang**

```slang
// Fullscreen-less triangle fed by a structured buffer bound at slot 0
// (slot indices map 1:1 to MTL4ArgumentTable buffer indices — see shader-style.md).
struct Vertex
{
    float2 position;
    float3 color;
};

StructuredBuffer<Vertex> gVertices;  // buffer slot 0

struct VSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID)
{
    Vertex v = gVertices[vid];
    VSOutput o;
    o.position = float4(v.position, 0.0, 1.0);
    o.color = v.color;
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOutput in): SV_Target
{
    return float4(in.color, 1.0);
}
```

- [ ] **Step 2: Add the rule to xmake.lua**

```lua
rule("slang2metallib")
    set_extensions(".slang")
    on_buildcmd_file(function (target, batchcmds, sourcefile, opt)
        local outdir = path.join(target:targetdir(), "Shaders")
        local name = path.basename(sourcefile)
        local msl = path.join(outdir, name .. ".metal")
        local slangc = path.join(os.projectdir(), "ThirdParty/slang/bin/slangc")
        batchcmds:mkdir(outdir)
        batchcmds:show_progress(opt.progress, "${color.build.object}slang %s", sourcefile)
        batchcmds:vrunv(slangc, {sourcefile, "-target", "metal", "-o", msl})
        -- Amendment A1: offline metallib precompile only when the Metal toolchain
        -- exists; otherwise the runtime compiles the .metal source (RHI resolves both).
        local has_metal = try {function ()
            os.runv("xcrun", {"-sdk", "macosx", "metal", "--version"}); return true
        end}
        if has_metal then
            local lib = path.join(outdir, name .. ".metallib")
            batchcmds:vrunv("xcrun", {"-sdk", "macosx", "metal", "-std=metal4.0",
                                      "-frecord-sources", "-gline-tables-only", "-o", lib, msl})
        end
        batchcmds:add_depfiles(sourcefile)
        batchcmds:set_depmtime(os.mtime(msl))
        batchcmds:set_depcache(target:dependfile(msl))
    end)
```
`-std=metal4.0` is the expected flag (try `metal4.1` if rejected, once the toolchain exists). `-frecord-sources -gline-tables-only` embeds shader debug info for Xcode captures.

- [ ] **Step 3: Verify by hand before target wiring**

```bash
ThirdParty/slang/bin/slangc Shaders/Triangle.slang -target metal -o /tmp/Triangle.metal
grep -E "vertexMain|fragmentMain|\[\[buffer\(0\)\]\]" /tmp/Triangle.metal
xcrun -sdk macosx metal --version >/dev/null 2>&1 && xcrun -sdk macosx metal -std=metal4.0 -o /tmp/Triangle.metallib /tmp/Triangle.metal && echo SHADER-OK || echo NO-METAL-CLI-RUNTIME-PATH
```
Expected: both entry names present in readable MSL, `gVertices` at `[[buffer(0)]]`; `SHADER-OK` if the Metal toolchain is installed, otherwise `NO-METAL-CLI-RUNTIME-PATH` (Amendment A1 — acceptable, the runtime path covers it). If slang mangles entry names or binding indices differ, fix the .slang (explicit `[[vk::binding]]`-style or `-fvk-` flags are NOT the tool here — consult slang Metal docs: https://shader-slang.org/slang/user-guide/metal-target-specific) and record actual MSL function names for Task 9.

- [ ] **Step 4: Commit**

```bash
git add Shaders xmake.lua && git commit -m "Add Slang to metallib build rule and triangle shader"
```

---

### Task 7: RHI public surface + validation unit tests (Opus 5)

**Files:**
- Create: `Source/RHI/RHI.h` (whole public surface), `Source/RHI/Validate.h`, `Source/RHI/Validate.cpp`
- Create: `Tests/RHIValidateTests.cpp`
- Modify: `xmake.lua` (add `RHI` static target: `Source/RHI/*.cpp` + later `Source/RHI/Metal4/*.cpp`; deps Core; frameworks come in Task 8; Tests adds dep RHI)

**Interfaces:**
- Produces (consumed by Tasks 8–12 and App — signatures are binding):

```cpp
// Source/RHI/RHI.h — complete file
#pragma once
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace lmx::rhi {

enum class ErrorCode {
    DeviceUnsupported,
    ShaderLoadFailed,
    PipelineCreationFailed,
    ResourceCreationFailed,
    SwapchainFailed,
    InvalidDesc,
};
struct Error {
    ErrorCode code;
    std::string message;
};
template <typename T>
using Result = std::expected<T, Error>;

enum class Format { Unknown, BGRA8Unorm, RGBA8Unorm, D32Float };

struct BufferDesc {
    uint64_t size = 0;
    std::string_view label;
};
class Buffer {
public:
    virtual ~Buffer() = default;
    virtual uint64_t size() const = 0;
};

struct TextureDesc {
    uint32_t width = 0, height = 0;
    Format format = Format::BGRA8Unorm;
    bool renderTarget = false;
    bool cpuReadback = false;  // shared storage; enables readback()
    std::string_view label;
};
class Texture {
public:
    virtual ~Texture() = default;
    virtual uint32_t width() const = 0;
    virtual uint32_t height() const = 0;
    // Blocking readback of the full texture (requires cpuReadback). out must hold
    // width*height*4 bytes for 8-bit formats. Caller ensures GPU work completed
    // (Device::waitIdle).
    virtual void readback(void* out, uint64_t outSize) = 0;
};

class ShaderLibrary {
public:
    virtual ~ShaderLibrary() = default;
};

struct GraphicsPipelineDesc {
    ShaderLibrary* library = nullptr;
    std::string_view vertexEntry;
    std::string_view fragmentEntry;
    Format colorFormat = Format::BGRA8Unorm;
    std::string_view label;
};
class GraphicsPipeline {
public:
    virtual ~GraphicsPipeline() = default;
};

struct RenderPassDesc {
    Texture* colorTarget = nullptr;
    float clearColor[4] = {0.f, 0.f, 0.f, 1.f};
    bool clear = true;
};

class CommandList {
public:
    virtual ~CommandList() = default;
    virtual void beginRenderPass(const RenderPassDesc& desc) = 0;
    virtual void bindPipeline(GraphicsPipeline& pipeline) = 0;
    virtual void bindVertexBuffer(uint32_t slot, Buffer& buffer) = 0;  // argument-table slot
    virtual void draw(uint32_t vertexCount, uint32_t firstVertex = 0) = 0;
    virtual void endRenderPass() = 0;
};

struct SwapchainDesc {
    void* nativeLayer = nullptr;  // CAMetalLayer* — created by the windowing layer
    uint32_t width = 0, height = 0;
    Format format = Format::BGRA8Unorm;
};
class Swapchain {
public:
    virtual ~Swapchain() = default;
    virtual Result<Texture*> acquireNextTexture() = 0;  // valid until endFrame/present
    virtual void resize(uint32_t width, uint32_t height) = 0;
};

struct DeviceDesc {
    bool enableValidation = true;
};
class Device {
public:
    virtual ~Device() = default;
    virtual Result<std::unique_ptr<Swapchain>> createSwapchain(const SwapchainDesc&) = 0;
    virtual Result<std::unique_ptr<Buffer>> createBuffer(const BufferDesc&,
                                                         const void* initialData) = 0;
    virtual Result<std::unique_ptr<Texture>> createTexture(const TextureDesc&) = 0;
    // pathNoExt: resolves "<pathNoExt>.metallib" (precompiled) first, else
    // "<pathNoExt>.metal" (runtime-compiled MSL, Metal 4 language version) — Amendment A1.
    virtual Result<std::unique_ptr<ShaderLibrary>> loadShaderLibrary(std::string_view pathNoExt) = 0;
    virtual Result<std::unique_ptr<GraphicsPipeline>>
    createGraphicsPipeline(const GraphicsPipelineDesc&) = 0;

    // Frame loop: beginFrame blocks on pacing (3 in flight), returns the frame CommandList.
    // endFrame commits; if presentTo != nullptr, presents its acquired texture.
    virtual CommandList& beginFrame() = 0;
    virtual void endFrame(Swapchain* presentTo) = 0;
    virtual void waitIdle() = 0;

    virtual std::string_view deviceName() const = 0;
};

// Factory — the only symbol the backend exports. Metal4 is the sole backend in M1.
Result<std::unique_ptr<Device>> createDevice(const DeviceDesc& desc = {});

}  // namespace lmx::rhi
```

- Also produces `Validate.h`: `Result<void> validate(const BufferDesc&)`, `validate(const TextureDesc&)`, `validate(const GraphicsPipelineDesc&)`, `validate(const SwapchainDesc&)` — pure functions, each returning `ErrorCode::InvalidDesc` with a message naming the offending field; backends call them first.

- [ ] **Step 1: Write failing tests** — `Tests/RHIValidateTests.cpp`: zero-size BufferDesc → error mentions "size"; TextureDesc 0×0 → error; TextureDesc cpuReadback+Format::D32Float → error ("readback supports 8-bit formats only"); GraphicsPipelineDesc null library → error mentions "library"; empty vertexEntry → error; SwapchainDesc null nativeLayer → error; and the happy path for each desc → `has_value()`. Write each as a separate `TEST_CASE(..., "[rhi]")` with exact asserts (`REQUIRE_FALSE(r.has_value()); REQUIRE(r.error().code == ErrorCode::InvalidDesc); REQUIRE(r.error().message.contains("size"));`).
- [ ] **Step 2: Run** `xmake build Tests` — expected FAIL (headers missing).
- [ ] **Step 3: Implement** `RHI.h` exactly as above + `Validate.{h,cpp}` making tests pass; add RHI target to xmake.lua (`add_deps("Core")`, `add_includedirs("Source", {public=true})`), Tests `add_deps("RHI")`.
- [ ] **Step 4: Run** `xmake test Tests/unit` — expected PASS.
- [ ] **Step 5: Commit** — `git commit -m "Add RHI public surface and desc validation with tests"`

---

### Task 8: Metal4 backend bootstrap — Device creation (Opus 5)

**Files:**
- Create: `Source/RHI/Metal4/Metal4Common.h` (includes metal-cpp, `toMTL(Format)` helpers, `NS::String` helpers), `Source/RHI/Metal4/MetalCppImpl.cpp` (the one TU defining `NS_PRIVATE_IMPLEMENTATION`, `CA_PRIVATE_IMPLEMENTATION`, `MTL_PRIVATE_IMPLEMENTATION` before includes), `Source/RHI/Metal4/Metal4Device.h`, `Source/RHI/Metal4/Metal4Device.cpp`
- Create: `Source/App/main.cpp` (minimal: init log, createDevice, print name, exit) + App target
- Modify: `xmake.lua` (RHI: `add_files("Source/RHI/Metal4/*.cpp")`, `add_includedirs("ThirdParty/metal-cpp")`, `add_frameworks("Metal", "QuartzCore", "Foundation")`; App target: binary, deps Core+RHI, packages libsdl3+glm, `add_rules("slang2metallib")`, `add_files("Shaders/*.slang")`)

**Interfaces:**
- Consumes: `RHI.h` surface (Task 7), metal-cpp headers (Task 5).
- Produces: working `lmx::rhi::createDevice()` → `Metal4Device` holding: `MTL::Device* device` (`MTL::CreateSystemDefaultDevice()`), Metal 4 family check (`device->supportsFamily(MTL::GPUFamilyMetal4)` — exact enum name per metal-cpp headers; on failure return `ErrorCode::DeviceUnsupported` with the device name in the message), `MTL4::CommandQueue* queue` (created via the device's MTL4 queue factory — verify exact metal-cpp name, e.g. `newMTL4CommandQueue()`), `MTL4::Compiler* compiler` (from `MTL4::CompilerDescriptor`), one `MTL::ResidencySet* residency` attached to the queue, three `MTL4::CommandAllocator*`, one `MTL::SharedEvent* frameEvent`, `uint64_t frameNumber = 0`. All labeled ("lmx.device.queue" etc.). Destructor releases in reverse order (metal-cpp is manual retain/release — see Apple's `managing-metal-cpp-lifetimes` skill).

- [ ] **Step 1:** Implement the files. `Metal4Device` implements every `Device` virtual; all except the five creation methods used later may `LMX_ASSERT(false, "implemented in a later task")` bodies EXCEPT: constructor/family check/`deviceName()`/`waitIdle()` (`frameEvent`-based or `queue` idle wait — simplest correct: signal+wait a fresh shared event value) must be real now.
- [ ] **Step 2:** Minimal `Source/App/main.cpp`:

```cpp
#include "Core/Log.h"
#include "RHI/RHI.h"

int main() {
    lmx::log::init();
    auto device = lmx::rhi::createDevice();
    if (!device) {
        LMX_LOG_ERROR("createDevice failed: {}", device.error().message);
        return 1;
    }
    LMX_LOG_INFO("Metal 4 device: {}", (*device)->deviceName());
    return 0;
}
```

- [ ] **Step 3: Verify** — `xmake build App && xmake run App` → expected: log line with your GPU name (e.g. "Apple M-series"). Run `leaks --atExit -- ./build/.../App 2>/dev/null | tail -3` once to sanity-check metal-cpp release discipline (no growing leak list; a few Apple-internal singletons are normal).
- [ ] **Step 4: Commit** — `git commit -m "Add Metal 4 device bootstrap and App entry point"`

---

### Task 9: Metal4 resources — ShaderLibrary, GraphicsPipeline, Buffer, Texture (Opus 5)

**Files:**
- Create: `Source/RHI/Metal4/Metal4Resources.h/.cpp` (Metal4Buffer, Metal4Texture, Metal4ShaderLibrary, Metal4Pipeline)
- Modify: `Source/RHI/Metal4/Metal4Device.cpp` (implement `createBuffer`, `createTexture`, `loadShaderLibrary`, `createGraphicsPipeline`)
- Modify: `Source/App/main.cpp` (exercise all four; still windowless)

**Interfaces:**
- Consumes: Task 7 descs + validation, Task 8 device members, `Triangle.metallib` from the App target's shader rule (path: alongside the App binary under `Shaders/`).
- Produces: the four creation methods, each: `validate(desc)` first → Metal object → label → **add to residency set** (buffers/textures; then `residency->commit()`), returning typed wrappers. Details: Buffer = `device->newBuffer(size, MTL::ResourceStorageModeShared)` + memcpy initialData; Texture = descriptor with `renderTarget ? MTL::TextureUsageRenderTarget : 0`, storage `Shared` when `cpuReadback` (readback via `getBytes`); ShaderLibrary = per Amendment A1: if `<pathNoExt>.metallib` exists → `device->newLibrary(url)`; else read `<pathNoExt>.metal` and `device->newLibrary(source, compileOptions, &error)` with the Metal-4 language version from the metal-cpp `MTL::LanguageVersion` enum (pick the highest 4.x the headers offer); neither file / compile error → `ShaderLoadFailed` with path and compiler text in message; Pipeline = `MTL4::RenderPipelineDescriptor` + `MTL4::LibraryFunctionDescriptor` (library + entry names from Task 6 Step 3 record) + colorFormat, built through `compiler->newRenderPipelineState(...)` → `PipelineCreationFailed` on error with compiler error text.

- [ ] **Step 1:** Extend `main.cpp` to create: vertex buffer (3 × the classic RGB triangle: `{{0,0.5},{1,0,0}}, {{-0.5,-0.5},{0,1,0}}, {{0.5,-0.5},{0,0,1}}`). **Measured shader ABI (Task 6 record — binding):** Slang emits `struct Vertex_natural_0 { packed_float2 position_0; packed_float3 color_1; }` — tightly packed, stride 20, position@0, color@8. C++ side: `struct Vertex { float px, py; float r, g, b; }; static_assert(sizeof(Vertex) == 20);`. Entry names preserved (`vertexMain`/`fragmentMain`); `gVertices` is at buffer index 0 in BOTH vertex and fragment stages (Slang emits it into the fragment signature too — bind slot 0 on both argument-table stages or validation flags an unbound buffer). Also create: readback texture 4×4, shader library via `loadShaderLibrary("Shaders/Triangle")`, pipeline from it.
- [ ] **Step 2: Verify** — `xmake run App` logs success for all four creations. Intentionally break the entry name once ("vertexMainX") → expect a `PipelineCreationFailed` error with useful message, then restore.
- [ ] **Step 3: Commit** — `git commit -m "Add Metal 4 buffer, texture, shader library, and pipeline creation"`

---

### Task 10: Frame loop + Swapchain + SDL3 window — clear color on screen (Opus 5)

**Files:**
- Create: `Source/RHI/Metal4/Metal4CommandList.h/.cpp`, `Source/RHI/Metal4/Metal4Swapchain.h/.cpp`
- Modify: `Source/RHI/Metal4/Metal4Device.cpp` (`beginFrame`/`endFrame`/`createSwapchain`)
- Modify: `Source/App/main.cpp` (SDL3 window + loop; clear to a color, no draw yet)

**Interfaces:**
- Consumes: everything prior.
- Produces: the M1 frame protocol (binding for Task 11/12):
  - `beginFrame()`: `frameNumber++`; if `frameNumber > 3` → `frameEvent->waitUntilSignaledValue(frameNumber - 3, timeoutMs)`; `allocators[frameNumber % 3]->reset()`; command buffer `beginCommandBuffer(allocator)`; returns the device-owned `Metal4CommandList`.
  - `CommandList::beginRenderPass`: `MTL4::RenderPassDescriptor` from desc (clear/load, clearColor, colorTarget's MTLTexture) → `commandBuffer->renderCommandEncoder(desc)`; `bindPipeline` → `setRenderPipelineState`; `bindVertexBuffer(slot, buffer)` → argument table `setAddress(buffer->gpuAddress(), slot)` + `encoder->setArgumentTable(table, vertex-stage)` (table created once on device: `MTL4::ArgumentTableDescriptor` maxBufferBindCount 8, maxTextureBindCount 8); `draw` → `drawPrimitives(Triangle, firstVertex, vertexCount)`; `endRenderPass` → `endEncoding`.
  - `endFrame(swapchain)`: `endCommandBuffer()`; if presenting: `queue->waitForDrawable(drawable)` → `queue->commit(&cmdBuf, 1)` → `queue->signalDrawable(drawable)` → `drawable->present()`; else plain commit. Then `queue->signalEvent(frameEvent, frameNumber)`. (Exact call names/order per Apple triangle sample — the executor MUST cross-check the sample source from the zip in the references.)
  - `Metal4Swapchain`: wraps the `CA::MetalLayer*` from desc (sets device, pixelFormat, drawableSize); `acquireNextTexture()` → `layer->nextDrawable()` (nil → `SwapchainFailed`), wraps drawable texture in a transient non-owning Metal4Texture; `resize` sets `drawableSize`.
- App loop produces: SDL init video; `SDL_CreateWindow("Luminex", 1280, 720, SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE)`; `SDL_Metal_CreateView` + `SDL_Metal_GetLayer` → `SwapchainDesc.nativeLayer`; per frame: poll events (`SDL_EVENT_QUIT`; on `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` → `swapchain->resize`), acquire, `beginFrame`, render pass clearing to `{0.1f, 0.15f, 0.2f, 1.f}`, `endFrame(&swapchain)`.

- [ ] **Step 1:** Implement; build clean (`xmake`).
- [ ] **Step 2: Verify visually** — `xmake run App`: window shows the steel-blue clear color, no validation errors with `MTL_DEBUG_LAYER=1 xmake run App`, resizing doesn't crash, quit is clean (waitIdle in shutdown path before releasing).
- [ ] **Step 3: Commit** — `git commit -m "Add Metal 4 frame loop, swapchain, and SDL3 window with clear pass"`

---

### Task 11: The triangle + GPU capture hook (Opus 5)

**Files:**
- Modify: `Source/App/main.cpp` (bind pipeline + vertex buffer, draw 3, 'c' capture key)
- Create: `Source/RHI/Metal4/Metal4Capture.h/.cpp` (`lmx::rhi::metal4::triggerCapture(Device&, std::string_view outPath)` using `MTL::CaptureManager` + `MTL::CaptureDescriptor` → .gputrace file)

**Interfaces:**
- Consumes: the whole stack.
- Produces: THE TRIANGLE. And: pressing `c` (SDL keycode `SDLK_C`) writes `luminex-frame.gputrace` next to the binary (requires env `MTL_CAPTURE_ENABLED=1`; log a warning if capture manager reports unsupported).

- [ ] **Step 1:** Wire draw calls into the App loop between beginRenderPass/endRenderPass: `bindPipeline(*pipeline); bindVertexBuffer(0, *vertexBuffer); draw(3);`
- [ ] **Step 2: Verify** — `xmake run App` → RGB triangle on steel-blue background. `MTL_DEBUG_LAYER=1` run → zero validation messages. Press `c` with `MTL_CAPTURE_ENABLED=1` → .gputrace appears and opens in Xcode (`open luminex-frame.gputrace`) showing the draw with argument table contents.
- [ ] **Step 3:** Screenshot for the README, fully automated (Rudy may be AFK): add a `--screenshot <out.bmp>` mode to App — render ONE frame offscreen (1280×720, renderTarget+cpuReadback texture, same pipeline/vertex buffer), `readback()`, write an uncompressed 32-bit BMP (hand-rolled 54-byte header — BGRA matches BMP natively), exit. Then `sips -s format png <out.bmp> --out docs/images/m1-triangle.png` and reference it from README. This doubles as a no-window sanity path.
- [ ] **Step 4: Commit** — `git commit -m "Render the first Metal 4 triangle through the RHI"`

---

### Task 12: GPU smoke test — offscreen render + readback (Opus 5)

**Files:**
- Create: `Tests/GpuSmokeTests.cpp`
- Modify: `xmake.lua` (Tests links RHI already; ensure Tests also gets `slang2metallib` + `add_files("Shaders/*.slang")` so the metallib lands next to the test binary too)

**Interfaces:**
- Consumes: full RHI. No SDL — offscreen only.

- [ ] **Step 1: Write the test**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <vector>

#include "RHI/RHI.h"

namespace {
// Measured Slang/MSL ABI (Task 6 record): packed_float2 + packed_float3, stride 20.
struct Vertex {
    float position[2];
    float color[3];
};
static_assert(sizeof(Vertex) == 20, "must match Slang's packed Vertex_natural_0 layout");
}

TEST_CASE("offscreen triangle renders expected pixels", "[gpu]") {
    using namespace lmx::rhi;
    auto deviceR = createDevice();
    REQUIRE(deviceR.has_value());
    auto& device = **deviceR;

    const std::array<Vertex, 3> verts = {{
        {{0.f, 0.5f}, {1.f, 0.f, 0.f}},
        {{-0.5f, -0.5f}, {0.f, 1.f, 0.f}},
        {{0.5f, -0.5f}, {0.f, 0.f, 1.f}},
    }};
    auto vb = device.createBuffer({.size = sizeof(verts), .label = "smoke.vb"}, verts.data());
    REQUIRE(vb.has_value());
    auto rt = device.createTexture({.width = 64, .height = 64, .format = Format::BGRA8Unorm,
                                    .renderTarget = true, .cpuReadback = true, .label = "smoke.rt"});
    REQUIRE(rt.has_value());
    auto lib = device.loadShaderLibrary("Shaders/Triangle");
    REQUIRE(lib.has_value());
    auto pso = device.createGraphicsPipeline({.library = lib->get(), .vertexEntry = "vertexMain",
                                              .fragmentEntry = "fragmentMain",
                                              .colorFormat = Format::BGRA8Unorm, .label = "smoke.pso"});
    REQUIRE(pso.has_value());

    auto& cmd = device.beginFrame();
    RenderPassDesc pass{.colorTarget = rt->get(), .clearColor = {0.f, 0.f, 0.f, 1.f}, .clear = true};
    cmd.beginRenderPass(pass);
    cmd.bindPipeline(**pso);
    cmd.bindVertexBuffer(0, **vb);
    cmd.draw(3);
    cmd.endRenderPass();
    device.endFrame(nullptr);
    device.waitIdle();

    std::vector<uint8_t> pixels(64 * 64 * 4);
    (*rt)->readback(pixels.data(), pixels.size());

    auto px = [&](int x, int y) { return &pixels[(y * 64 + x) * 4]; };  // BGRA
    // corner (2,2) stays clear color = black
    REQUIRE(px(2, 2)[2] == 0);
    // center-bottom-ish (32, 40) is inside the triangle — must be non-black
    auto* c = px(32, 40);
    REQUIRE((int(c[0]) + int(c[1]) + int(c[2])) > 60);
}
```
Note: NDC y-up vs texture row order means the triangle's interior sits in the *lower* rows of the image half — if the center probe fails, dump the buffer as PPM to inspect (`std::ofstream("smoke.ppm")`, P6 header, RGB swizzle) and adjust probe coordinates once; the corner-black assert is orientation-proof.

- [ ] **Step 2:** Working-directory care: the test loads `Shaders/Triangle` (metallib or .metal per A1) relative to CWD — run via `xmake test` (xmake runs tests with CWD = target dir). Verify: `xmake test` → both `unit` and `gpu` PASS locally.
- [ ] **Step 3: Commit** — `git commit -m "Add offscreen GPU smoke test with pixel readback"`

---

### Task 13: CI (Sonnet 5)

**Files:**
- Create: `.github/workflows/ci.yml`

**Interfaces:**
- Produces: green check on push to main. GPU test excluded (runner GPUs are virtualized; Metal 4 availability unverified — the smoke test stays a documented local gate per spec §6).

- [ ] **Step 1: Write workflow**

```yaml
name: CI
on:
  push: {branches: [main]}
  pull_request:
jobs:
  build-test:
    runs-on: macos-26
    steps:
      - uses: actions/checkout@v4
      - name: Install xmake
        run: brew install xmake
      - name: Metal toolchain
        run: sudo xcodebuild -downloadComponent MetalToolchain || echo "::warning::Metal toolchain download failed"
      - name: Setup third-party
        run: xmake setup
      - name: Configure and build (release)
        run: xmake f -m release -y && xmake -y
      - name: Unit tests
        run: xmake test Tests/unit
      - name: Format check
        run: brew install clang-format && xmake format --check
```
If the Metal toolchain step proves unreliable on runners, shader compilation will fail the build — in that case split shader targets behind an xmake option (`--shaders=n`) in a follow-up commit and note it in CLAUDE.md. Do not silently drop CI compilation of the backend itself.

- [ ] **Step 2:** Push and verify the workflow runs green (`gh run watch`). Fix package-name or brew hiccups as they surface.
- [ ] **Step 3: Commit** (the yml lands with its own commit before push): `git commit -m "Add GitHub Actions CI for macOS build, tests, and format check"`

---

### Task 14: Polish + closeout (Sonnet 5; checkbox updates Haiku 4.5)

**Files:**
- Modify: `README.md` (triangle screenshot from Task 11, verified quick-start), `CLAUDE.md` (verify commands truthful), `docs/specs/2026-08-07-luminex-upgrade-design.md` (Status → "Implemented (M1) — 2026-08-07"), this plan (all checkboxes ticked)

- [ ] **Step 1:** Re-run the full local gate from scratch as a user would:

```bash
git clean -xdff ThirdParty build .xmake 2>/dev/null; xmake setup && xmake f -m debug -y && xmake -y && xmake test && xmake run App
```
All green + triangle visible. Fix any doc/command drift found.
- [ ] **Step 2:** `xmake format --check` clean; update the three docs; tick all plan checkboxes.
- [ ] **Step 3: Commit** — `git commit -m "Polish README and docs for M1 completion"`

---

## Self-Review (completed at write time)

- **Spec coverage**: D1 xmake (T2/T5/T6), D2 Metal4-no-fallback (T8), D3 C++23 (T2), D4 SDL3 (T10), D5/D6 Slang two-step (T6), D7 glm (required in T2, used from M2 — kept because App links it per spec dep list), D8 format+tidy (T3/T13 — **clang-tidy job deliberately deferred to M2**, noted here as a conscious cut from spec §6: non-blocking job adds CI time with zero code to lint beyond what -Wall catches at this size; revisit at M2), D9 Catch2 (T2), D10 spdlog (T2); RHI surface/mapping (T7–T11); smoke test (T12); CI (T13); CLAUDE.md/ADRs/conventions (T4); milestone DoD (T14).
- **Placeholders**: The two `<TAG-FROM-STEP-1>` markers in Task 5 are *instructions to substitute measured values at execution time* with an explicit grep-check step — not deferred content.
- **Type consistency**: `Result<T>`, desc field names, `beginFrame()/endFrame(Swapchain*)`, `bindVertexBuffer(slot, Buffer&)`, entry names `vertexMain`/`fragmentMain`, slot 0, 3 FIF — consistent across Tasks 6–12.
