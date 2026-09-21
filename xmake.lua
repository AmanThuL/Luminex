set_project("Luminex")
set_version("0.1.0")
set_languages("c++23")
add_rules("mode.debug", "mode.release")
set_defaultmode("debug")
set_warnings("allextra")
set_policy("build.warning", true)

add_requires("libsdl3", "glm", "spdlog", "catch2 3.x", "cgltf", "stb")

-- Reusable rules/tasks and pinned third-party definitions are owned by these includes.
includes("xmake/shaders.lua", "xmake/setup.lua", "xmake/tasks.lua", "xmake/dependencies.lua")

-- The RHI component configures from its own root too, so it owns one includable targets file and
-- reads where its vendored dependencies and the Dear ImGui target live from this root. Its own
-- RojoRHI/xmake.lua carries root settings for a standalone configure and is deliberately not included.
rojorhi_thirdparty = path.join(os.scriptdir(), "ThirdParty")
rojorhi_imgui_target = "ImGui"
if not os.isfile(path.join(os.scriptdir(), "RojoRHI/xmake/targets.lua")) then
    -- Description scope has no raise/error/assert/os.exit, and a missing `includes` only warns,
    -- so stop by calling an undefined name whose spelling repeats the instruction.
    print("error: RojoRHI/ is empty: run `git submodule update --init` from the repository root")
    local run_git_submodule_update_init_RojoRHI_is_empty
    run_git_submodule_update_init_RojoRHI_is_empty()
end
includes("RojoRHI/xmake/targets.lua")

-- First-party targets keep source membership and dependency declarations beside their units.
includes("Source/Core/xmake.lua", "Source/Render/xmake.lua",
         "Source/Engine/xmake.lua",
         "Source/Asset/xmake.lua", "Source/App/Model/xmake.lua",
         "Source/App/xmake.lua", "Tests/xmake.lua",
         "Tools/TextureBake/xmake.lua", "Benchmarks/FrameData/xmake.lua")
