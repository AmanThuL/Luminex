target("App")
    set_kind("binary")
    add_files("*.cpp", "Panels/*.cpp")
    add_deps("Core", "RojoRHI", "RojoRHIMetal4ImGui", "Render", "Asset", "Scene", "AppModel", "ImGui", "ImGuiNodeEditor")
    add_packages("libsdl3", "glm")
    -- Compile every shader for App so test-only entries cannot silently drift out of build health.
    add_rules("slang2metallib")
    add_files("../../Shaders/**.slang")
    after_build(function (target)
        local fontdir = path.join(os.projectdir(), "ThirdParty/Inter")
        local dest = path.join(target:targetdir(), "Fonts")
        os.mkdir(dest)
        for _, name in ipairs({"InterVariable.ttf", "LICENSE.txt", "SOURCE.txt"}) do
            local source = path.join(fontdir, name)
            assert(os.isfile(source), "Missing editor font resource; run xmake setup first")
            os.cp(source, path.join(dest, name))
        end
    end)
