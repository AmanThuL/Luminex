project "App"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++17"
	staticruntime "off"

	targetdir ("%{wks.location}/Build/Bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/Build/BinInt/" .. outputdir .. "/%{prj.name}")

	files
	{
		"./**.h",
		"./**.cpp"
	}

	includedirs
	{
		"%{IncludeDir.cglm}",
		"%{IncludeDir.GLFW}",
        "%{IncludeDir.ImGui}",
        "%{IncludeDir.stb}",
        "%{IncludeDir.json}",
        "%{IncludeDir.VulkanSDK}"
	}

	libdirs
	{
		"%{LibraryDir.VulkanSDK}"
	}

	links
	{
		"GLFW",
		"ImGui",
        "d3d12",
		"dxgi",
		"d3dcompiler",
		"%{Library.Vulkan}",
		"%{Library.VulkanUtils}"
	}

	defines
	{
		"_CRT_SECURE_NO_WARNINGS",
		"GLFW_INCLUDE_VULKAN"
	}

	filter "system:windows"
		systemversion "latest"

		defines
		{
		}

	filter "configurations:Debug"
		defines "LMX_DEBUG"
		runtime "Debug"
		symbols "on"

		links
		{
		}

	filter "configurations:Release"
		defines "LMX_RELEASE"
		runtime "Release"
		optimize "on"

		links
		{
			"%{Library.ShaderC_Release}",
			"%{Library.SPIRV_Cross_Release}",
			"%{Library.SPIRV_Cross_HLSL_Release}"
		}

	filter "configurations:Dist"
		defines "LMX_DIST"
		runtime "Release"
		optimize "on"

		links
		{
		}