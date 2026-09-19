-- Slang -> readable MSL -> .metallib (ADR 0003). Emits both artifacts into
-- <targetdir>/Shaders/; a target opts in with add_rules("slang2metallib") plus
-- add_files("Shaders/**.slang"). App and Tests both load the artifacts at runtime, so both
-- opt in -- which is why they must not share a target directory: two targets emitting the
-- same paths race under a parallel build. See Tests/xmake.lua for its separate target directory.
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

        -- Runtime paths are flat even though sources are nested. Also reject case aliases on
        -- the default macOS filesystem before any command can overwrite another artifact.
        local names = {}
        for _, shader in ipairs(os.files(path.join(os.projectdir(), "Shaders/**.slang"))) do
            local basename = path.basename(shader):lower()
            if names[basename] then
                os.raise("slang2metallib: '%s' and '%s' share output basename '%s'",
                         names[basename], shader, basename)
            end
            names[basename] = shader
        end

        local outdir = path.join(target:targetdir(), "Shaders")
        local name = path.basename(sourcefile)
        local msl = path.join(outdir, name .. ".metal")
        local slangc = path.join(os.projectdir(), "ThirdParty/slang/bin/slangc")
        batchcmds:mkdir(outdir)
        batchcmds:show_progress(opt.progress, "${color.build.object}slang %s", sourcefile)
        local slangargs = {sourcefile, "-I", path.join(os.projectdir(), "Shaders/Modules"),
                           "-target", "metal", "-o", msl}
        local visibility = name:startswith("Visibility") or (name:startswith("Occlusion") and name ~= "OcclusionReference") or name:startswith("Hzb") or name:startswith("LightCluster")
        if visibility then table.join2(slangargs, {"-fp-mode", "precise"}) end
        batchcmds:vrunv(slangc, slangargs)
        -- Offline metallib precompile is optional because Command Line Tools installations may
        -- not include the Metal toolchain; the runtime can compile the emitted MSL instead.
        local has_metal = try {function ()
            os.runv("xcrun", {"-sdk", "macosx", "metal", "--version"}); return true
        end}
        if has_metal then
            local lib = path.join(outdir, name .. ".metallib")
            local metalargs = {"-sdk", "macosx", "metal", "-std=metal4.0",
                               "-frecord-sources", "-gline-tables-only", "-o", lib, msl}
            if visibility then table.join2(metalargs, {"-fno-fast-math", "-ffp-contract=off"}) end
            batchcmds:vrunv("xcrun", metalargs)
        end
        -- Every .slang source, not just this one, and deliberately so: `import Shadow;` makes
        -- ShadowSmoke.slang depend on Shadow.slang, and nothing here can see that edge --
        -- slangc's CLI has no depfile mode wired up, and parsing `import` lines out of the
        -- source would be a second, silently-drifting implementation of Slang's module
        -- resolution. Without this, editing a module leaves every importer's .metal/.metallib
        -- stale. The recursive conservative glob rebuilds all shaders whenever any
        -- entry or module changes. It includes every nested module and test oracle, avoiding
        -- stale metallibs without duplicating the compiler's dependency resolver.
        batchcmds:add_depfiles(os.files(path.join(os.projectdir(), "Shaders/**.slang")))
        batchcmds:set_depmtime(os.mtime(msl))
        batchcmds:set_depcache(target:dependfile(msl))
    end)
