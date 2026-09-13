-- Scenes own GPU uploads and produce the renderer's borrowed frame inputs.
target("Scene")
    set_kind("static")
    add_files("*.cpp")
    add_deps("Core", "RHI", "Asset", "Render")
