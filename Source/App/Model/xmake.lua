-- Shared App policies and scene-session logic have no UI or backend includes.
target("AppModel")
    set_kind("static")
    add_files("*.cpp")
    add_deps("Core", "RojoRHI", "Render", "Asset", "Scene")
