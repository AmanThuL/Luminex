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
    -- Catch2 3.x treats a tag filter matching zero tests as failure (exit 2)
    -- unless told otherwise; no [gpu] tests exist yet in M1, so this entry
    -- must be allowed to pass trivially until GPU tests land (later tasks).
    add_tests("gpu",  {runargs = {"[gpu]", "--allow-running-no-tests"}})

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
