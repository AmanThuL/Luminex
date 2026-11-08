-- Frame-data benchmark (docs/milestones/m5/m5.2-design.md section 11): times the production RHI's
-- per-frame data-delivery path -- frame-data uploads addressed through pushed roots -- over five
-- frozen offscreen workloads. The baseline of a paired comparison is the frozen RojoRHI v1 build of
-- this target; Tools/Bench/frame_data_paired.py runs the two and refuses timing when their digests
-- differ. Links the production RHI only, so it stays a fair comparison host.
target("FrameDataBench")
    set_kind("binary")
    set_default(false)
    -- Keep bench shader outputs separate from App/Tests; all three targets may compile in parallel.
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/bench")
    add_files("*.cpp")
    add_deps("Core", "RojoRHI")
    lmx_apply_shader_rule("bench")
