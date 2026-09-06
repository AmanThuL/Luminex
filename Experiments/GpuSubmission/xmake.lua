local experiment_root = path.join(os.projectdir(), "Experiments/GpuSubmission")

rule("submission_shaders")
    set_extensions(".slang")
    on_buildcmd_file(function (target, batchcmds, sourcefile, opt)
        local outdir = path.join(target:targetdir(), "Shaders")
        local msl = path.join(outdir, path.basename(sourcefile) .. ".metal")
        batchcmds:mkdir(outdir)
        batchcmds:show_progress(opt.progress, "${color.build.object}slang %s", sourcefile)
        batchcmds:vrunv(path.join(os.projectdir(), "ThirdParty/slang/bin/slangc"),
                       {sourcefile, "-target", "metal", "-o", msl})
        local has_metal = try {function ()
            os.runv("xcrun", {"-sdk", "macosx", "metal", "--version"}); return true
        end}
        if has_metal then
            batchcmds:vrunv("xcrun", {"-sdk", "macosx", "metal", "-std=metal4.0",
                "-frecord-sources", "-gline-tables-only", "-o",
                path.join(outdir, path.basename(sourcefile) .. ".metallib"), msl})
        end
        batchcmds:add_depfiles(os.files(path.join(os.projectdir(),
            "Experiments/GpuSubmission/Shaders/*.slang")))
        batchcmds:set_depmtime(os.mtime(msl))
        batchcmds:set_depcache(target:dependfile(msl))
    end)

target("GpuSubmissionModel")
    set_kind("static")
    set_default(false)
    add_files(path.join(experiment_root, "Model/*.cpp"))
    add_includedirs(experiment_root, {public = true})
    add_packages("glm", {public = true})

target("GpuSubmissionMetal")
    set_kind("static")
    set_default(false)
    add_files(path.join(experiment_root, "Metal/Host.cpp"))
    add_deps("Core", "GpuSubmissionModel")
    add_includedirs(path.join(os.projectdir(), "ThirdParty/metal-cpp"))
    add_frameworks("Metal", "Foundation", "QuartzCore")

target("GpuSubmissionReference")
    set_kind("static")
    set_default(false)
    add_files(path.join(experiment_root, "Reference/*.cpp"))
    add_deps("GpuSubmissionModel", "RHI", "Render")

target("GpuSubmissionBench")
    set_kind("binary")
    set_default(false)
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/gpu-submission")
    add_files(path.join(experiment_root, "Bench/*.cpp"))
    add_deps("GpuSubmissionModel", "GpuSubmissionMetal", "GpuSubmissionReference")
    add_rules("submission_shaders")
    add_files(path.join(experiment_root, "Shaders/Scene.slang"),
              path.join(experiment_root, "Shaders/Prepare.slang"))

target("GpuSubmissionTests")
    set_kind("binary")
    set_default(false)
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/gpu-submission-tests")
    add_files(path.join(experiment_root, "Tests/*.cpp"))
    add_deps("GpuSubmissionModel", "GpuSubmissionMetal", "GpuSubmissionReference")
    add_packages("catch2")
    add_rules("submission_shaders")
    add_files(path.join(experiment_root, "Shaders/Scene.slang"),
              path.join(experiment_root, "Shaders/Prepare.slang"))
    on_load(function (target)
        target:add("defines", 'LMX_SUBMISSION_SHADER_DIR="' ..
            path.absolute(path.join(target:targetdir(), "Shaders")) .. '"')
    end)
    add_tests("unit", {runargs = {"~[gpu]"}})
    add_tests("gpu", {runargs = {"[gpu]"}})
