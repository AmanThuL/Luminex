-- One-time live catalog migration; retained only in the exporter snapshot.
target("SceneExport")
    set_kind("binary")
    set_default(false)
    add_files("main.cpp", {cxflags = "-ffp-contract=off"})
    add_deps("Core", "RojoRHI", "Asset", "Engine", "Scenes")
