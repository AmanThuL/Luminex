-- Render families absent from the configured build. Each implementation removes its block;
-- remove this file when every family builds. Paths below also define exact policy coverage.
local repository_root = path.directory(os.scriptdir())
local port_path = path.join(repository_root, "xmake/port.lua")

local pending_families = {
    {
        family = "VISIBILITY",
        sources = {
            "Source/Render/Passes/Visibility/GpuVisibility*.cpp",
            "Source/Render/Passes/Visibility/VisibilityCapture.cpp",
            "Source/Render/Passes/Occlusion/HzbStage.cpp",
            "Source/Render/Passes/Occlusion/OcclusionReference.cpp",
            "Source/Render/Renderer/RendererOcclusion.cpp",
        },
        tests = {
            "Tests/Render/Passes/Visibility/Gpu*.cpp",
            "Tests/Render/Passes/Occlusion/Gpu*.cpp",
            "Tests/Render/Passes/Occlusion/HzbTests.cpp",
            "Tests/Render/Renderer/RendererCaptureLayoutTests.cpp",
            "Tests/App/Model/Performance/MeasurementSampleTests.cpp",
        },
        shaders = {
            "Shaders/Passes/Visibility/VisibilityClassify.slang",
            "Shaders/Passes/Visibility/VisibilityClassifyOcclusion.slang",
            "Shaders/Passes/Visibility/VisibilityEmit.slang",
            "Shaders/Passes/Visibility/VisibilityEmitSparse.slang",
            "Shaders/Passes/Visibility/VisibilityScan.slang",
            "Shaders/Passes/Occlusion/HzbDebugView.slang",
            "Shaders/Passes/Occlusion/HzbPublish.slang",
            "Shaders/Passes/Occlusion/HzbReduce.slang",
            "Shaders/Passes/Occlusion/OcclusionReference.slang",
        },
        testShaders = {
            "Shaders/Tests/HzbDepthFixture.slang",
            "Shaders/Tests/HzbReadbackFixture.slang",
            "Shaders/Tests/OcclusionProbe.slang",
        },
    },
    {
        family = "LIGHTS",
        sources = {
            "Source/Render/Passes/LocalLights/LightClusterStage.cpp",
            "Source/Render/Passes/LocalLights/LightClusterReadback.cpp",
            "Source/Render/Passes/LocalLights/LightDebugStage.cpp",
            "Source/Render/Renderer/RendererLighting.cpp",
        },
        tests = {
            "Tests/Render/Passes/LocalLights/Gpu*.cpp",
            "Tests/Render/Passes/LocalLights/RenderZeroLightInvariantTests.cpp",
        },
        shaders = {
            "Shaders/Passes/LocalLights/LightClusterCount.slang",
            "Shaders/Passes/LocalLights/LightClusterFill.slang",
            "Shaders/Passes/LocalLights/LightClusterScan.slang",
            "Shaders/Passes/LocalLights/LightDebugView.slang",
        },
        testShaders = {
            "Shaders/Tests/PunctualLightOracle.slang",
        },
    },
    {
        family = "POST",
        sources = {
            "Source/Render/Passes/Exposure/ExposureStage.cpp",
            "Source/Render/Passes/Bloom/BloomStage.cpp",
            "Source/Render/Passes/SelectionOutline/SelectionOutline.cpp",
        },
        tests = {
            "Tests/Render/Passes/Bloom/Gpu*.cpp",
            "Tests/Render/Passes/Exposure/Gpu*.cpp",
            "Tests/Render/Passes/SelectionOutline/Gpu*.cpp",
            "Tests/Render/Graph/GpuAppFrameDeclarationTests.cpp",
        },
        shaders = {
            "Shaders/Passes/Exposure/ExposureSeed.slang",
            "Shaders/Passes/Exposure/ExposureResolve.slang",
            "Shaders/Passes/Exposure/HistogramAccumulate.slang",
            "Shaders/Passes/Bloom/BloomThreshold.slang",
            "Shaders/Passes/Bloom/BloomDownsample.slang",
            "Shaders/Passes/Bloom/BloomUpsample.slang",
            "Shaders/Passes/SelectionOutline/SelectionMask.slang",
            "Shaders/Passes/SelectionOutline/SelectionOutline.slang",
        },
    },
    {
        family = "TEMPORAL",
        sources = {
            "Source/Render/Passes/Temporal/TemporalResolve*.cpp",
            "Source/Render/Passes/Temporal/VendorTemporalScaler.cpp",
            "Source/Render/Renderer/RendererTemporal.cpp",
        },
        tests = {
            "Tests/Engine/Scene/GpuSceneTableTemporalTests.cpp",
            "Tests/Render/Passes/Temporal/Gpu*.cpp",
            "Tests/Render/Passes/Temporal/RenderVendorTemporalTests.cpp",
            "Tests/App/Model/Graph/GpuGraphInspectorTests.cpp",
        },
        shaders = {
            "Shaders/Passes/Temporal/SpatialUpscale.slang",
            "Shaders/Passes/Temporal/TemporalDebugView.slang",
            "Shaders/Passes/Temporal/TemporalReproject.slang",
            "Shaders/Passes/Temporal/TemporalResolve.slang",
            "Shaders/Passes/Temporal/TemporalUpscale.slang",
            "Shaders/Passes/Temporal/VendorTemporalHistory.slang",
            "Shaders/Passes/Temporal/VendorTemporalPack.slang",
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
