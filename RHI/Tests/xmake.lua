-- CPU-only contract tests for the standalone RHI component. They link the RHI target alone, so
-- the suite stays runnable once the component leaves this repository.
target("RHITests")
    set_kind("binary")
    set_default(false)
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/rhi-test")
    add_files("*.cpp")
    add_deps("RHI")
    add_packages("catch2", "glm")
    add_tests("unit", {runargs = {"~[gpu]"}})
    -- The GPU cases still live in the Tests target, so this group matches nothing yet and Catch2
    -- would fail the run; the allowance goes away with the GPU movers.
    add_tests("gpu", {runargs = {"[gpu]~[.]", "--allow-running-no-tests"}})
