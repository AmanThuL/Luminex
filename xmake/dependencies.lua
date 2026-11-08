-- Dear ImGui core and SDL3 platform backend share one pinned vendored tree.
target("ImGui")
    set_kind("static")
    add_files("../ThirdParty/imgui/*.cpp")
    add_files("../ThirdParty/imgui/backends/imgui_impl_sdl3.cpp")
    add_includedirs("../ThirdParty/imgui", {public = true})
    add_includedirs("../ThirdParty/imgui/backends", {public = true})
    add_packages("libsdl3")
    add_frameworks("Cocoa")

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

-- Editor-only transform handles; the pin and license are recorded by xmake/setup.lua and ADR 0032.
target("ImGuizmo")
    set_kind("static")
    set_warnings("none")
    add_files("../ThirdParty/ImGuizmo/src/ImGuizmo.cpp")
    add_includedirs("../ThirdParty/ImGuizmo/src", {public = true})
    add_deps("ImGui")
