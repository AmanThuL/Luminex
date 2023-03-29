-- Luminex Dependencies

VULKAN_SDK = os.getenv("VULKAN_SDK")

IncludeDir = {}
IncludeDir["stb"] = "%{wks.location}/ThirdParty/stb"
IncludeDir["json"] = "%{wks.location}/ThirdParty/json"
IncludeDir["GLFW"] = "%{wks.location}/ThirdParty/glfw/include"
IncludeDir["ImGui"] = "%{wks.location}/ThirdParty/imgui"
-- IncludeDir["ImGuizmo"] = "%{wks.location}/ThirdParty/vendor/ImGuizmo"
IncludeDir["cglm"] = "%{wks.location}/ThirdParty/cglm"
-- IncludeDir["entt"] = "%{wks.location}/ThirdParty/entt/include"
-- IncludeDir["shaderc"] = "%{wks.location}/ThirdParty/shaderc/include"
-- IncludeDir["SPIRV_Cross"] = "%{wks.location}/ThirdParty/SPIRV-Cross"
IncludeDir["VulkanSDK"] = "%{VULKAN_SDK}/Include"

LibraryDir = {}

LibraryDir["VulkanSDK"] = "%{VULKAN_SDK}/Lib"
-- LibraryDir["VulkanSDK_Debug"] = "%{wks.location}/ThirdParty/VulkanSDK/Lib"

Library = {}
Library["Vulkan"] = "%{LibraryDir.VulkanSDK}/vulkan-1.lib"
Library["VulkanUtils"] = "%{LibraryDir.VulkanSDK}/VkLayer_utils.lib"

-- Library["ShaderC_Debug"] = "%{LibraryDir.VulkanSDK_Debug}/shaderc_sharedd.lib"
-- Library["SPIRV_Cross_Debug"] = "%{LibraryDir.VulkanSDK_Debug}/spirv-cross-cored.lib"
-- Library["SPIRV_Cross_GLSL_Debug"] = "%{LibraryDir.VulkanSDK_Debug}/spirv-cross-glsld.lib"
-- Library["SPIRV_Tools_Debug"] = "%{LibraryDir.VulkanSDK_Debug}/SPIRV-Toolsd.lib"

Library["ShaderC_Release"] = "%{LibraryDir.VulkanSDK}/shaderc_shared.lib"
Library["SPIRV_Cross_Release"] = "%{LibraryDir.VulkanSDK}/spirv-cross-core.lib"
Library["SPIRV_Cross_HLSL_Release"] = "%{LibraryDir.VulkanSDK}/spirv-cross-hlsl.lib"