local metalcpp_pin = "release/metal-cpp_macOS26.4_iOS26.4"
local metalcpp_commit = "c595afef4a5dc388f4047cd0c69f9e7f9468d9ed"
local slang_pin    = "v2026.14.1"
local slang_sha256 = "a1c5ecae0d2425b13fe7f616686f2df7cc7028d3f6a85fb717497cf98bee3d0a"
local gltf_validator_version = "2.0.0-dev.3.10"
local gltf_validator_archive_sha256 =
    "bce89ceea00b4d3191a8779018f41744b84a4539b127465d63ba83ad0f243fef"
local gltf_validator_binary_sha256 =
    "4751098c84469231c4e06e2ba0f2fe472f3143a35725c7a168b595d2110d7eef"
-- The docking-branch pin is the first revision used here with the native Metal 4 backend.
-- Re-pinning requires rebasing the texture-removal patch and verifying ARC and ImTextureID
-- conventions; setup rejects a mismatched checkout before applying the patch.
-- That patch also carries a fix to the backend's platform-window pacing: upstream signals a single
-- monotonic counter onto a per-frame-slot shared event, so the next NewFrame -- which runs on a
-- different slot -- waits forever on a value that slot's event never received. It gives each slot
-- its own pending value, and is what makes the detached Render Graph window survive past its
-- second frame. Its third hunk drains an autorelease pool around the backend's platform-window
-- create/render path and releases what it owns: the file is compiled without ARC and the frame
-- loop drains no pool, so upstream's ARC-shaped code leaked one drawable -- one IOSurface -- per
-- frame of the detached window. Re-pinning must rebase all three.
local imgui_pin    = "83f668625ad45364de71d385aeb6a5dd04bee02e"
-- thedmd/imgui-node-editor `master` at 2026-03-29 (unreleased; the repo has no tags past 0.9.1,
-- which predates the docking-branch ImGui API this project pins). Patched against our imgui_pin
-- below; re-pinning requires re-verifying the patch's version gates.
local node_editor_pin = "021aa0ea4da13fed864bafb2a92d4c5205076866"
local imguizmo_pin = "18cef5e031d8c6973d80284c67f60549fafd78c1"

local geist_version = "1.7.2"
local geist_url = "https://github.com/vercel/geist-font/releases/download/v" .. geist_version ..
                  "/geist-font-v" .. geist_version .. ".zip"
local geist_sha256 = "7fc800d2ac6b92844895196e5041aca55d814c15db70c44f79b3b83ab82b04e2"
local geist_files = {
    {source = "geist-font/Geist/ttf/Geist-Regular.ttf", name = "Geist-Regular.ttf",
     sha256 = "5c8968eafb98a4c4f47033daf29e38e284a6f2a82eb017d171ab040fe7c4b615"},
    {source = "geist-font/Geist/ttf/Geist-Medium.ttf", name = "Geist-Medium.ttf",
     sha256 = "0090e004725f6f64b841715b4167920580f883fcf9b67fc6d744089103fec101"},
    {source = "geist-font/GeistMono/ttf/GeistMono-Regular.ttf", name = "GeistMono-Regular.ttf",
     sha256 = "42d8ad2e610238e64e8abfcde3037c63f7850a73928742b7ab7229d897bcb155"},
    {source = "geist-font/OFL.txt", name = "OFL.txt",
     sha256 = "c683bfbcc7e087f5d37a54ef628f10387c451a83ddc459b151403a164ac46c90"}
}

local codicons_version = "0.0.46-24"
local codicons_url = "https://registry.npmjs.org/@vscode/codicons/-/codicons-" .. codicons_version .. ".tgz"
local codicons_sha256 = "d77bf2ed152e82c4b81288c5271a3481c61559d1a5416593756e3b0fe8a02bf1"
local codicons_files = {
    {source = "package/dist/codicon.ttf", name = "codicon.ttf",
     sha256 = "3819e4ae4b87350e7c37a5d8f24e71ada2f1f2ee58f7ce5ebc1f88e3c8c38c80"},
    {source = "package/LICENSE", name = "LICENSE",
     sha256 = "af5e030844efddbc7ab00dcfea8b019703753d4d9f5172d727c533a492aec665"}
}

local helmet_pin = "2bac6f8c57bf471df0d2a1e8a8ec023c7801dddf" -- KhronosGroup/glTF-Sample-Assets
local helmet_sha256 = "a1e3b04de97b11de564ce6e53b95f02954a297f0008183ac63a4f5974f6b32d8"
local helmet_license_sha256 = "424cf69d2b709c8cd1316c72671e9f8370a15e10fee03734c2a95072152f2f5d"
local helmet_metadata_sha256 = "3ad51b9684cf1e8d13e2909e179d4c5e2e3ff17b5c38ce044d1697ea6a6aefbd"
-- CesiumMilkTruck (CC BY 4.0) and InterpolationTest (CC0) come from the same pinned Khronos
-- commit. The truck is the fetched rigid-animation scene; InterpolationTest is a sampler
-- conformance fixture for the loader tests and is never a catalog scene.
local milk_truck_sha256 = "09371b34608116de5842d23abe260bf11acf3e1554daf334a647eb566eee5c49"
local milk_truck_license_sha256 = "2706e1f1df6be804a83ac7a108d2bf1b11d4fcdc5ec1c821ba725a659ccdebbd"
local milk_truck_metadata_sha256 = "a15b14d573d0115c40f4b6218e2f926aa311e0f214b5c8d7bf9b6801bb8fee0a"
local interpolation_test_sha256 = "a86eb331b4a083715e75fe19a1f747eac5692d5b9ff120f1eaa457c23ba72bca"
local interpolation_test_license_sha256 =
    "3a3762ba63cc52e52bbaa65f8f187e02cf05d7256e750ede4c40db611169e7e7"
local interpolation_test_metadata_sha256 =
    "726d8fad65a13382a398316600855716f780a6c15e3184ece243051da02370c5"
local sponza_url = "https://casual-effects.com/g3d/data10/common/model/crytek_sponza/sponza.zip"
local sponza_info_url = "https://casual-effects.com/g3d/data10/common/model/crytek_sponza/info.js"
local sponza_archive_sha256 = "da005cbee0be2df2abc8513f3ceb61bcb6f69aac112babcd9c00169a27c2770c"
local sponza_info_sha256 = "c584c17ae5514e6218c2f19127f06106d81f472320a8f9a17a94cedfd119a16e"
local sponza_sha256 = "9f1960875b3a4781a012f9745a88576aaf596acccff4d48d6947674618f9a4a0"
local san_miguel_url =
    "https://casual-effects.com/g3d/data10/research/model/San_Miguel/San_Miguel.zip"
local san_miguel_info_url =
    "https://casual-effects.com/g3d/data10/research/model/San_Miguel/info.js"
local san_miguel_archive_size = 535519642
local san_miguel_archive_sha256 = "85874077735808150e679b3c71d70a37a270cb8833f4911325aa1099da3f7d4a"
local san_miguel_info_sha256 = "e0af84b294006670817994017793b4d3c91e502f39b7d73279f10ea96fb24506"
local san_miguel_sha256 = "db20256cfd5d7ba44761ccf7b9edfec10fc5f3cbc4974003b6d79ee0fb44e717"
local material_lab_environment_url =
    "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/studio_small_09_1k.hdr"
local material_lab_environment_sha256 =
    "e7cfda5f4e98e623db12b8bfd0184e048488e4855d9c83e2751fb44a32e80c45"

task("setup")
    -- An explicit (even empty) options table is required for -P to work with this task: a
    -- set_menu with no "options" key rejects -P as an unrecognized option outright.
    set_menu {usage = "xmake setup [--san-miguel]", description = "fetch pinned dependencies and scene assets",
              options = {{nil, "san-miguel", "k", nil, "also fetch the optional San Miguel realtime scene"}}}
    on_run(function ()
        import("core.base.option")
        -- Applies a maintained ThirdParty patch and leaves the tree holding exactly it, whatever
        -- state the checkout was already in: pristine, already carrying this patch, or carrying an
        -- older revision of it. That last state is the one that used to fail setup outright -- the
        -- reverse-check misses, and the forward check then fails on context the stale patch already
        -- moved -- so the tracked tree is restored before checking again. Doing so is safe because
        -- HEAD has just been asserted to be the pin, which leaves patch residue as the only thing
        -- discardable. Untracked files are never touched, only reported.
        local function apply_maintained_patch(dir, patch)
            local already_applied = false
            try {
                function ()
                    os.iorunv("git", {"-C", dir, "apply", "--reverse", "--check", patch})
                    already_applied = true
                end,
                catch {
                    function ()
                    end
                }
            }
            if not already_applied then
                os.execv("git", {"-C", dir, "checkout", "-q", "--", "."})
                os.execv("git", {"-C", dir, "apply", "--check", patch})
                os.execv("git", {"-C", dir, "apply", patch})
            end
            local applied = os.iorunv("git", {"-C", dir, "diff", "--no-ext-diff", "--binary"})
            assert(applied == io.readfile(patch),
                   format("%s has tracked changes beyond the maintained patch", dir))
            local untracked = os.iorunv("git", {"-C", dir, "ls-files", "--others",
                                                "--exclude-standard"}):trim()
            assert(untracked == "", format("%s contains untracked files", dir))
        end

        local geist_dir = "ThirdParty/Geist"
        os.mkdir(geist_dir)
        local geist_verified = true
        for _, entry in ipairs(geist_files) do
            local dest = path.join(geist_dir, entry.name)
            if os.isfile(dest) then
                local actual = os.iorunv("shasum", {"-a", "256", dest}):match("^(%x+)")
                assert(actual == entry.sha256, format("Geist %s checksum mismatch; expected %s",
                                                     entry.name, entry.sha256))
            else
                geist_verified = false
            end
        end
        if not geist_verified then
            local archive = path.join(geist_dir, "geist-font-v" .. geist_version .. ".zip.download")
            os.execv("curl", {"-fL", "--retry", "2", "--max-time", "300", "-o", archive, geist_url})
            local actual = os.iorunv("shasum", {"-a", "256", archive}):match("^(%x+)")
            assert(actual == geist_sha256, format("Geist archive checksum mismatch; expected %s",
                                                 geist_sha256))
            local extracted = path.join(geist_dir, "extracted")
            os.mkdir(extracted)
            os.execv("unzip", {"-q", "-o", archive, "-d", extracted})
            for _, entry in ipairs(geist_files) do
                local source = path.join(extracted, entry.source)
                local digest = os.iorunv("shasum", {"-a", "256", source}):match("^(%x+)")
                assert(digest == entry.sha256, format("Geist %s checksum mismatch; expected %s",
                                                     entry.name, entry.sha256))
                os.cp(source, path.join(geist_dir, entry.name))
            end
            os.rm(extracted)
            os.rm(archive)
        end
        local geist_source = "Geist " .. geist_version .. " (static Sans Regular/Medium and Mono Regular)\n" ..
                             geist_url .. "\nArchive SHA-256: " .. geist_sha256 ..
                             "\nLicense: SIL Open Font License 1.1; see OFL.txt\n"
        if not os.isfile(path.join(geist_dir, "SOURCE.txt")) or
           io.readfile(path.join(geist_dir, "SOURCE.txt")) ~= geist_source then
            io.writefile(path.join(geist_dir, "SOURCE.txt"), geist_source)
        end
        print("Geist " .. geist_version .. " fonts and license verified")

        local codicons_dir = "ThirdParty/Codicons"
        os.mkdir(codicons_dir)
        local codicons_verified = true
        for _, entry in ipairs(codicons_files) do
            local dest = path.join(codicons_dir, entry.name)
            if os.isfile(dest) then
                local actual = os.iorunv("shasum", {"-a", "256", dest}):match("^(%x+)")
                assert(actual == entry.sha256, format("Codicons %s checksum mismatch; expected %s",
                                                     entry.name, entry.sha256))
            else
                codicons_verified = false
            end
        end
        if not codicons_verified then
            local archive = path.join(codicons_dir, "codicons-" .. codicons_version .. ".tgz.download")
            os.execv("curl", {"-fL", "--retry", "2", "--max-time", "300", "-o", archive, codicons_url})
            local actual = os.iorunv("shasum", {"-a", "256", archive}):match("^(%x+)")
            assert(actual == codicons_sha256, format("Codicons archive checksum mismatch; expected %s",
                                                    codicons_sha256))
            local unpacked = path.join(codicons_dir, ".unpacked")
            os.tryrm(unpacked)
            os.mkdir(unpacked)
            os.execv("tar", {"-xzf", archive, "-C", unpacked,
                             "package/dist/codicon.ttf", "package/LICENSE"})
            for _, entry in ipairs(codicons_files) do
                local candidate = path.join(unpacked, entry.source)
                local actual = os.iorunv("shasum", {"-a", "256", candidate}):match("^(%x+)")
                assert(actual == entry.sha256, format("Codicons %s checksum mismatch; expected %s",
                                                     entry.name, entry.sha256))
            end
            for _, entry in ipairs(codicons_files) do
                os.cp(path.join(unpacked, entry.source), path.join(codicons_dir, entry.name))
            end
            os.rm(unpacked)
            os.rm(archive)
        end
        io.writefile(path.join(codicons_dir, "SOURCE.txt"),
                     "@vscode/codicons " .. codicons_version .. "\n" .. codicons_url ..
                     "\nLicense: CC BY 4.0; see LICENSE\n")

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
        local validator_dir = "ThirdParty/glTF-Validator"
        local validator_binary = path.join(validator_dir, "gltf_validator")
        if not os.isfile(validator_binary) then
            os.mkdir(validator_dir)
            local archive_name = format("gltf_validator-%s-macos64.tar.xz",
                                        gltf_validator_version)
            local archive = path.join(validator_dir, archive_name .. ".download")
            local url = format("https://github.com/KhronosGroup/glTF-Validator/releases/download/%s/%s",
                               gltf_validator_version, archive_name)
            os.execv("curl", {"-fL", "--retry", "2", "--max-time", "300", "-o", archive, url})
            local archive_hash = os.iorunv("shasum", {"-a", "256", archive}):match("^(%x+)")
            assert(archive_hash == gltf_validator_archive_sha256,
                   format("glTF Validator archive checksum mismatch; expected %s",
                          gltf_validator_archive_sha256))
            os.execv("tar", {"-xJf", archive, "-C", validator_dir})
            os.rm(archive)
            try {
                function ()
                    os.execv("xattr", {"-dr", "com.apple.quarantine", validator_dir})
                end
            }
        end
        local validator_hash = os.iorunv("shasum", {"-a", "256", validator_binary}):match("^(%x+)")
        assert(validator_hash == gltf_validator_binary_sha256,
               format("glTF Validator executable checksum mismatch; expected %s",
                      gltf_validator_binary_sha256))
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
                                      "RojoRHI/Tools/Patches/imgui-metal4-remove-texture.patch")
        assert(os.isfile(imgui_patch),
               "RojoRHI/ is empty: run `git submodule update --init`, then `xmake setup` again")
        apply_maintained_patch("ThirdParty/imgui", imgui_patch)

        if not os.isdir("ThirdParty/imgui-node-editor") then
            -- Fetch the exact untagged commit without cloning the rest of the repo's history.
            os.mkdir("ThirdParty/imgui-node-editor")
            os.execv("git", {"-C", "ThirdParty/imgui-node-editor", "init", "-q"})
            os.execv("git", {"-C", "ThirdParty/imgui-node-editor", "remote", "add", "origin",
                             "https://github.com/thedmd/imgui-node-editor.git"})
            os.execv("git", {"-C", "ThirdParty/imgui-node-editor", "fetch", "--depth", "1",
                             "origin", node_editor_pin})
            os.execv("git", {"-C", "ThirdParty/imgui-node-editor", "checkout", "-q", "FETCH_HEAD"})
        end
        local node_editor_head = os.iorunv("git", {"-C", "ThirdParty/imgui-node-editor",
                                                    "rev-parse", "HEAD"}):trim()
        assert(node_editor_head == node_editor_pin,
               format("ThirdParty/imgui-node-editor is at %s; expected pinned commit %s",
                      node_editor_head, node_editor_pin))

        local node_editor_patch = path.join(os.projectdir(),
                                            "Tools/Patches/imgui-node-editor-imgui-1.93.patch")
        apply_maintained_patch("ThirdParty/imgui-node-editor", node_editor_patch)

        if not os.isdir("ThirdParty/ImGuizmo") then
            os.mkdir("ThirdParty/ImGuizmo")
            os.execv("git", {"-C", "ThirdParty/ImGuizmo", "init", "-q"})
            os.execv("git", {"-C", "ThirdParty/ImGuizmo", "remote", "add", "origin",
                             "https://github.com/CedricGuillemet/ImGuizmo.git"})
            os.execv("git", {"-C", "ThirdParty/ImGuizmo", "fetch", "--depth", "1",
                             "origin", imguizmo_pin})
            os.execv("git", {"-C", "ThirdParty/ImGuizmo", "checkout", "-q", "FETCH_HEAD"})
        end
        local imguizmo_head = os.iorunv("git", {"-C", "ThirdParty/ImGuizmo",
                                                 "rev-parse", "HEAD"}):trim()
        assert(imguizmo_head == imguizmo_pin,
               format("ThirdParty/ImGuizmo is at %s; expected pinned commit %s",
                      imguizmo_head, imguizmo_pin))
        -- Every Khronos sample ships the same three files under Models/<name>: the binary glTF,
        -- its license and its provenance metadata. All three are fetched so the checkout carries
        -- the attribution each asset's license requires.
        local khronos_models = {"DamagedHelmet", "CesiumMilkTruck", "InterpolationTest"}
        for _, model in ipairs(khronos_models) do
            local directory = path.join("Assets/Fetched", model)
            os.mkdir(directory)
            local sources = {
                {name = model .. ".glb", source = "glTF-Binary/" .. model .. ".glb"},
                {name = "LICENSE.md", source = "LICENSE.md"},
                {name = "metadata.json", source = "metadata.json"}
            }
            for _, file in ipairs(sources) do
                local destination = path.join(directory, file.name)
                if not os.isfile(destination) then
                    -- Publish each fetched file only after curl has completed it successfully.
                    local part = destination .. ".part"
                    os.tryrm(part)
                    os.execv("curl", {"-L", "--fail", "--max-time", "600", "-o", part,
                        format("https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/%s/Models/%s/%s",
                               helmet_pin, model, file.source)})
                    os.mv(part, destination)
                end
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
        local khronos_hashes = {
            {model = "DamagedHelmet", label = "Damaged Helmet", asset = helmet_sha256,
             license = helmet_license_sha256, metadata = helmet_metadata_sha256},
            {model = "CesiumMilkTruck", label = "Cesium Milk Truck", asset = milk_truck_sha256,
             license = milk_truck_license_sha256, metadata = milk_truck_metadata_sha256},
            {model = "InterpolationTest", label = "InterpolationTest",
             asset = interpolation_test_sha256, license = interpolation_test_license_sha256,
             metadata = interpolation_test_metadata_sha256}
        }
        for _, entry in ipairs(khronos_hashes) do
            local directory = path.join("Assets/Fetched", entry.model)
            local files = {
                {name = entry.model .. ".glb", expected = entry.asset, what = ""},
                {name = "LICENSE.md", expected = entry.license, what = " license"},
                {name = "metadata.json", expected = entry.metadata, what = " metadata"}
            }
            for _, file in ipairs(files) do
                local hash = os.iorunv("shasum", {"-a", "256",
                    path.join(directory, file.name)}):match("^(%x+)")
                assert(hash == file.expected,
                       format("%s%s checksum mismatch; expected %s", entry.label, file.what,
                              file.expected))
            end
        end
        local sponza_hash = os.iorunv("python3", {"Tools/tree_digest.py",
                                                  "Assets/Fetched/Sponza"}):trim()
        assert(sponza_hash == sponza_sha256,
               format("Sponza tree checksum mismatch; expected %s", sponza_sha256))

        local has_san_miguel = option.get("san-miguel")
                              or os.isfile("Assets/Fetched/SanMiguel/SanMiguel.gltf")
        if has_san_miguel then
            os.execv("python3", {"Tools/setup_san_miguel.py", "--url", san_miguel_url,
                                  "--info-url", san_miguel_info_url,
                                  "--size", tostring(san_miguel_archive_size),
                                  "--archive-sha256", san_miguel_archive_sha256,
                                  "--info-sha256", san_miguel_info_sha256,
                                  "--tree-sha256", san_miguel_sha256})
        end

        -- Deterministic offline mip bake (Source/Engine/Asset/Texture/TextureBake.h): every
        -- base-color and normal image the two glTF/GLB files reference gets a sibling
        -- Baked/image<N>.dds that Engine/Scene/Scene.cpp's ensureUploaded prefers over the runtime
        -- fallback path. Re-running setup is cheap here too -- Tools/bake_gltf_textures.py skips
        -- any image whose manifest matches the current source hash, role-specific filter, and
        -- bake-tool version.
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
        os.execv("python3", {"Tools/bake_gltf_textures.py",
                             "Assets/Fetched/CesiumMilkTruck/CesiumMilkTruck.glb", texturebake_bin})
        if has_san_miguel then
            os.execv("python3", {"Tools/bake_gltf_textures.py",
                                 "Assets/Fetched/SanMiguel/SanMiguel.gltf", texturebake_bin})
        end

        print("setup done: metal-cpp %s, slang %s, imgui %s, imgui-node-editor %s, ImGuizmo %s, " ..
              "Sponza archive %s, Khronos samples %s, MaterialLab environment %s", metalcpp_pin,
              slang_pin, imgui_pin, node_editor_pin, imguizmo_pin, sponza_archive_sha256, helmet_pin,
              material_lab_environment_sha256)
    end)
