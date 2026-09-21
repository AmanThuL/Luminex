target("Core")
    set_kind("static")
    add_files("*/*.cpp")
    -- Matches the visibility shader's contraction setting, as Render does for Visibility.cpp.
    add_files("Math/Frustum.cpp", {cxflags = "-ffp-contract=off"})
    add_includedirs("..", {public = true})
    add_packages("spdlog", "glm", {public = true})
