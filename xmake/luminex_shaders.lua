-- Consumer shader entries and backend library names share the component's Slang rule.
local repository_root = path.directory(os.scriptdir())
local pending_shaders = lmx_pending_files("shaders")
local pending_test_shaders = lmx_pending_files("testShaders")

-- Registered directly so every helper below is created in the runtime sandbox.
function lmx_configure_shader_library(target)
    import("core.project.config")
    local extensions = {null = ".nulllib", metal4 = ".metal4lib", d3d12 = ".d3d12lib"}
    local backend = config.get("rojorhi_backend")
    local extension = assert(extensions[backend], "unknown RojoRHI shader backend: " .. tostring(backend))
    target:add("defines", 'LMX_SHADER_LIBRARY_EXTENSION="' .. extension .. '"')

    local kind = target:values("lmx.shader.kind")
    if not kind then return end
    assert(kind == "app" or kind == "tests" or kind == "bench", "unknown shader entry kind: " .. kind)

    local function lmx_shader_entries(entry_kind)
        local excluded = {}
        local function exclude(patterns)
            for _, pattern in ipairs(patterns) do
                local files = os.files(pattern)
                assert(#files > 0, "pending shader pattern matches no files: " .. pattern)
                for _, file in ipairs(files) do
                    excluded[path.absolute(file)] = true
                end
            end
        end
        exclude(pending_shaders)
        exclude(pending_test_shaders)
        local candidates = {}
        if entry_kind == "bench" then
            candidates = {path.join(repository_root, "Shaders/Tests/FrameDataQuad.slang")}
        else
            candidates = os.files(path.join(repository_root, "Shaders/Passes/**.slang"))
            if entry_kind == "tests" then
                table.join2(candidates, os.files(path.join(repository_root, "Shaders/Tests/*.slang")))
            end
        end
        local entries, basenames = {}, {}
        for _, file in ipairs(candidates) do
            if entry_kind == "bench" or not excluded[path.absolute(file)] then
                local text = assert(io.readfile(file), "cannot read shader: " .. file)
                    :gsub("/%*.-%*/", ""):gsub("//[^\n]*", "")
                if text:find('%[%s*shader%s*%(%s*"(%w+)"%s*%)%s*%]') then
                    local basename = path.basename(file):lower()
                    assert(not basenames[basename], "duplicate shader entry basename: " .. basename)
                    basenames[basename] = true
                    table.insert(entries, file)
                end
            end
        end
        table.sort(entries)
        assert(#entries > 0, "no shader entries for " .. entry_kind)
        return entries
    end

    local includedirs = {path.join(repository_root, "Shaders/Common")}
    if kind == "tests" or kind == "app" then
        local passes = os.dirs(path.join(repository_root, "Shaders/Passes/*"))
        table.sort(passes)
        table.join2(includedirs, passes)
    end
    if kind == "tests" then
        table.insert(includedirs, path.join(repository_root, "Shaders/Tests"))
    end
    target:values_set("slang.slangc", path.join(repository_root,
        os.host() == "windows" and "ThirdParty/slang/bin/slangc.exe" or "ThirdParty/slang/bin/slangc"))
    target:values_set("slang.moduledir", path.join(repository_root, "RojoRHI/Shaders/RojoRHI"))
    target:values_set("slang.includedirs", table.unpack(includedirs))
    if kind == "app" then
        target:values_set("slang.layout_probe", path.join(repository_root, "Shaders/Layout/LuminexLayout.slang"))
        local layout_map = {
            "DisplayRoot=lmx::render::DisplayRoot",
            -- ---- scene ----
            -- ---- visibility ----
            -- ---- lights ----
            -- ---- post ----
            -- ---- temporal ----
        }
        target:values_set("slang.layout_map", table.unpack(layout_map))
        target:values_set("slang.layout_include", "Render/Passes/Display/DisplayRoots.h")
    elseif kind == "tests" then
        target:values_set("slang.layout_probe", path.join(repository_root, "Shaders/Layout/TestLayout.slang"))
        local layout_map = {
            "BufferHazardRoot=lmx::test::BufferHazardRoot",
            "ComputeImageRoot=lmx::test::ComputeImageRoot",
            "FullscreenSampleRoot=lmx::test::FullscreenSampleRoot",
            "SamplerRoot=lmx::test::SamplerRoot",
            "VisibilityDepthRoot=lmx::test::VisibilityDepthRoot",
            -- ---- scene ----
            -- ---- visibility ----
            -- ---- lights ----
            -- ---- post ----
            -- ---- temporal ----
        }
        target:values_set("slang.layout_map", table.unpack(layout_map))
        target:values_set("slang.layout_include", "Support/OracleRoots.h")
    end
    local entries = lmx_shader_entries(kind)
    target:values_set("lmx.shader.entries", table.unpack(entries))
    target:add("files", table.unpack(entries))
end

function lmx_apply_shader_rule(kind)
    add_rules("rojorhi.slang")
    if kind == "app" or kind == "tests" then add_rules("rojorhi.shader_layout") end
    set_values("lmx.shader.kind", kind)
    on_load(lmx_configure_shader_library)
end
