"""Load and validate a Luminex capture-schema sidecar (<bundle>.gputrace.schema.json, version 2).

The sidecar is written by Source/Engine/Gpu/CaptureSchema.cpp when a capture ends. This module
loads the versioned JSON into plain dataclasses and validates its required keys; geometry and
content cross-checks against the bundle live in bundlelib.join(), and root decoding in uniformlib.

Version 2 names the frame's data by root: `frameRoots` lists each registered root's name and GPU
address (`Addr` lo/hi), and `uniformStructs` gives each named layout. Version 1 sidecars, whose
`frameDataUploads` named arena pages and binding slots, came from builds before RojoRHI v2 and are
rejected with a message saying so.

Run: python3 -c "import schemalib; print(schemalib.load_schema('bundle.gputrace.schema.json'))"
"""
import dataclasses
import json
import pathlib

SUPPORTED_VERSION = 2

# The sidecar is written next to the bundle by every App-driven capture (GpuContext::endCapture
# calls writeCaptureSchema). Every SchemaError below carries this alongside the offending path --
# the usual cause of a schema failure is "the sidecar doesn't exist / is stale" rather than a bug
# in this module.
RUN_COMMAND_HINT = (
    "MTL_CAPTURE_ENABLED=1 LMX_MAX_FRAMES=<N> LMX_CAPTURE_AT_FRAME=<M> "
    "LMX_CAPTURE_PATH=<out>.gputrace ./App   (run from the App's build dir; the schema sidecar "
    "lands at <out>.gputrace.schema.json next to the bundle)"
)


class SchemaError(Exception):
    """Raised when the sidecar JSON is missing a required key or its version is unsupported.

    Every message names the offending key or the actual-vs-expected version, includes the
    sidecar path, and includes RUN_COMMAND_HINT -- the run command that produces a fresh sidecar
    -- so a failure is actionable without re-reading this module's source.
    """


def _error(message: str) -> SchemaError:
    return SchemaError(f"{message}\nProduce one with: {RUN_COMMAND_HINT}")


@dataclasses.dataclass
class UniformField:
    name: str
    offset_bytes: int
    type: str


@dataclasses.dataclass
class UniformStruct:
    name: str
    size_bytes: int
    fields: list  # list[UniformField]


@dataclasses.dataclass
class FrameRoot:
    name: str  # the registered root name, which names its layout in uniform_structs
    address: int  # the GPU virtual address, (hi << 32) | lo


@dataclasses.dataclass
class Resource:
    label: str
    kind: str  # "texture2d" | "cube" | "buffer" | ...
    # Texture-only fields; None for kind == "buffer" (the sidecar omits these keys entirely --
    # CaptureSchema.cpp's renderSchema() branches on kind the same way).
    format: str | None
    width: int | None
    height: int | None
    mip_levels: int | None
    # Buffer-only field; None for texture/cube resources.
    size_bytes: int | None


@dataclasses.dataclass
class Schema:
    context: dict
    resources: list  # list[Resource]
    uniform_structs: list  # list[UniformStruct]
    frame_roots: list  # list[FrameRoot], in registration order


def _require(obj: dict, key: str, where: str):
    if key not in obj:
        raise _error(f"{where}: missing required key {key!r}")
    return obj[key]


def _require_int(obj: dict, key: str, where: str, limit: int | None = None) -> int:
    """A required non-negative integer (a JSON bool is not one), below `limit` when given."""
    value = _require(obj, key, where)
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise _error(f"{where}: {key!r} must be a non-negative integer, got {value!r}")
    if limit is not None and value >= limit:
        raise _error(f"{where}: {key!r} must be below {limit:#x}, got {value!r}")
    return value


def _load_resource(raw: dict, where: str) -> Resource:
    kind = _require(raw, "kind", where)
    label = _require(raw, "label", where)
    if kind == "buffer":
        return Resource(
            label=label,
            kind=kind,
            format=None,
            width=None,
            height=None,
            mip_levels=None,
            size_bytes=_require_int(raw, "sizeBytes", where),
        )
    return Resource(
        label=label,
        kind=kind,
        format=_require(raw, "format", where),
        width=_require_int(raw, "width", where),
        height=_require_int(raw, "height", where),
        mip_levels=_require_int(raw, "mipLevels", where),
        size_bytes=None,
    )


def _load_uniform_struct(raw: dict, where: str) -> UniformStruct:
    fields = [
        UniformField(
            name=_require(f, "name", f"{where} fields[{i}]"),
            offset_bytes=_require_int(f, "offsetBytes", f"{where} fields[{i}]"),
            type=_require(f, "type", f"{where} fields[{i}]"),
        )
        for i, f in enumerate(raw.get("fields", []))
    ]
    return UniformStruct(
        name=_require(raw, "name", where),
        size_bytes=_require_int(raw, "sizeBytes", where),
        fields=fields,
    )


def _load_frame_root(raw: dict, where: str) -> FrameRoot:
    addr = _require(raw, "addr", where)
    if not isinstance(addr, dict):
        raise _error(f"{where}: 'addr' must be an object with 'lo' and 'hi'")
    lo = _require_int(addr, "lo", f"{where} addr", limit=1 << 32)
    hi = _require_int(addr, "hi", f"{where} addr", limit=1 << 32)
    return FrameRoot(name=_require(raw, "name", where), address=(hi << 32) | lo)


def load_schema(path) -> Schema:
    """Read and validate a schema sidecar. Raises SchemaError on any structural problem."""
    path = pathlib.Path(path)
    try:
        text = path.read_text()
    except OSError as exc:
        raise _error(f"cannot read schema {path}: {exc}") from exc
    try:
        raw = json.loads(text)
    except json.JSONDecodeError as exc:
        raise _error(f"{path}: invalid JSON: {exc}") from exc
    if not isinstance(raw, dict):
        raise _error(f"{path}: top-level JSON value must be an object")

    version = _require(raw, "version", str(path))
    if version == 1:
        raise _error(
            f"{path}: schema version 1 comes from a build before RojoRHI v2 (frame data by arena "
            f"page and slot); this tool reads version {SUPPORTED_VERSION} only (frame data by root "
            "name and address). Re-capture with the current App")
    if version != SUPPORTED_VERSION:
        raise _error(
            f"{path}: unsupported schema version {version!r}, expected {SUPPORTED_VERSION}"
        )

    # `context` is passed through as a plain dict rather than a dataclass: its non-finite floats
    # round-trip through the sidecar as JSON null (CaptureSchema.cpp's appendFloat), which
    # `json.loads` already turns into None. Consumers must tolerate None wherever a non-finite
    # context float can appear.
    context = _require(raw, "context", str(path))

    resources = [
        _load_resource(r, f"{path} resources[{i}]")
        for i, r in enumerate(_require(raw, "resources", str(path)))
    ]
    uniform_structs = [
        _load_uniform_struct(s, f"{path} uniformStructs[{i}]")
        for i, s in enumerate(_require(raw, "uniformStructs", str(path)))
    ]
    frame_roots = [
        _load_frame_root(r, f"{path} frameRoots[{i}]")
        for i, r in enumerate(_require(raw, "frameRoots", str(path)))
    ]

    return Schema(
        context=context,
        resources=resources,
        uniform_structs=uniform_structs,
        frame_roots=frame_roots,
    )
