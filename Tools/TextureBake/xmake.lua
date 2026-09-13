-- Offline mip bake CLI (Asset/TextureBake.h's thin wrapper). `xmake setup` builds and invokes
-- this against every fetched glTF's referenced images; Scene.cpp prefers its DDS output over the
-- runtime fallback path.
target("TextureBake")
    set_kind("binary")
    add_files("main.cpp")
    add_deps("Core", "Asset")
    add_packages("stb")
