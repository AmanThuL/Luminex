-- Shared App policies and scene-session logic have no UI or backend includes.
target("AppModel")
    set_kind("static")
    add_files("**.cpp")
    remove_files(lmx_pending_files("sources"))
    add_defines(lmx_pending_defines())
    add_deps("Core", "RojoRHI", "Render", "Asset", "Engine", "Scenes")
