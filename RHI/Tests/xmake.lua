-- Contract and GPU tests for the standalone RHI component. They link the RHI target alone, so
-- the suite stays runnable once the component leaves this repository.
target("RHITests")
    set_kind("binary")
    set_default(false)
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/rhi-test")
    add_files("*.cpp")
    add_deps("RHI")
    add_packages("catch2", "glm")
    -- GPU cases load shaders relative to the test binary, so this target compiles its own smoke
    -- shaders out of the component's own tree. Six of them are byte-identical copies of oracles the
    -- Tests target still owns: duplicated deliberately, so nothing under RHI/ reaches outside it.
    -- The non-recursive pattern keeps Modules/ off the entry-point list.
    add_rules("slang2metallib")
    set_values("slang.moduledir", "RHI/Shaders/Tests/Modules")
    add_files("../Shaders/Tests/*.slang")
    add_tests("unit", {runargs = {"~[gpu]"}})
    -- Hidden diagnostics have explicit reproduction commands and are not ordinary regression gates.
    add_tests("gpu", {runargs = {"[gpu]~[.]"}})
