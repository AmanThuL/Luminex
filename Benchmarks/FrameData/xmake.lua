-- Frame-data benchmark (docs/milestones/m5/m5.2-design.md section 11): times the
-- production RHI's per-frame data-delivery path over five frozen offscreen workloads. The baseline
-- half of a comparison builds this same target from the `m5.2-baseline` tag, whose tree still
-- delivers frame data through the incumbent path. Links the production RHI only -- no
-- archived NoApi prototype include, import, or link -- so it stays a fair comparison host for both
-- sides.
target("FrameDataBench")
    set_kind("binary")
    set_default(false)
    -- Keep bench shader outputs separate from App/Tests; all three targets may compile in parallel.
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/bench")
    add_files("*.cpp")
    add_deps("Core", "RojoRHI")
    add_rules("slang2metallib")
    add_files("../../Shaders/**.slang")
