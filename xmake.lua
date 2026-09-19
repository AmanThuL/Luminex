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

-- First-party targets keep source membership and dependency declarations beside their units.
includes("Source/Core/xmake.lua", "RHI/xmake.lua", "Source/Render/xmake.lua",
         "Source/Asset/xmake.lua", "Source/Scene/xmake.lua", "Source/App/Model/xmake.lua",
         "Source/App/xmake.lua", "Tests/xmake.lua", "RHI/Tests/xmake.lua",
         "Tools/TextureBake/xmake.lua", "Benchmarks/FrameData/xmake.lua")
