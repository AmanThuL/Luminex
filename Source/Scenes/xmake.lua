-- Catalog scenes assembled from Engine's vocabulary, above it: App and Tests depend on Scenes.
target("Scenes")
    set_kind("static")
    add_files("*.cpp")
    add_deps("Core", "RojoRHI", "Asset", "Engine")
