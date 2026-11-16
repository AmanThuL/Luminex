set_project("Luminex")
set_version("0.1.0")
set_languages("c++23")
add_rules("mode.debug", "mode.release")
set_defaultmode("debug")
set_warnings("allextra")
-- Source files are UTF-8, and MSVC reads them in the system code page unless told; fmt also requires it.
set_encodings("utf-8")
set_policy("build.warning", true)

add_requires("libsdl3", "glm", "spdlog", "catch2 3.x", "cgltf", "stb")

-- Reusable rules/tasks and pinned third-party definitions are owned by these includes.
includes("xmake/setup.lua", "xmake/tasks.lua", "xmake/dependencies.lua")

-- The RojoRHI checkout to build against: the pinned RojoRHI/ submodule by default, or another
-- checkout, such as a sibling development clone, through `xmake f --rojorhi_dir=../rojo-rhi`.
option("rojorhi_dir")
    set_default("")
    set_showmenu(true)
    set_description("RojoRHI checkout to build against, relative to the project root; empty for RojoRHI/")
option_end()

-- The RHI component configures from its own root too, so it owns one includable targets file and
-- reads where its vendored dependencies and the Dear ImGui target live from this root. Its own
-- xmake.lua carries root settings for a standalone configure and is deliberately not included.
rojorhi_thirdparty = path.join(os.scriptdir(), "ThirdParty")
rojorhi_imgui_target = "ImGui"
local rojorhi_dir = get_config("rojorhi_dir")
local rojorhi_root = (rojorhi_dir and rojorhi_dir ~= "") and path.absolute(rojorhi_dir, os.scriptdir())
    or path.join(os.scriptdir(), "RojoRHI")
if not os.isfile(path.join(rojorhi_root, "xmake/targets.lua")) then
    -- Description scope has no raise/error/assert/os.exit, and a missing `includes` only warns,
    -- so stop by calling an undefined name whose spelling repeats the instruction.
    print("error: " .. rojorhi_root .. " holds no RojoRHI: run `git submodule update --init` "
          .. "from the repository root, or point --rojorhi_dir at a RojoRHI checkout")
    local run_git_submodule_update_init_or_fix_rojorhi_dir
    run_git_submodule_update_init_or_fix_rojorhi_dir()
end
includes(path.join(rojorhi_root, "xmake/targets.lua"))

includes("xmake/luminex_shaders.lua")

-- First-party targets keep source membership and dependency declarations beside their units.
includes("Source/Core/xmake.lua", "Source/Render/xmake.lua",
         "Source/Engine/xmake.lua",
         "Source/Engine/Asset/xmake.lua",
         "Source/Scenes/xmake.lua", "Source/App/Model/xmake.lua",
         "Source/App/xmake.lua", "Tests/xmake.lua",
         "Tools/TextureBake/xmake.lua", "Benchmarks/FrameData/xmake.lua")
