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
    -- shaders. The six under Shaders/Tests are still shared with the Tests target; the copy
    -- commit gives this component its own tree and drops the reach outside RHI/.
    add_rules("slang2metallib")
    add_files("../Shaders/Tests/*.slang")
    add_files("../../Shaders/Tests/BufferHazardSmoke.slang", "../../Shaders/Tests/ComputeImageSmoke.slang",
              "../../Shaders/Tests/FullscreenSample.slang", "../../Shaders/Tests/MrtSmoke.slang",
              "../../Shaders/Tests/SamplerSmoke.slang", "../../Shaders/Tests/ShadowSmoke.slang")
    add_tests("unit", {runargs = {"~[gpu]"}})
    -- Hidden diagnostics have explicit reproduction commands and are not ordinary regression gates.
    add_tests("gpu", {runargs = {"[gpu]~[.]"}})
