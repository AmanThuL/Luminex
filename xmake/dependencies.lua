-- Dear ImGui (docking) + its SDL3 platform backend + native Metal 4 renderer backend, vendored
-- wholesale like metal-cpp/slang (xmake/setup.lua): the xrepo `imgui` package has no
-- Metal backend config. It never compiles imgui_impl_metal*, so using it would mean stitching
-- in a second, independently-versioned source tree just for the Metal
-- backend. One pinned tree, one target: core + both backends stay locked to the same commit.
-- Pinned past the v1.92.7-docking tag (see xmake/setup.lua): imgui_impl_metal4.{h,mm} --
-- the native Metal 4 backend (MTL4::CommandQueue/CommandBuffer/RenderCommandEncoder,
-- ImTextureID = the MTLTexture object pointer) -- lands on the docking branch after that tag.
-- The pointer, *not* its gpuResourceID, despite what imgui_impl_metal4.h's feature list still
-- says: the backend stores the texture object as ImTextureID, casts it back to id<MTLTexture>,
-- and only then derives gpuResourceID for binding. A ResourceID could not survive that round trip.
-- Upstream trap, harmless only because we never call it: the header declares
-- ImGui_ImplMetal4_CreateDeviceObjects for both APIs, but the implementation defines the C++
-- overload under the un-suffixed name ImGui_ImplMetal_CreateDeviceObjects. Calling the declared
-- C++ symbol is a link error; NewFrame creates the device objects lazily instead.
-- IMGUI_IMPL_METAL_CPP (public) switches imgui_impl_metal4.h/.mm to the metal-cpp overloads
-- (MTL::Device*, MTL4::CommandQueue*/CommandBuffer*/RenderPassDescriptor*/RenderCommandEncoder*
-- as declared by the pinned backend header). -fno-objc-arc is required because this backend does
-- an explicit `[x autorelease]` send, which is a hard compile error under xmake's default ARC-on
-- Objective-C++ compilation.
target("ImGui")
    set_kind("static")
    add_files("../ThirdParty/imgui/*.cpp")
    add_files("../ThirdParty/imgui/backends/imgui_impl_sdl3.cpp")
    add_files("../ThirdParty/imgui/backends/imgui_impl_metal4.mm")
    add_includedirs("../ThirdParty/imgui", {public = true})
    add_includedirs("../ThirdParty/imgui/backends", {public = true})
    add_includedirs("../ThirdParty/metal-cpp")
    add_defines("IMGUI_IMPL_METAL_CPP", {public = true})
    add_packages("libsdl3")
    add_frameworks("Metal", "QuartzCore", "Cocoa")
    add_mxxflags("-fno-objc-arc")

-- thedmd/imgui-node-editor (node_editor_pin in xmake/setup.lua), vendored the same way as Dear ImGui: no
-- xrepo package exists, and the maintained patch keeps it building against our post-1.93 ImGui
-- pin (imgui_pin in xmake/setup.lua) after upstream stalled on an unreleased `master`. Depends on ImGui for
-- its headers; nothing else links it, so it stays out of Tests. set_warnings("none") silences three
-- standing warnings in its vendored source (an unused-but-set variable, a deprecated
-- std::aligned_storage use, and an unused Log() parameter) that are not ours to fix; left under the
-- project's default allextra policy, they would sit in every build log and mask a new first-party
-- warning from this target.
target("ImGuiNodeEditor")
    set_kind("static")
    set_warnings("none")
    add_files("../ThirdParty/imgui-node-editor/imgui_node_editor.cpp",
              "../ThirdParty/imgui-node-editor/imgui_node_editor_api.cpp",
              "../ThirdParty/imgui-node-editor/imgui_canvas.cpp",
              "../ThirdParty/imgui-node-editor/crude_json.cpp")
    add_includedirs("../ThirdParty/imgui-node-editor", {public = true})
    add_deps("ImGui")
