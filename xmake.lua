set_project("Luminex")
set_version("0.1.0")
set_languages("c++23")
add_rules("mode.debug", "mode.release")
set_defaultmode("debug")
set_warnings("allextra")
set_policy("build.warning", true)

add_requires("libsdl3", "glm", "spdlog", "catch2 3.x", "cgltf", "stb")

-- Slang -> readable MSL -> .metallib (ADR 0003). Emits both artifacts into
-- <targetdir>/Shaders/; a target opts in with add_rules("slang2metallib") plus
-- add_files("Shaders/*.slang"). App and Tests both load the artifacts at runtime, so both
-- opt in -- which is why they must not share a target directory: two targets emitting the
-- same paths race under a parallel build. See the Tests target's set_targetdir below.
rule("slang2metallib")
    set_extensions(".slang")
    on_buildcmd_file(function (target, batchcmds, sourcefile, opt)
        -- Two targets emitting the same shader paths race under a parallel build. The rule
        -- rejects a shared target directory before either compiler can write partial output.
        import("core.project.project")
        for _, other in pairs(project.targets()) do
            if other:name() ~= target:name() and other:rule("slang2metallib")
               and path.absolute(other:targetdir()) == path.absolute(target:targetdir()) then
                os.raise("slang2metallib: targets '%s' and '%s' share targetdir '%s'; give one "
                         .. "its own set_targetdir", target:name(), other:name(), target:targetdir())
            end
        end

        local outdir = path.join(target:targetdir(), "Shaders")
        local name = path.basename(sourcefile)
        local msl = path.join(outdir, name .. ".metal")
        local slangc = path.join(os.projectdir(), "ThirdParty/slang/bin/slangc")
        batchcmds:mkdir(outdir)
        batchcmds:show_progress(opt.progress, "${color.build.object}slang %s", sourcefile)
        batchcmds:vrunv(slangc, {sourcefile, "-target", "metal", "-o", msl})
        -- Offline metallib precompile is optional because Command Line Tools installations may
        -- not include the Metal toolchain; the runtime can compile the emitted MSL instead.
        local has_metal = try {function ()
            os.runv("xcrun", {"-sdk", "macosx", "metal", "--version"}); return true
        end}
        if has_metal then
            local lib = path.join(outdir, name .. ".metallib")
            batchcmds:vrunv("xcrun", {"-sdk", "macosx", "metal", "-std=metal4.0",
                                      "-frecord-sources", "-gline-tables-only", "-o", lib, msl})
        end
        -- Every .slang source, not just this one, and deliberately so: `import Shadow;` makes
        -- ShadowSmoke.slang depend on Shadow.slang, and nothing here can see that edge --
        -- slangc's CLI has no depfile mode wired up, and parsing `import` lines out of the
        -- source would be a second, silently-drifting implementation of Slang's module
        -- resolution. Without this, editing a module leaves every importer's .metal/.metallib
        -- stale. The conservative glob costs a full shader rebuild -- ten files, well under a
        -- second in total -- whenever any shader changes, which at this scale is far cheaper
        -- than one stale metallib.
        batchcmds:add_depfiles(os.files(path.join(os.projectdir(), "Shaders/*.slang")))
        batchcmds:set_depmtime(os.mtime(msl))
        batchcmds:set_depcache(target:dependfile(msl))
    end)

target("Core")
    set_kind("static")
    add_files("Source/Core/*.cpp")
    add_includedirs("Source", {public = true})
    add_packages("spdlog", {public = true})

-- Dear ImGui (docking) + its SDL3 platform backend + native Metal 4 renderer backend, vendored
-- wholesale like metal-cpp/slang (task("setup") below): the xrepo `imgui` package has no
-- Metal backend config. It never compiles imgui_impl_metal*, so using it would mean stitching
-- in a second, independently-versioned source tree just for the Metal
-- backend. One pinned tree, one target: core + both backends stay locked to the same commit.
-- Pinned past the v1.92.7-docking tag (see imgui_pin below): imgui_impl_metal4.{h,mm} --
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
    add_files("ThirdParty/imgui/*.cpp")
    add_files("ThirdParty/imgui/backends/imgui_impl_sdl3.cpp")
    add_files("ThirdParty/imgui/backends/imgui_impl_metal4.mm")
    add_includedirs("ThirdParty/imgui", {public = true})
    add_includedirs("ThirdParty/imgui/backends", {public = true})
    add_includedirs("ThirdParty/metal-cpp")
    add_defines("IMGUI_IMPL_METAL_CPP", {public = true})
    add_packages("libsdl3")
    add_frameworks("Metal", "QuartzCore", "Cocoa")
    add_mxxflags("-fno-objc-arc")

includes("RHI/xmake.lua")

-- Camera + procedural mesh helpers on top of RHI. glm is public so App/Tests only need
-- "Render" in their own add_deps to inherit its include path.
target("Render")
    set_kind("static")
    add_files("Source/Render/*.cpp")
    add_deps("Core", "RHI")
    add_packages("glm", {public = true})

-- Procedural mesh generators on top of Render. glm is public
-- so App/Tests only need "Engine" in their own add_deps to inherit its include path (mirrors
-- the Render target above).
target("Engine")
    set_kind("static")
    add_files("Source/Engine/*.cpp")
    add_deps("Core", "RHI", "Render")
    add_packages("glm", {public = true})
    -- cgltf (parsing) + stb (image decode) back GltfLoader.{h,cpp}; both are implementation
    -- details (GltfLoader.h's public interface carries no cgltf/stb types), so private.
    add_packages("cgltf", "stb")

-- Offline mip bake CLI (Engine/TextureBake.h's thin wrapper). `xmake setup` builds and invokes
-- this against every fetched glTF's referenced images; Scene.cpp prefers its DDS output over the
-- runtime fallback path.
target("TextureBake")
    set_kind("binary")
    add_files("Tools/TextureBake/main.cpp")
    add_deps("Core", "RHI", "Render", "Engine")
    add_packages("stb")

target("App")
    set_kind("binary")
    add_files("Source/App/*.cpp")
    add_deps("Core", "RHI", "RHIMetal4ImGui", "Render", "Engine", "ImGui")
    add_packages("libsdl3", "glm")
    -- Compile every shader for App so test-only entries cannot silently drift out of build health.
    add_rules("slang2metallib")
    add_files("Shaders/*.slang")

target("Tests")
    set_kind("binary")
    set_default(false)
    -- Keep test shader outputs separate from App; both targets may compile in parallel.
    -- Use singular "test" because the default macOS filesystem aliases it with the Tests binary.
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/test")
    add_files("Tests/*.cpp", "Source/App/AppOptions.cpp", "Source/App/ExposureReset.cpp",
              "Source/App/FrameRecordRing.cpp", "Source/App/GraphInspectorModel.cpp",
              "Source/App/PassTimingHistory.cpp")
    add_deps("Core", "RHI", "Render", "Engine")
    add_packages("catch2", "glm")
    -- ToolsTests needs a stable path to the Python suite when launched from the test build dir.
    add_defines('LMX_REPO_ROOT="$(projectdir)"')
    -- GPU tests load shaders relative to the test binary and do not depend on an App build.
    add_rules("slang2metallib")
    add_files("Shaders/*.slang")
    add_tests("unit", {runargs = {"~[gpu]"}})
    add_tests("gpu", {runargs = {"[gpu]"}})

-- M5.1 RHI execution-model experiment (docs/specs/2026-08-12-m5.1-rhi-execution-model-design.md):
-- a frozen, non-default research spike under Experiments/NoApi/, namespace lmx::noapi. All four
-- targets are set_default(false) -- a plain `xmake` never builds them (spec section 5) -- and this
-- section is the only place outside Experiments/ the milestone touches.

-- The API-neutral workload manifest (spec sections 6-7): the frozen representative graph, stress
-- case tables, and deterministic synthetic asset generation shared by both encoders. Links Core
-- only -- no RHI/ and no Experiments/NoApi/Include (the prototype's own headers) -- so neither
-- adapter's headers need to appear in the manifest's dependency graph for it to compile.
target("NoApiManifest")
    set_kind("static")
    set_default(false)
    add_files("Experiments/NoApi/Workload/*.cpp")
    add_includedirs("Experiments/NoApi", {public = true})
    add_deps("Core")

-- The prototype library (spec section 5): the address-first Metal 4 interface implementation.
-- Links metal-cpp and Core only; Experiments/NoApi/Include is its public surface, authored
-- alongside this target. Placeholder.cpp keeps the target linkable until stage 2's real sources
-- land under Experiments/NoApi/Source.
target("NoApiProto")
    set_kind("static")
    set_default(false)
    add_files("Experiments/NoApi/Source/*.cpp")
    add_includedirs("Experiments/NoApi/Include", {public = true})
    add_includedirs("ThirdParty/metal-cpp")
    add_frameworks("Metal", "QuartzCore", "Foundation")
    add_deps("Core")

-- Prototype-only tests (spec section 5): smoke and misuse death-tests on the prototype side.
-- metal-cpp appears here only for the capture-quality probe, which drives MTLCaptureManager
-- around a prototype frame; every other case uses the prototype interface alone.
target("NoApiTests")
    set_kind("binary")
    set_default(false)
    add_files("Experiments/NoApi/Tests/NoApiTests.cpp")
    add_includedirs("Experiments/NoApi/Tests")
    add_includedirs("ThirdParty/metal-cpp")
    add_frameworks("Metal", "Foundation")
    add_deps("Core", "NoApiProto")
    add_packages("catch2")

-- The comparison host (spec section 5): links both the production RHI and NoApiProto, executing
-- the shared workload manifest through a maintained-RHI adapter and a prototype adapter -- App's
-- production dependency stack minus SDL3/ImGui, which the offscreen bench never needs.
-- --check-manifest is the stage 1 consistency test: it declares the manifest's representative
-- graph to the production RenderGraph and asserts the compiled record against the manifest's
-- frozen schedule and barrier expectations. --run-graph=rhi (Stage 3) hand-encodes the same
-- manifest through production rhi::CommandList calls (Experiments/NoApi/Bench/RhiAdapter.*), and
-- --run-graph=noapi encodes it through the address-first prototype
-- (Experiments/NoApi/Bench/NoApiAdapter.*); the adapter-neutral runner
-- (Experiments/NoApi/Bench/Runner.*) drives both.
target("NoApiBench")
    set_kind("binary")
    set_default(false)
    -- Own targetdir: the slang2metallib rule below rejects a target directory shared with App or
    -- Tests, which also compile shaders in parallel.
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/noapibench")
    add_files("Experiments/NoApi/Tests/NoApiBenchMain.cpp", "Experiments/NoApi/Bench/*.cpp")
    add_includedirs("Experiments/NoApi")
    add_deps("Core", "RHI", "Render", "Engine", "NoApiManifest", "NoApiProto")
    add_packages("glm")
    -- NoApiProto is a static library and xmake does not propagate its private framework list to
    -- this binary's link line, so the comparison host names the same frameworks itself.
    add_frameworks("Metal", "QuartzCore", "Foundation")
    -- P03/P04 bind the unmodified production ShadowPass/ScenePass pipelines (spec section 6), so
    -- this target compiles exactly those two production shaders plus the shared modules they
    -- import -- not production's own P05-P12-equivalent shaders (BloomThreshold.slang and
    -- friends), which Experiments/NoApi/Shaders' distilled kernels below replace and would
    -- otherwise collide with by basename in this target's shared Shaders/ output directory.
    -- The Lighting/Shadow modules are also what the prototype's own ProtoScene.slang frontend
    -- imports, by relative path: both encoders run the same shading math, only the binding
    -- frontend differs (spec section 2).
    add_rules("slang2metallib")
    add_files("Shaders/ShadowPass.slang", "Shaders/ScenePass.slang", "Shaders/Shadow.slang",
              "Shaders/Lighting.slang")
    -- M5.1 Stage 4's stress cases (H01-H24, S-BIND, I1-I4, plan Stage 4 items 1-3) reuse three of
    -- the production RHI's own test oracles unchanged rather than duplicating their kernels.
    add_files("Shaders/ComputeSmoke.slang", "Shaders/IndirectSmoke.slang", "Shaders/SamplerSmoke.slang")
    add_files("Experiments/NoApi/Shaders/*.slang")

local metalcpp_pin = "release/metal-cpp_macOS26.4_iOS26.4"
local metalcpp_commit = "c595afef4a5dc388f4047cd0c69f9e7f9468d9ed"
local slang_pin    = "v2026.14.1"
local slang_sha256 = "a1c5ecae0d2425b13fe7f616686f2df7cc7028d3f6a85fb717497cf98bee3d0a"
-- The docking-branch pin is the first revision used here with the native Metal 4 backend.
-- Re-pinning requires rebasing the texture-removal patch and verifying ARC and ImTextureID
-- conventions; setup rejects a mismatched checkout before applying the patch.
local imgui_pin    = "83f668625ad45364de71d385aeb6a5dd04bee02e"

local helmet_pin = "2bac6f8c57bf471df0d2a1e8a8ec023c7801dddf" -- KhronosGroup/glTF-Sample-Assets
local helmet_sha256 = "a1e3b04de97b11de564ce6e53b95f02954a297f0008183ac63a4f5974f6b32d8"
local helmet_license_sha256 = "424cf69d2b709c8cd1316c72671e9f8370a15e10fee03734c2a95072152f2f5d"
local helmet_metadata_sha256 = "3ad51b9684cf1e8d13e2909e179d4c5e2e3ff17b5c38ce044d1697ea6a6aefbd"
local sponza_url = "https://casual-effects.com/g3d/data10/common/model/crytek_sponza/sponza.zip"
local sponza_info_url = "https://casual-effects.com/g3d/data10/common/model/crytek_sponza/info.js"
local sponza_archive_sha256 = "da005cbee0be2df2abc8513f3ceb61bcb6f69aac112babcd9c00169a27c2770c"
local sponza_info_sha256 = "c584c17ae5514e6218c2f19127f06106d81f472320a8f9a17a94cedfd119a16e"
local sponza_sha256 = "9f1960875b3a4781a012f9745a88576aaf596acccff4d48d6947674618f9a4a0"
local material_lab_environment_url =
    "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/studio_small_09_1k.hdr"
local material_lab_environment_sha256 =
    "e7cfda5f4e98e623db12b8bfd0184e048488e4855d9c83e2751fb44a32e80c45"

task("setup")
    -- An explicit (even empty) options table is required for -P to work with this task: a
    -- set_menu with no "options" key rejects -P as an unrecognized option outright.
    set_menu {usage = "xmake setup", description = "fetch pinned dependencies and scene assets",
              options = {}}
    on_run(function ()
        if not os.isdir("ThirdParty/metal-cpp") then
            os.mkdir("ThirdParty/metal-cpp")
            os.execv("git", {"-C", "ThirdParty/metal-cpp", "init", "-q"})
            os.execv("git", {"-C", "ThirdParty/metal-cpp", "remote", "add", "origin",
                             "https://github.com/apple/metal-cpp.git"})
            os.execv("git", {"-C", "ThirdParty/metal-cpp", "fetch", "--depth", "1", "origin",
                             metalcpp_commit})
            os.execv("git", {"-C", "ThirdParty/metal-cpp", "checkout", "-q", "FETCH_HEAD"})
        end
        local metalcpp_head = os.iorunv("git", {"-C", "ThirdParty/metal-cpp",
                                                 "rev-parse", "HEAD"}):trim()
        assert(metalcpp_head == metalcpp_commit,
               format("ThirdParty/metal-cpp is at %s; expected %s", metalcpp_head,
                      metalcpp_commit))
        os.execv("git", {"-C", "ThirdParty/metal-cpp", "diff", "--quiet", "HEAD", "--"})
        if not os.isfile("ThirdParty/slang/bin/slangc") then
            local ver = slang_pin:sub(2)  -- strip leading v
            local zip = format("slang-%s-macos-aarch64.zip", ver)
            local url = format("https://github.com/shader-slang/slang/releases/download/%s/%s", slang_pin, zip)
            os.mkdir("ThirdParty/slang")
            os.execv("curl", {"-L", "--max-time", "600", "-o", "ThirdParty/" .. zip, url})
            os.execv("unzip", {"-q", "-o", "ThirdParty/" .. zip, "-d", "ThirdParty/slang"})
            os.rm("ThirdParty/" .. zip)
            try {
                function ()
                    os.execv("xattr", {"-dr", "com.apple.quarantine", "ThirdParty/slang"})
                end
            }
        end
        local slang_hash = os.iorunv("shasum", {"-a", "256",
                                                 "ThirdParty/slang/bin/slangc"}):match("^(%x+)")
        assert(slang_hash == slang_sha256,
               format("ThirdParty/slang/bin/slangc checksum mismatch; expected %s",
                      slang_sha256))
        if not os.isdir("ThirdParty/imgui") then
            -- Fetch the exact untagged commit without cloning the rest of the docking branch.
            os.mkdir("ThirdParty/imgui")
            os.execv("git", {"-C", "ThirdParty/imgui", "init", "-q"})
            os.execv("git", {"-C", "ThirdParty/imgui", "remote", "add", "origin",
                             "https://github.com/ocornut/imgui.git"})
            os.execv("git", {"-C", "ThirdParty/imgui", "fetch", "--depth", "1", "origin", imgui_pin})
            os.execv("git", {"-C", "ThirdParty/imgui", "checkout", "-q", "FETCH_HEAD"})
        end
        local imgui_head = os.iorunv("git", {"-C", "ThirdParty/imgui", "rev-parse", "HEAD"}):trim()
        assert(imgui_head == imgui_pin,
               format("ThirdParty/imgui is at %s; expected pinned commit %s", imgui_head, imgui_pin))

        local imgui_patch = path.join(os.projectdir(),
                                      "Tools/Patches/imgui-metal4-remove-texture.patch")
        local patch_already_applied = false
        try {
            function ()
                os.iorunv("git", {"-C", "ThirdParty/imgui", "apply", "--reverse", "--check",
                                  imgui_patch})
                patch_already_applied = true
            end,
            catch {
                function ()
                end
            }
        }
        if not patch_already_applied then
            os.execv("git", {"-C", "ThirdParty/imgui", "apply", "--check", imgui_patch})
            os.execv("git", {"-C", "ThirdParty/imgui", "apply", imgui_patch})
        end
        local imgui_diff = os.iorunv("git", {"-C", "ThirdParty/imgui", "diff",
                                              "--no-ext-diff", "--binary"})
        assert(imgui_diff == io.readfile(imgui_patch),
               "ThirdParty/imgui has tracked changes beyond the maintained patch")
        local imgui_untracked = os.iorunv("git", {"-C", "ThirdParty/imgui", "ls-files",
                                                   "--others", "--exclude-standard"}):trim()
        assert(imgui_untracked == "", "ThirdParty/imgui contains untracked files")
        os.mkdir("Assets/Fetched/DamagedHelmet")
        local helmet_files = {
            {name = "DamagedHelmet.glb", source = "glTF-Binary/DamagedHelmet.glb"},
            {name = "LICENSE.md", source = "LICENSE.md"},
            {name = "metadata.json", source = "metadata.json"}
        }
        for _, file in ipairs(helmet_files) do
            local destination = path.join("Assets/Fetched/DamagedHelmet", file.name)
            if not os.isfile(destination) then
                -- Publish each fetched file only after curl has completed it successfully.
                local part = destination .. ".part"
                os.execv("curl", {"-L", "--fail", "--max-time", "600", "-o", part,
                    format("https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/%s/Models/DamagedHelmet/%s",
                           helmet_pin, file.source)})
                os.mv(part, destination)
            end
        end
        local material_lab_dir = "Assets/Fetched/MaterialLab"
        local material_lab_environment = path.join(material_lab_dir, "studio_small_09_1k.hdr")
        os.mkdir(material_lab_dir)
        if not os.isfile(material_lab_environment) then
            -- Publish the environment only after curl has completed it successfully.
            local part = material_lab_environment .. ".part"
            os.tryrm(part)
            os.execv("curl", {"-L", "--fail", "--max-time", "600", "-o", part,
                              material_lab_environment_url})
            os.mv(part, material_lab_environment)
        end
        local material_lab_environment_hash =
            os.iorunv("shasum", {"-a", "256", material_lab_environment}):match("^(%x+)")
        assert(material_lab_environment_hash == material_lab_environment_sha256,
               format("MaterialLab environment checksum mismatch; expected %s",
                      material_lab_environment_sha256))
        io.writefile(path.join(material_lab_dir, "LICENSE.txt"), [[
Studio Small 09 is published by Poly Haven under CC0 1.0 Universal.

Asset page: https://polyhaven.com/a/studio_small_09
Poly Haven license: https://polyhaven.com/license
CC0 legal code: https://creativecommons.org/publicdomain/zero/1.0/legalcode

Attribution is not required under CC0. Original author: Sergej Majboroda.
]])
        io.writefile(path.join(material_lab_dir, "metadata.json"), format([[
{
  "name": "Studio Small 09",
  "author": "Sergej Majboroda",
  "provider": "Poly Haven",
  "assetPage": "https://polyhaven.com/a/studio_small_09",
  "downloadUrl": "%s",
  "file": "studio_small_09_1k.hdr",
  "sha256": "%s",
  "license": "CC0-1.0",
  "licenseUrl": "https://polyhaven.com/license"
}
]], material_lab_environment_url, material_lab_environment_sha256))
        if not os.isfile("Assets/Fetched/Sponza/Sponza.gltf")
           or not os.isfile("Assets/Fetched/Sponza/ARCHIVE_INFO.js")
           or not os.isfile("Assets/Fetched/Sponza/COPYRIGHT.txt") then
            local archive = "Assets/Fetched/.sponza.zip"
            local archive_part = archive .. ".part"
            local source = "Assets/Fetched/.sponza-source"
            local stage = "Assets/Fetched/.sponza-stage"
            os.tryrm(archive)
            os.tryrm(archive_part)
            os.tryrm(source)
            os.tryrm(stage)
            os.execv("curl", {"-L", "--fail", "--max-time", "600", "-o", archive_part,
                              sponza_url})
            os.mv(archive_part, archive)
            local archive_hash = os.iorunv("shasum", {"-a", "256", archive})
                                         :match("^(%x+)")
            assert(archive_hash == sponza_archive_sha256,
                   format("Sponza archive checksum mismatch; expected %s",
                          sponza_archive_sha256))
            os.mkdir(source)
            os.execv("unzip", {"-q", archive, "-d", source})
            os.execv("curl", {"-L", "--fail", "--max-time", "600", "-o",
                              path.join(source, "ARCHIVE_INFO.js"), sponza_info_url})
            local info_hash = os.iorunv("shasum", {"-a", "256",
                                                    path.join(source, "ARCHIVE_INFO.js")})
                                      :match("^(%x+)")
            assert(info_hash == sponza_info_sha256,
                   format("Sponza archive metadata checksum mismatch; expected %s",
                          sponza_info_sha256))
            os.execv("python3", {"Tools/convert_obj_to_gltf.py",
                                  path.join(source, "sponza.obj"),
                                  path.join(source, "sponza.mtl"), stage})
            os.cp(path.join(source, "copyright.txt"), path.join(stage, "COPYRIGHT.txt"))
            os.cp(path.join(source, "ARCHIVE_INFO.js"), path.join(stage, "ARCHIVE_INFO.js"))
            local staged_hash = os.iorunv("python3", {"Tools/tree_digest.py", stage}):trim()
            assert(staged_hash == sponza_sha256,
                   format("converted Sponza checksum mismatch; expected %s", sponza_sha256))
            os.tryrm("Assets/Fetched/Sponza")
            os.mv(stage, "Assets/Fetched/Sponza")
            os.tryrm(source)
            os.tryrm(archive)
        end
        local helmet_hash = os.iorunv("shasum", {"-a", "256",
                                                  "Assets/Fetched/DamagedHelmet/DamagedHelmet.glb"})
                                    :match("^(%x+)")
        assert(helmet_hash == helmet_sha256,
               format("Damaged Helmet checksum mismatch; expected %s", helmet_sha256))
        local helmet_license_hash = os.iorunv("shasum", {"-a", "256",
            "Assets/Fetched/DamagedHelmet/LICENSE.md"}):match("^(%x+)")
        assert(helmet_license_hash == helmet_license_sha256,
               format("Damaged Helmet license checksum mismatch; expected %s",
                      helmet_license_sha256))
        local helmet_metadata_hash = os.iorunv("shasum", {"-a", "256",
            "Assets/Fetched/DamagedHelmet/metadata.json"}):match("^(%x+)")
        assert(helmet_metadata_hash == helmet_metadata_sha256,
               format("Damaged Helmet metadata checksum mismatch; expected %s",
                      helmet_metadata_sha256))
        local sponza_hash = os.iorunv("python3", {"Tools/tree_digest.py",
                                                  "Assets/Fetched/Sponza"}):trim()
        assert(sponza_hash == sponza_sha256,
               format("Sponza tree checksum mismatch; expected %s", sponza_sha256))

        -- Deterministic offline mip bake (Source/Engine/TextureBake.h): every base-color and
        -- normal image the two glTF/GLB files reference gets a sibling Baked/image<N>.dds that
        -- Engine/Scene.cpp's ensureUploaded prefers over the runtime fallback path. Re-running
        -- setup is cheap here too -- Tools/bake_gltf_textures.py skips any image whose manifest
        -- matches the current source hash, role-specific filter, and bake-tool version.
        import("core.project.config")
        import("core.project.project")
        -- setup is a one-command bootstrap, including xrepo packages the bake tool links. CI and
        -- other non-interactive callers have no stdin for xmake's install confirmation, so accept
        -- the pinned package plan here instead of requiring a separate `xmake f -y` beforehand.
        os.execv("xmake", {"build", "-P", ".", "-y", "TextureBake"})
        config.load() -- needed for targetfile() below to resolve the mode-scoped build path
        local texturebake_bin = project.target("TextureBake"):targetfile()
        os.execv("python3", {"Tools/bake_gltf_textures.py", "Assets/Fetched/Sponza/Sponza.gltf",
                             texturebake_bin})
        os.execv("python3", {"Tools/bake_gltf_textures.py",
                             "Assets/Fetched/DamagedHelmet/DamagedHelmet.glb", texturebake_bin})

        print("setup done: metal-cpp %s, slang %s, imgui %s, Sponza archive %s, helmet %s, " ..
              "MaterialLab environment %s", metalcpp_pin, slang_pin, imgui_pin,
              sponza_archive_sha256, helmet_pin, material_lab_environment_sha256)
    end)

task("format")
    set_menu {usage = "xmake format [--check]", description = "clang-format all sources",
              options = {{nil, "check", "k", nil, "check only, do not modify"}}}
    on_run(function ()
        import("core.base.option")
        local args = {"-style=file"}
        table.insert(args, option.get("check") and "--dry-run" or "-i")
        if option.get("check") then table.insert(args, "--Werror") end
        for _, f in ipairs(os.files("Source/**.h")) do table.insert(args, f) end
        for _, f in ipairs(os.files("Source/**.cpp")) do table.insert(args, f) end
        for _, f in ipairs(os.files("RHI/**.h")) do table.insert(args, f) end
        for _, f in ipairs(os.files("RHI/**.cpp")) do table.insert(args, f) end
        for _, f in ipairs(os.files("Tests/**.cpp")) do table.insert(args, f) end
        os.execv("clang-format", args)
    end)

task("policy")
    set_menu {usage = "xmake policy", description = "check repository and C++ layout policy"}
    on_run(function ()
        os.execv("python3", {"Tools/check_project_policy.py"})
        os.execv("xmake", {"project", "-k", "compile_commands"})
        os.execv("python3", {"Tools/check_cpp_comments.py", "--public-api-docs", "error"})
        os.execv("python3", {"Tools/check_rhi_headers.py"})
        os.execv("python3", {"Tools/check_cpp_layout.py"})
    end)
