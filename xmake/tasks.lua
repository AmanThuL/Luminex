task("format")
    set_menu {usage = "xmake format [--check]", description = "clang-format all sources",
              options = {{nil, "check", "k", nil, "check only, do not modify"}}}
    on_run(function ()
        import("core.base.option")
        local args = {"-style=file"}
        table.insert(args, option.get("check") and "--dry-run" or "-i")
        if option.get("check") then table.insert(args, "--Werror") end
        for _, f in ipairs(os.files("Source/**.h")) do table.insert(args, f) end
        for _, f in ipairs(os.files("Source/**.cpp")) do table.insert(args, f) end
        -- Named component source roots, not a bare "RojoRHI/**": a developer-copied
        -- RojoRHI/ThirdParty (AGENTS.md) or a RojoRHI/build directory must not break this check.
        for _, root in ipairs({"RojoRHI/Include", "RojoRHI/Source", "RojoRHI/Backends",
                                "RojoRHI/Tests", "RojoRHI/Tools"}) do
            for _, f in ipairs(os.files(root .. "/**.h")) do table.insert(args, f) end
            for _, f in ipairs(os.files(root .. "/**.cpp")) do table.insert(args, f) end
        end
        for _, f in ipairs(os.files("Tests/**.h")) do table.insert(args, f) end
        for _, f in ipairs(os.files("Tests/**.cpp")) do table.insert(args, f) end
        for _, f in ipairs(os.files("Benchmarks/**.h")) do table.insert(args, f) end
        for _, f in ipairs(os.files("Benchmarks/**.cpp")) do table.insert(args, f) end
        os.execv("clang-format", args)
    end)

task("policy")
    set_menu {usage = "xmake policy", description = "check repository and C++ layout policy"}
    on_run(function ()
        os.execv("python3", {"Tools/check_project_policy.py"})
        os.execv("python3", {"Tools/check_shader_imports.py"})
        os.execv("xmake", {"project", "-k", "compile_commands"})
        os.execv("python3", {"Tools/check_module_deps.py"})
        os.execv("python3", {"Tools/check_source_headers.py"})
        os.execv("python3", {"Tools/check_cpp_comments.py", "--public-api-docs", "error"})
        os.execv("python3", {"Tools/check_cpp_layout.py"})
        -- Until the mount the component's own checkers run here, so coverage never lapses. The
        -- root compile_commands.json above already covers the component and its ImGui adapter; a
        -- database generated inside RojoRHI would point at a missing RojoRHI/ThirdParty instead.
        for _, name in ipairs({"check_project_policy.py", "check_shader_imports.py",
                               "check_cpp_comments.py", "check_rhi_headers.py",
                               "check_cpp_layout.py"}) do
            local args = {path.join("RojoRHI/Tools", name)}
            if name == "check_cpp_comments.py" or name == "check_cpp_layout.py" then
                table.insert(args, "--compile-commands")
                table.insert(args, "compile_commands.json")
            end
            if name == "check_cpp_comments.py" then
                table.insert(args, "--public-api-docs")
                table.insert(args, "error")
            end
            os.execv("python3", args)
        end
    end)
