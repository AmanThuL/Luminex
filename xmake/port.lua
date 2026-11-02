-- Render families absent from the configured build. Each implementation removes its block;
-- remove this file when every family builds. Paths below also define exact policy coverage.
local repository_root = path.directory(os.scriptdir())
local port_path = path.join(repository_root, "xmake/port.lua")

local pending_families = {
    {
        family = "LIGHTS",
        sources = {
            "Source/Render/Passes/LocalLights/LightDebugStage.cpp",
            "Source/Render/Renderer/RendererLighting.cpp",
        },
        tests = {
            "Tests/Render/Passes/LocalLights/GpuLightDebugViewTests.cpp",
            "Tests/Render/Passes/LocalLights/GpuLocalLightTests.cpp",
            "Tests/Render/Passes/LocalLights/GpuRendererLightingTests.cpp",
            "Tests/Render/Passes/LocalLights/RenderZeroLightInvariantTests.cpp",
            "Tests/Render/Passes/LocalLights/LightCaptureLayoutTests.cpp",
            "Tests/Render/Passes/Temporal/GpuZeroLightHistoryTests.cpp",
            "Tests/App/Model/Performance/MeasurementSampleTests.cpp",
        },
        shaders = {
            "Shaders/Passes/LocalLights/LightDebugView.slang",
        },
        testShaders = {
            "Shaders/Tests/PunctualLightOracle.slang",
        },
    },
    {
        family = "BENCH",
        sources = {
            "Benchmarks/FrameData/*.cpp",
        },
        testShaders = {
            "Shaders/Tests/FrameDataQuad.slang",
        },
        targets = {
            "FrameDataBench",
        },
    },
}

lmx_port_pending = pending_families

function lmx_pending_files(kind)
    local files = {}
    for _, family in ipairs(pending_families) do
        for _, pattern in ipairs(family[kind] or {}) do
            table.insert(files, path.join(repository_root, pattern))
        end
    end
    return files
end

function lmx_pending_defines()
    local defines = {}
    for _, family in ipairs(pending_families) do
        if family.family ~= "BENCH" then
            table.insert(defines, "LMX_PENDING_" .. family.family)
        end
    end
    return defines
end

function lmx_pending_target(name)
    for _, family in ipairs(pending_families) do
        for _, target in ipairs(family.targets or {}) do
            if target == name then
                return true
            end
        end
    end
    return false
end


function lmx_configure_pending(target)
    local function pending_cpp()
        local files, seen, families = {}, {}, {}
        for _, family in ipairs(pending_families) do
            assert(type(family.family) == "string" and not families[family.family],
                   "duplicate or invalid pending family")
            families[family.family] = true
            for _, kind in ipairs({"sources", "tests"}) do
                for _, pattern in ipairs(family[kind] or {}) do
                    assert(type(pattern) == "string" and not path.is_absolute(pattern)
                           and not pattern:find("\\", 1, true)
                           and not ("/" .. pattern .. "/"):find("/../", 1, true)
                           and not ("/" .. pattern .. "/"):find("/./", 1, true)
                           and not pattern:find("//", 1, true)
                           and (pattern:match("^Source/") or pattern:match("^Tests/")
                                or pattern:match("^Benchmarks/")), "invalid pending pattern")
                    local matches = os.files(path.join(repository_root, pattern))
                    assert(#matches > 0, "pending pattern matches no files: " .. pattern)
                    for _, file in ipairs(matches) do
                        local relative = path.relative(path.absolute(file), repository_root):gsub("\\", "/")
                        assert(path.extension(relative) == ".cpp" and not relative:match("^%.%./"),
                               "pending path is not an in-root cpp: " .. relative)
                        assert(not seen[relative], "duplicate pending cpp: " .. relative)
                        seen[relative] = family.family
                        table.insert(files, relative)
                    end
                end
            end
        end
        table.sort(files)
        return files
    end

    target:values_set("lmx.port.pending_version", 1)
    target:values_set("lmx.port.pending_sha256", hash.sha256(port_path))
    target:values_set("lmx.port.pending_cpp", table.unpack(pending_cpp()))
end
