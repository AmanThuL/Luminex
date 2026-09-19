-- Camera + procedural mesh helpers on top of RHI. glm is public so App/Tests only need
-- "Render" in their own add_deps to inherit its include path.
target("Render")
    set_kind("static")
    add_files("*.cpp")
    add_files("Visibility.cpp", "Occlusion.cpp", "LocalLightMath.cpp", "LightClusters.cpp",
              {cxflags = "-ffp-contract=off"})
    add_deps("Core", "RHI")
    add_packages("glm", {public = true})
