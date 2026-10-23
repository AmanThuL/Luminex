-- Render graph, stages and renderer over Engine's scene vocabulary and RHI. glm is public so
-- App/Tests only need "Render" in their own add_deps to inherit its include path.
target("Render")
    set_kind("static")
    add_files("*/*.cpp", "Passes/*/*.cpp")
    add_files("Passes/Visibility/Visibility.cpp", "Passes/Occlusion/Occlusion.cpp", "Passes/LocalLights/LightClusters.cpp",
              {cxflags = "-ffp-contract=off"})
    remove_files(lmx_pending_files("sources"))
    add_defines(lmx_pending_defines())
    on_load(lmx_configure_pending)
    add_deps("Core", "RojoRHI", "Engine")
    add_packages("glm", {public = true})
