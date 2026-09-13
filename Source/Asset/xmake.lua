-- CPU decoding and generation share descriptor vocabulary without linking GPU interfaces.
target("Asset")
    set_kind("static")
    add_files("*.cpp")
    add_deps("Core")
    add_includedirs("../../RHI/Include", {public = true})
    add_packages("cgltf", "stb")
