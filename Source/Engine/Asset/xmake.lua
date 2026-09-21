-- CPU decoding and generation share descriptor vocabulary without linking GPU interfaces.
target("Asset")
    set_kind("static")
    add_files("*.cpp", "Image/*.cpp", "Model/*.cpp", "Texture/*.cpp")
    add_deps("Core")
    add_includedirs("../../../RojoRHI/Include", {public = true})
    add_packages("cgltf", "stb")
