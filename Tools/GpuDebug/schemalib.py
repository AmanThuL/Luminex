"""Load and validate a Luminex capture-schema sidecar (<bundle>.gputrace.schema.json, version 1).

The sidecar is written by RHI/Source/CaptureSchema.cpp on every endCapture() call. This module
loads the versioned JSON into plain dataclasses and validates its required keys; geometry and
content cross-checks against the bundle live in bundlelib.join().

Run: python3 -c "import schemalib; print(schemalib.load_schema('bundle.gputrace.schema.json'))"
"""
import dataclasses
import json
import pathlib

SUPPORTED_VERSION = 1

# The sidecar is written next to the bundle by every App-driven capture (RHI/Source/
# CaptureSchema.cpp's writeJson, called from Metal4Capture's endCapture()). Every SchemaError
# below carries this alongside the offending path -- the usual cause of a schema failure is "the
# sidecar doesn't exist / is stale" rather than a bug in this module.
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
    slot: int
    size_bytes: int
    fields: list  # list[UniformField]


@dataclasses.dataclass
class UniformUpload:
    ring_label: str
    slot: int
    ring_offset: int
    size_bytes: int


@dataclasses.dataclass
class Resource:
    label: str
    kind: str  # "texture2d" | "cube" | "buffer"
    # Texture-only fields; None for kind == "buffer" (the sidecar omits these keys entirely --
    # CaptureSchema.cpp's renderJson() branches on kind the same way, see the reciprocal branch
    # there for why).
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
    uniform_uploads: list  # list[UniformUpload]


def _require(obj: dict, key: str, where: str):
    if key not in obj:
        raise _error(f"{where}: missing required key {key!r}")
    return obj[key]


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
            size_bytes=_require(raw, "sizeBytes", where),
        )
    return Resource(
        label=label,
        kind=kind,
        format=_require(raw, "format", where),
        width=_require(raw, "width", where),
        height=_require(raw, "height", where),
        mip_levels=_require(raw, "mipLevels", where),
        size_bytes=None,
    )


def _load_uniform_struct(raw: dict, where: str) -> UniformStruct:
    fields = [
        UniformField(
            name=_require(f, "name", f"{where} fields[{i}]"),
            offset_bytes=_require(f, "offsetBytes", f"{where} fields[{i}]"),
            type=_require(f, "type", f"{where} fields[{i}]"),
        )
        for i, f in enumerate(raw.get("fields", []))
    ]
    return UniformStruct(
        name=_require(raw, "name", where),
        slot=_require(raw, "slot", where),
        size_bytes=_require(raw, "sizeBytes", where),
        fields=fields,
    )


def _load_uniform_upload(raw: dict, where: str) -> UniformUpload:
    return UniformUpload(
        ring_label=_require(raw, "ringLabel", where),
        slot=_require(raw, "slot", where),
        ring_offset=_require(raw, "ringOffset", where),
        size_bytes=_require(raw, "sizeBytes", where),
    )


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
    if version != SUPPORTED_VERSION:
        raise _error(
            f"{path}: unsupported schema version {version!r}, expected {SUPPORTED_VERSION}"
        )

    # `context` is passed through as a plain dict rather than a dataclass: its non-finite floats
    # round-trip through the sidecar as JSON null (CaptureSchema.cpp's appendFloat), which
    # `json.loads` already turns these values into None. Consumers must tolerate None wherever a
    # non-finite context float can appear.
    context = _require(raw, "context", str(path))

    resources = [
        _load_resource(r, f"{path} resources[{i}]")
        for i, r in enumerate(_require(raw, "resources", str(path)))
    ]
    uniform_structs = [
        _load_uniform_struct(s, f"{path} uniformStructs[{i}]")
        for i, s in enumerate(_require(raw, "uniformStructs", str(path)))
    ]
    uniform_uploads = [
        _load_uniform_upload(u, f"{path} uniformUploads[{i}]")
        for i, u in enumerate(_require(raw, "uniformUploads", str(path)))
    ]

    return Schema(
        context=context,
        resources=resources,
        uniform_structs=uniform_structs,
        uniform_uploads=uniform_uploads,
    )
