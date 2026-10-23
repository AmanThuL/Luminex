-- Prints one JSON object describing every xmake target: kind, source files, dependencies,
-- packages, frameworks and target file, all repository-relative. Single-valued xmake fields
-- arrive as bare strings and are normalised to lists.
import("core.project.config")
import("core.project.project")
import("core.base.json")

function _list(value)
    if value == nil then
        return {}
    elseif type(value) == "table" then
        return value
    end
    return {value}
end

function _pending(target)
    local version = target:values("lmx.port.pending_version")
    local sha256 = target:values("lmx.port.pending_sha256")
    local cpp = {}
    for _, file in ipairs(_list(target:values("lmx.port.pending_cpp"))) do
        table.insert(cpp, file)
    end
    if version ~= nil or sha256 ~= nil or #cpp > 0 then
        json.mark_as_array(cpp)
        return {version = version, portSha256 = sha256, cpp = cpp}
    end
end

function main()
    -- Loads the configured build mode so target:targetfile() below includes it (e.g.
    -- build/macosx/arm64/release/App); without this call every targetfile omits the mode
    -- segment and points at a path nothing ever builds.
    config.load()
    local out = {}
    for name, target in pairs(project.targets()) do
        local files = {}
        for _, file in ipairs(target:sourcefiles()) do
            table.insert(files, path.relative(path.absolute(file), os.projectdir()))
        end
        table.sort(files)
        out[name] = {
            kind = target:kind(),
            files = files,
            deps = _list(target:get("deps")),
            packages = _list(target:get("packages")),
            frameworks = _list(target:get("frameworks")),
            targetfile = path.relative(target:targetfile(), os.projectdir()),
            pending = _pending(target),
        }
    end
    print(json.encode(out))
end
