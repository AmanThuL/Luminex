target("App")
    set_kind("binary")
    add_files("*.cpp", "Panels/*.cpp")
    add_deps("Core", "RHI", "RHIMetal4ImGui", "Render", "Asset", "Scene", "AppModel", "ImGui", "ImGuiNodeEditor")
    add_packages("libsdl3", "glm")
    -- Compile every shader for App so test-only entries cannot silently drift out of build health.
    add_rules("slang2metallib")
    add_files("../../Shaders/**.slang")
