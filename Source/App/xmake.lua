target("App")
    set_kind("binary")
    add_files("Shell/*.cpp", "Headless/*.cpp", "Panels/*/*.cpp")
    add_files("Shell/*.mm")
    add_frameworks("AppKit")
    add_deps("Core", "RojoRHI", "RojoRHIMetal4ImGui", "Render", "Asset", "AppModel", "ImGui", "ImGuiNodeEditor", "Engine", "Scenes")
    add_packages("libsdl3", "glm")
    -- Compile every shader for App so test-only entries cannot silently drift out of build health.
    add_rules("slang2metallib")
    add_files("../../Shaders/**.slang")
    after_build(function (target)
        local appicondir = path.join(target:targetdir(), "Icons")
        os.mkdir(appicondir)
        os.cp(path.join(os.projectdir(), "Assets/Icons/*"), appicondir)
        local fontdir = path.join(os.projectdir(), "ThirdParty/Inter")
        local dest = path.join(target:targetdir(), "Fonts")
        os.mkdir(dest)
        for _, name in ipairs({"InterVariable.ttf", "LICENSE.txt", "SOURCE.txt"}) do
            local source = path.join(fontdir, name)
            assert(os.isfile(source), "Missing editor font resource; run xmake setup first")
            os.cp(source, path.join(dest, name))
        end
        local icondir = path.join(os.projectdir(), "ThirdParty/Codicons")
        local iconfiles = {
            {source = "codicon.ttf", staged = "codicon.ttf"},
            {source = "LICENSE", staged = "Codicons-LICENSE.txt"},
            {source = "SOURCE.txt", staged = "Codicons-SOURCE.txt"}
        }
        local icons_available = true
        for _, entry in ipairs(iconfiles) do
            icons_available = icons_available and os.isfile(path.join(icondir, entry.source))
        end
        if not icons_available then
            wprint("Codicons resources are missing; run xmake setup. App will use text labels.")
        end
        for _, entry in ipairs(iconfiles) do
            local staged = path.join(dest, entry.staged)
            if icons_available then
                os.cp(path.join(icondir, entry.source), staged)
            else
                os.tryrm(staged)
            end
        end
    end)
