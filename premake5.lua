include "./Tools/Premake/PremakeCustomization/solution_items.lua"
include "Dependencies.lua"

newoption {
	trigger = "gfxapi",
	value = "API",
	description = "Choose a particular 3D graphics API for rendering backend",
	allowed = {
	   { "vulkan", "Vulkan" },
	   { "d3d12",  "DirectX 12 (Windows only)" },
	},
	default = "vulkan"
 }

workspace "Luminex"
	architecture "x86_64"
	startproject "App"

	configurations
	{
		"Debug",
		"Release",
		"Dist"
	}

	solution_items
	{
		".editorconfig"
	}

	flags
	{
		"MultiProcessorCompile"
	}

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"


group "Dependencies"
	include "ThirdParty/glfw"
	include "ThirdParty/imgui"
group ""

include "Source/App"
-- include "Source/Core"
-- include "Source/Engine"
-- include "Source/Render"
-- include "Source/LMXRHI"
