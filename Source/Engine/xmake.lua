-- Scene vocabulary, identity and catalog below the renderer: Render depends on Engine.
target("Engine")
    set_kind("static")
    add_files("View/*.cpp", "Lights/*.cpp", "Geometry/*.cpp", "Scene/*.cpp", "Upload/*.cpp", "Catalog/*.cpp")
    add_files("Lights/LocalLightMath.cpp", {cxflags = "-ffp-contract=off"})
    add_deps("Core", "RojoRHI", "Asset")
