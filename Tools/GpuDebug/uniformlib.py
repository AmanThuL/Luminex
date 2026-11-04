"""Decode the frame's named roots (`Schema.frame_roots`) from the buffer bytes a `.gputrace` holds.

A root is a (name, GPU address) pair the renderer registered while the capture was open; the
name also names its layout in `Schema.uniform_structs`. The bundle records every buffer's GPU
address, length and label (bundlelib.scan_buffers), so:

1. A root resolves to the one captured buffer whose `[address, address + length)` holds its
   address -- in practice a RojoRHI frame-arena page, which the sidecar does not list because the
   arena is internal to RojoRHI.
2. Its struct decodes at `root address - buffer address` in that buffer's blob, field by field,
   from the layout registered under the root's name. The struct must lie wholly inside the blob.
3. Each `Addr` field is followed one level and shown without the raw address, which differs run
   to run: `{"root": name}` when it equals a registered root's address, else `{"buffer": label,
   "offset": n}` for the labelled buffer it points into, else `{"unresolved": "0x..."}`. A zero
   address stays `0`, not `null`: null is reserved for non-finite floats, which the anomaly checks
   look for, and a zero address is an ordinary unused field.

The bytes are what the capture saw in GPU memory, not a CPU copy, so a root overwritten before the
GPU read it shows the overwrite. A root that cannot be decoded (no buffer holds its address, the
buffer has no captured contents, an unknown field type) is reported with its reason; the other
roots still decode. Non-finite floats render as JSON `null`, as `CaptureSchema.cpp`'s
`appendFloat` does for the context; decode them, flag them, never fabricate a number.

Run: python3 -c "import uniformlib"  (see tests/test_uniformlib.py for worked examples)
"""
import dataclasses
import math
import re
import struct

import bundlelib

# First N bytes shown for a root whose name has no registered layout.
_RAW_HEX_BYTES = 64

_ARRAY_RE = re.compile(r"^(.+)\[(\d+)\]$")

# Scalar element format, component count and byte size per field type. Arrays (`T[n]`) use the
# C++ layout the registering code takes its offsets from: elements packed at sizeof(T).
_TYPES = {
    "float": ("f", 1, 4),
    "float2": ("f", 2, 8),
    "float3": ("f", 3, 12),
    "float4": ("f", 4, 16),
    "float4x4": ("f", 16, 64),
    "int": ("i", 1, 4),
    "int2": ("i", 2, 8),
    "int3": ("i", 3, 12),
    "int4": ("i", 4, 16),
    "uint": ("I", 1, 4),
    "uint2": ("I", 2, 8),
    "uint3": ("I", 3, 12),
    "uint4": ("I", 4, 16),
    "Addr": ("Q", 1, 8),
}


# The reason every root gets when the bundle yields no buffer address at all: a capture always has
# buffers, so this means bundlelib.scan_buffers did not recognise the trace's records.
NO_BUFFER_RECORDS = ("the trace's buffer address records were not recognised (no buffer addresses "
                     "found in the bundle; the .gputrace record format may have changed)")


class UniformError(Exception):
    """Raised for a field whose type this decoder does not know; names the field and type."""


@dataclasses.dataclass
class DecodedRoot:
    # Deliberately no raw address: it varies run to run even for two captures with byte-identical
    # content, and this record feeds `uniforms.json`, which is meant to be diffable across runs.
    # The buffer label and offset name the range instead.
    index: int  # position in the sidecar's frameRoots, i.e. registration order
    name: str
    buffer_label: str | None
    buffer_offset: int
    size_bytes: int | None  # None when the name has no registered layout (values hold rawHex)
    values: dict
    finite: bool  # False if any decoded float in `values` was NaN/Inf (rendered as None/null)


@dataclasses.dataclass
class UndecodedRoot:
    index: int
    name: str
    reason: str


@dataclasses.dataclass
class RootDecode:
    decoded: list  # list[DecodedRoot]
    undecoded: list  # list[UndecodedRoot]
    attribution: str  # one-line summary; written verbatim as uniforms.json's "rootAttribution"


def _sanitize_float(value: float):
    """(json_safe_value, finite) -- non-finite floats become None so json.dumps writes `null`
    instead of the non-standard `NaN`/`Infinity` tokens Python would otherwise emit."""
    if math.isfinite(value):
        return value, True
    return None, False


def _type_size(type_name: str) -> int:
    match = _ARRAY_RE.match(type_name)
    base, count = (match.group(1), int(match.group(2))) if match else (type_name, 1)
    if base not in _TYPES:
        raise UniformError(f"unrecognized field type {type_name!r}")
    return _TYPES[base][2] * count


class _AddrNamer:
    """Turns an `Addr` value into its diffable description (module docstring, step 3)."""

    def __init__(self, roots: list, buffers: list):
        self._roots = {}
        for root in roots:
            self._roots.setdefault(root.address, root.name)
        self._buffers = buffers

    def __call__(self, address: int):
        if address == 0:
            return 0
        if address in self._roots:
            return {"root": self._roots[address]}
        buffer = bundlelib.buffer_containing(self._buffers, address)
        if buffer is not None and buffer.label is not None:
            return {"buffer": buffer.label, "offset": address - buffer.gpu_address}
        return {"unresolved": f"{address:#x}"}


def _decode_element(base: str, data: bytes, offset: int, name_addr):
    """(value, finite) for one element of scalar/vector/matrix type `base` at `offset`.

    float4x4 is 16 floats in glm/Slang column-major storage order (mem[col*4+row] == M[row][col])
    reshaped into a row-major nested list for display (display[row][col] == M[row][col]: the SAME
    matrix, not a mathematical transpose of it). int/uint have no finiteness concept.
    """
    fmt, count, _ = _TYPES[base]
    raw = struct.unpack_from(f"<{count}{fmt}", data, offset)
    if base == "Addr":
        return name_addr(raw[0]), True
    if fmt != "f":
        return (raw[0] if count == 1 else list(raw)), True
    values, finite = [], True
    for component in raw:
        value, ok = _sanitize_float(component)
        values.append(value)
        finite = finite and ok
    if base == "float4x4":
        columns = [values[c * 4:(c + 1) * 4] for c in range(4)]
        return [[columns[c][r] for c in range(4)] for r in range(4)], finite
    return (values[0] if count == 1 else values), finite


def _decode_field(field, data: bytes, base_offset: int, name_addr):
    """(value, finite) for one schemalib.UniformField at `base_offset + field.offset_bytes`."""
    offset = base_offset + field.offset_bytes
    match = _ARRAY_RE.match(field.type)
    base, count = (match.group(1), int(match.group(2))) if match else (field.type, None)
    if base not in _TYPES:
        raise UniformError(f"field {field.name!r}: unrecognized field type {field.type!r}")
    if count is None:
        return _decode_element(base, data, offset, name_addr)
    stride = _TYPES[base][2]
    values, finite = [], True
    for element in range(count):
        value, ok = _decode_element(base, data, offset + element * stride, name_addr)
        values.append(value)
        finite = finite and ok
    return values, finite


def _struct_extent(struct_) -> int:
    """Bytes the struct occupies: its registered size, or its last field's end if larger."""
    end = struct_.size_bytes
    for field in struct_.fields:
        end = max(end, field.offset_bytes + _type_size(field.type))
    return end


def decode_roots(schema, buffers: list) -> RootDecode:
    """Decode every `schema.frame_roots` entry from `buffers` (bundlelib.scan_buffers output).

    Never raises for one root's problem: a root that cannot be decoded lands in `undecoded` with
    its reason, so one stale layout or missing blob does not hide the rest of the frame.
    """
    structs = {s.name: s for s in schema.uniform_structs}
    name_addr = _AddrNamer(schema.frame_roots, buffers)
    blob_bytes: dict = {}
    decoded, undecoded = [], []

    for index, root in enumerate(schema.frame_roots):
        def skip(reason: str, index=index, root=root):
            undecoded.append(UndecodedRoot(index=index, name=root.name, reason=reason))

        if root.address == 0:
            skip("registered with a zero address")
            continue
        if not buffers:
            skip(NO_BUFFER_RECORDS)
            continue
        buffer = bundlelib.buffer_containing(buffers, root.address)
        if buffer is None:
            skip(f"no single captured buffer holds address {root.address:#x}")
            continue
        if buffer.blob is None:
            skip(f"buffer {buffer.label!r} holds it, but the capture dumped no contents for it")
            continue
        data = blob_bytes.get(buffer.blob.path)
        if data is None:
            data = blob_bytes[buffer.blob.path] = buffer.blob.path.read_bytes()
        offset = root.address - buffer.gpu_address
        struct_ = structs.get(root.name)

        if struct_ is None:
            raw = data[offset:offset + _RAW_HEX_BYTES]
            decoded.append(DecodedRoot(
                index=index, name=root.name, buffer_label=buffer.label, buffer_offset=offset,
                size_bytes=None, values={"rawHex": raw.hex()}, finite=True))
            continue
        try:
            extent = _struct_extent(struct_)
            if offset + extent > len(data):
                skip(f"its {extent}-byte layout at offset {offset} runs past the "
                     f"{len(data)}-byte contents of {buffer.label!r}")
                continue
            values, finite = {}, True
            for field in struct_.fields:
                value, ok = _decode_field(field, data, offset, name_addr)
                values[field.name] = value
                finite = finite and ok
        except UniformError as exc:
            skip(str(exc))
            continue
        except struct.error as exc:  # a field read past the bytes despite the extent check
            skip(f"field read failed: {exc}")
            continue
        decoded.append(DecodedRoot(
            index=index, name=root.name, buffer_label=buffer.label, buffer_offset=offset,
            size_bytes=struct_.size_bytes, values=values, finite=finite))

    total = len(schema.frame_roots)
    if total == 0:
        attribution = "no frame roots recorded in this capture"
    else:
        attribution = f"{len(decoded)} of {total} frame roots decoded from captured buffers"
        if undecoded:
            first = undecoded[0]
            attribution += f"; first undecoded: root {first.index} {first.name}: {first.reason}"
    return RootDecode(decoded=decoded, undecoded=undecoded, attribution=attribution)
