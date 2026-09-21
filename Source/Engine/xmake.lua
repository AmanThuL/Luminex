-- Scene vocabulary, identity and catalog below the renderer: Render depends on Engine.
target("Engine")
    set_kind("static")
    add_files("Types/*.cpp")
    add_files("Types/LocalLightMath.cpp", {cxflags = "-ffp-contract=off"})
    add_deps("Core", "RojoRHI", "Asset")
