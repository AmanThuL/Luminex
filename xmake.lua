set_project("Luminex")
set_version("0.1.0")
set_languages("c++23")
add_rules("mode.debug", "mode.release")
set_defaultmode("debug")
set_warnings("allextra")
set_policy("build.warning", true)

add_requires("libsdl3", "glm", "spdlog", "catch2 3.x")

-- Slang -> readable MSL -> .metallib (ADR 0003). Emits both artifacts into
-- <targetdir>/Shaders/; a target opts in with add_rules("slang2metallib") plus
-- add_files("Shaders/*.slang"). App and Tests both load the artifacts at runtime, so both
-- opt in -- which is why they must not share a target directory: two targets emitting the
-- same paths race under a parallel build. See the Tests target's set_targetdir below.
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

target("Core")
    set_kind("static")
    add_files("Source/Core/*.cpp")
    add_includedirs("Source", {public = true})
    add_packages("spdlog", {public = true})

-- Dear ImGui (docking) + its SDL3 platform backend + Metal renderer backend, vendored
-- wholesale like metal-cpp/slang (task("setup") below): the xrepo `imgui` package has no
-- metal backend config (M2 Task 0 finding -- it never compiles imgui_impl_metal*), so route A
-- would mean stitching in a second, independently-versioned source tree just for the Metal
-- backend. One pinned tree, one target: core + both backends stay locked to the same tag.
-- IMGUI_IMPL_METAL_CPP (public) switches imgui_impl_metal.h/.mm to the metal-cpp overloads
-- (MTL::Device*/MTL::CommandBuffer*/MTL::RenderCommandEncoder* -- see the M2 plan's Amendments
-- for why that is *not* MTL4::RenderCommandEncoder*: this imgui version has no Metal 4 path).
-- -fno-objc-arc: xmake compiles .mm under ARC by default here, but this backend does explicit
-- `[x release]` / `[x autorelease]` sends (imgui_impl_metal.mm ~lines 182, 429) -- a hard ARC
-- compile error (verified: build fails without this flag). The file must build under MRC.
target("ImGui")
    set_kind("static")
    add_files("ThirdParty/imgui/*.cpp")
    add_files("ThirdParty/imgui/backends/imgui_impl_sdl3.cpp")
    add_files("ThirdParty/imgui/backends/imgui_impl_metal.mm")
    add_includedirs("ThirdParty/imgui", {public = true})
    add_includedirs("ThirdParty/imgui/backends", {public = true})
    add_includedirs("ThirdParty/metal-cpp")
    add_defines("IMGUI_IMPL_METAL_CPP", {public = true})
    add_packages("libsdl3")
    add_frameworks("Metal", "QuartzCore", "Cocoa")
    add_mxxflags("-fno-objc-arc")

-- add_files is non-recursive, so the Metal4 backend sources are listed explicitly.
-- metal-cpp is an implementation detail of this target: RHI.h never names an MTL type,
-- so the include dir stays private and only Source/ is public. ImGui is a dep (not just the
-- Metal renderer backend's headers): Source/RHI/Metal4/Metal4ImGui.* (M2 Task 9) links it.
target("RHI")
    set_kind("static")
    add_files("Source/RHI/*.cpp", "Source/RHI/Metal4/*.cpp")
    add_includedirs("Source", {public = true})
    add_includedirs("ThirdParty/metal-cpp")
    add_frameworks("Metal", "QuartzCore", "Foundation")
    add_deps("Core", "ImGui")

target("App")
    set_kind("binary")
    add_files("Source/App/*.cpp")
    add_deps("Core", "RHI", "ImGui")
    add_packages("libsdl3", "glm")
    -- Emits build/<plat>/<arch>/<mode>/Shaders/Triangle.metal (+ .metallib when the
    -- Metal toolchain is present) next to the App binary.
    add_rules("slang2metallib")
    add_files("Shaders/*.slang")

target("Tests")
    set_kind("binary")
    set_default(false)
    -- Its own directory, not the default one it would otherwise share with App. Tests
    -- compiles the same shaders as App does, and two targets emitting the same output paths
    -- is a real data race: `xmake build -a` schedules them in parallel, both run slangc and
    -- `xcrun metal` on Shaders/Triangle.slang, and the interleaved writes leave behind a
    -- Triangle.metallib that Metal refuses to load. Observed, not theorised.
    -- Singular "test": macOS filesystems are case-insensitive, so a directory named "tests"
    -- collides with the "Tests" binary that sits one level up.
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/test")
    add_files("Tests/*.cpp")
    add_deps("Core", "RHI")
    add_packages("catch2")
    -- The [gpu] smoke test loads "Shaders/Triangle" relative to its working directory, and
    -- `xmake test` runs a target with CWD == target:rundir(), which defaults to the target
    -- directory (verified on this machine). Tests emits its own copy rather than reusing
    -- App's so that running the tests never requires App to have been built.
    add_rules("slang2metallib")
    add_files("Shaders/*.slang")
    add_tests("unit", {runargs = {"~[gpu]"}})
    -- --allow-running-no-tests: Catch2 3.x exits 2 when a tag filter matches nothing.
    -- [gpu] matches now, so this only keeps the entry from turning a future "all GPU
    -- tests removed/renamed" into a confusing hard failure of the runner itself.
    add_tests("gpu",  {runargs = {"[gpu]", "--allow-running-no-tests"}})

local metalcpp_pin = "release/metal-cpp_macOS26.4_iOS26.4"
local slang_pin    = "v2026.14.1"
local imgui_pin    = "v1.92.7-docking"

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
            os.execv("curl", {"-L", "--max-time", "600", "-o", "ThirdParty/" .. zip, url})
            os.execv("unzip", {"-q", "-o", "ThirdParty/" .. zip, "-d", "ThirdParty/slang"})
            os.rm("ThirdParty/" .. zip)
            try {
                function ()
                    os.execv("xattr", {"-dr", "com.apple.quarantine", "ThirdParty/slang"})
                end
            }
        end
        if not os.isdir("ThirdParty/imgui") then
            os.execv("git", {"clone", "--depth", "1", "--branch", imgui_pin,
                             "https://github.com/ocornut/imgui.git", "ThirdParty/imgui"})
        end
        print("setup done: metal-cpp %s, slang %s, imgui %s", metalcpp_pin, slang_pin, imgui_pin)
    end)

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
