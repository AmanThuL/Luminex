project "Premake"
	kind "Utility"

	targetdir ("%{wks.location}/Build/Bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/Build/BinInt/" .. outputdir .. "/%{prj.name}")

	files
	{
		"%{wks.location}/**premake5.lua"
	}

	postbuildmessage "Regenerating project files with Premake5!"
	postbuildcommands
	{
		"%{prj.location}Tools/Premake/Bin/Windows/premake5 %{_ACTION} --file=\"%{wks.location}premake5.lua\""
	}