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

function main()
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
        }
    end
    print(json.encode(out))
end
