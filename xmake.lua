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
