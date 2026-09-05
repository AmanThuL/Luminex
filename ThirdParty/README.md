Populated by `xmake setup` — see xmake.lua. Never committed.

## Inventory

- `metal-cpp` — apple/metal-cpp, pinned release (see `metalcpp_pin` in `xmake.lua`). Apache
  License 2.0, Copyright © 2024 Apple Inc.
- `slang` — shader-slang/slang, pinned release (see `slang_pin` in `xmake.lua`). Apache License
  2.0 with LLVM exception.
- `imgui` — ocornut/imgui, pinned docking-branch commit (see `imgui_pin` in `xmake.lua`), patched
  for the Metal 4 backend (`Tools/Patches/imgui-metal4-remove-texture.patch`). That patch carries
  three changes: the texture-removal entry point the RHI needs; a fix giving the backend's
  per-frame-slot shared events one pending value each — the upstream single counter deadlocks
  `ImGui_ImplMetal4_NewFrame` as soon as a platform window renders; and an autorelease pool plus
  explicit releases around the platform-window path, which under this non-ARC build otherwise
  leaks one drawable per frame of every extra window. MIT License,
  Copyright (c) 2014-2026 Omar Cornut.
- `imgui-node-editor` — thedmd/imgui-node-editor, pinned commit (see `node_editor_pin` in
  `xmake.lua`), patched for our Dear ImGui 1.93 pin (`Tools/Patches/imgui-node-editor-imgui-1.93.patch`).
  MIT License, Copyright (c) 2019 Michał Cichoń.
