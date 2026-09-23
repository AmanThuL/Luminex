target("Tests")
    set_kind("binary")
    set_default(false)
    -- Keep test shader outputs separate from App; both targets may compile in parallel.
    -- Use singular "test" because the default macOS filesystem aliases it with the Tests binary.
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/test")
    add_files("**.cpp")
    -- Shared helpers are included as "Support/<Name>.h", rooted like Source includes.
    add_includedirs(".")
    add_deps("Core", "RojoRHI", "Render", "Asset", "AppModel", "Engine", "Scenes")
    add_packages("catch2", "glm")
    -- ToolsTests needs a stable path to the Python suite when launched from the test build dir.
    add_defines('LMX_REPO_ROOT="$(projectdir)"')
    -- GPU tests load shaders relative to the test binary and do not depend on an App build.
    add_rules("slang2metallib")
    add_files("../Shaders/**.slang")
    add_tests("unit", {runargs = {"~[gpu]"}})
    -- Hidden diagnostics have explicit reproduction commands and are not ordinary regression gates.
    add_tests("gpu", {runargs = {"[gpu]~[.]"}})
