"""Decode schema-declared uniform uploads (`Schema.uniform_uploads`/`uniform_structs`)
against the ring-buffer bytes captured in a `.gputrace` bundle, plus the ring-blob attribution
policy those bytes need in the first place.

Buffer attribution follows constraints measured from real capture bundles:

- **Buffer blobs are not label-joinable.** The buffer-contents record's
  receiver is the *device*, not the buffer, so `bundlelib.join()` never attaches a label to a
  `MTLBuffer-*` blob -- it can only match a buffer resource by exact, unambiguous size, and
  correctly refuses to guess among same-size candidates (three uniform rings, each exactly
  `kUniformRingBytes` = 262144 bytes, are the textbook ambiguous case). `resolve_ring_bytes` below
  is the positional fallback `join()` deliberately does not attempt:
  1. Every upload in one capture names one ring (`ringLabel`); look up that ring's `sizeBytes`
     from the matching schema resource.
  2. Candidates = every root-level `MTLBuffer-*` blob whose FILE SIZE equals that `sizeBytes`
     exactly (buffer blobs carry no header, so file size is buffer length).
  3. Exactly one candidate -> use it; Metal typically only dumps the frame's referenced ring.
  4. Exactly as many candidates as sibling ring resources (three, in practice) -> sort blobs by
     the numeric `<id>` in `MTLBuffer-<id>-<n>` ascending and pick positionally. Captures show
     the three rings created consecutively, so
     ascending id order lines up with ascending ring index. This is explicitly an *untrusted*
     heuristic -- ordering, not identity -- and is labelled as such in the returned attribution.
  5. Any other candidate count -> decline to decode; the caller gets an explanatory note instead
     of a guess.
- **Non-finite floats render as JSON `null`**, matching how `CaptureSchema.cpp`'s `appendFloat`
  already serializes a non-finite context float into the schema sidecar (schemalib.py's
  `test_context_tolerates_null_floats`). A ring can legitimately hold uninitialized/NaN bytes
  outside the region a frame actually wrote. Decode it, flag it, and never fabricate a number.

Run: python3 -c "import uniformlib"  (see tests/test_uniformlib.py for worked examples)
"""
import dataclasses
import math
import re
import struct

RUN_COMMAND_HINT = (
    "MTL_CAPTURE_ENABLED=1 LMX_MAX_FRAMES=<N> LMX_CAPTURE_AT_FRAME=<M> "
    "LMX_CAPTURE_PATH=<out>.gputrace ./App   (run from the App's build dir)"
)

# First N bytes shown for an upload whose (slot, sizeBytes) matches no registered struct.
_RAW_HEX_BYTES = 64

_BUFFER_ID_RE = re.compile(r"^MTLBuffer-(\d+)-\d+$")
# A ring label's trailing ".<index>" (e.g. "lmx.device.uniformRing.1" -> prefix
# "lmx.device.uniformRing", index 1) -- how sibling rings in the same family are recognized.
_LABEL_INDEX_RE = re.compile(r"^(.*)\.(\d+)$")


class UniformError(Exception):
    """Raised when a schema-declared upload's byte range doesn't fit inside the ring bytes
    actually available for its `ringLabel`. Names the ring label plus the declared offset/size
    and the available length, so a caller can tell a stale schema from a mis-attributed ring
    blob (see the module docstring's `resolve_ring_bytes` policy) at a glance.
    """


@dataclasses.dataclass
class DecodedUpload:
    index: int
    struct_name: str
    slot: int
    ring_label: str
    ring_offset: int
    values: dict
    finite: bool  # False if any decoded float in `values` was NaN/Inf (rendered as None/null)


@dataclasses.dataclass
class RingResolution:
    ring_bytes_by_label: dict  # ring label -> bytes, only for rings the policy could attribute
    attribution: str  # human-readable note; written verbatim as uniforms.json's "ringAttribution"


def _sanitize_float(value: float):
    """(json_safe_value, finite) -- non-finite floats become None so json.dumps writes `null`
    instead of the non-standard `NaN`/`Infinity` tokens Python would otherwise emit."""
    if math.isfinite(value):
        return value, True
    return None, False


def _decode_field(field, ring_bytes: bytes, base_offset: int):
    """(value, finite) for one schemalib.UniformField, read at `base_offset + field.offset_bytes`.

    Types mirror Renderer.cpp's registerUniformLayoutsForCapture(): float/float3/float4 are plain
    struct.unpack_from reads, float4x4 is 16 floats in glm/Slang column-major storage order
    (mem[col*4+row] == M[row][col]) transposed into a row-major nested list for display
    (display[row][col] == M[row][col], i.e. the SAME matrix, just re-shaped for readability --
    not a mathematical transpose of the matrix itself), and int/uint are plain 4-byte reads with
    no finiteness concept.
    """
    offset = base_offset + field.offset_bytes
    if field.type == "float":
        raw = struct.unpack_from("<f", ring_bytes, offset)[0]
        return _sanitize_float(raw)
    if field.type == "float3":
        raw = struct.unpack_from("<3f", ring_bytes, offset)
        values, finite = [], True
        for component in raw:
            v, ok = _sanitize_float(component)
            values.append(v)
            finite = finite and ok
        return values, finite
    if field.type == "float4":
        raw = struct.unpack_from("<4f", ring_bytes, offset)
        values, finite = [], True
        for component in raw:
            v, ok = _sanitize_float(component)
            values.append(v)
            finite = finite and ok
        return values, finite
    if field.type == "float4x4":
        raw = struct.unpack_from("<16f", ring_bytes, offset)
        flat, finite = [], True
        for component in raw:
            v, ok = _sanitize_float(component)
            flat.append(v)
            finite = finite and ok
        columns = [flat[c * 4:(c + 1) * 4] for c in range(4)]
        rows = [[columns[c][r] for c in range(4)] for r in range(4)]
        return rows, finite
    if field.type == "int":
        return struct.unpack_from("<i", ring_bytes, offset)[0], True
    if field.type == "uint":
        return struct.unpack_from("<I", ring_bytes, offset)[0], True
    raise UniformError(
        f"field {field.name!r}: unrecognized uniform field type {field.type!r}"
    )


def _decode_struct_fields(struct_, ring_bytes: bytes, ring_offset: int):
    values, finite = {}, True
    for field in struct_.fields:
        value, ok = _decode_field(field, ring_bytes, ring_offset)
        values[field.name] = value
        finite = finite and ok
    return values, finite


def decode_uploads(schema, ring_bytes_by_label: dict) -> list:
    """Decode every `schema.uniform_uploads` entry against `ring_bytes_by_label`.

    `ring_bytes_by_label` maps a ring label to the bytes attributed to it (see
    `resolve_ring_bytes`) -- an upload whose ring has no entry is treated as an empty ring (bounds
    check below fails immediately, raising UniformError) rather than silently skipped, so a caller
    that forgets to resolve a ring finds out from the exception rather than a quietly-empty
    manifest.

    Struct resolution is by exact (slot, sizeBytes) match against `schema.uniform_structs` -- the
    plan Reference states that pair is unique across the four registered structs. No match decodes
    as `struct_name="unknown"` with the upload's first 64 raw bytes as hex, so the sidecar is
    always legible even for a struct this schema version doesn't recognize.
    """
    struct_by_key = {(s.slot, s.size_bytes): s for s in schema.uniform_structs}

    decoded = []
    for index, upload in enumerate(schema.uniform_uploads):
        ring_bytes = ring_bytes_by_label.get(upload.ring_label, b"")
        end = upload.ring_offset + upload.size_bytes
        if end > len(ring_bytes):
            raise UniformError(
                f"upload {index} on ring {upload.ring_label!r}: slice "
                f"[{upload.ring_offset}:{end}] (size {upload.size_bytes}) exceeds the "
                f"{len(ring_bytes)}-byte ring available for that label"
            )

        struct_ = struct_by_key.get((upload.slot, upload.size_bytes))
        if struct_ is None:
            raw = ring_bytes[upload.ring_offset:upload.ring_offset + min(_RAW_HEX_BYTES,
                                                                          upload.size_bytes)]
            decoded.append(DecodedUpload(
                index=index, struct_name="unknown", slot=upload.slot,
                ring_label=upload.ring_label, ring_offset=upload.ring_offset,
                values={"rawHex": raw.hex()}, finite=True))
            continue

        values, finite = _decode_struct_fields(struct_, ring_bytes, upload.ring_offset)
        decoded.append(DecodedUpload(
            index=index, struct_name=struct_.name, slot=upload.slot,
            ring_label=upload.ring_label, ring_offset=upload.ring_offset,
            values=values, finite=finite))
    return decoded


def _ring_label_index(label: str):
    """(prefix, index) from "lmx.device.uniformRing.1" -> ("lmx.device.uniformRing", 1), or None
    when `label` doesn't end in a numeric component."""
    match = _LABEL_INDEX_RE.match(label)
    if match is None:
        return None
    return match.group(1), int(match.group(2))


def _buffer_id(name: str):
    match = _BUFFER_ID_RE.match(name)
    return int(match.group(1)) if match else None


def resolve_ring_bytes(schema, bundle) -> RingResolution:
    """Apply the module docstring's positional-attribution policy for every ring `schema.
    uniform_uploads` references, reading blob bytes from `bundle.blobs` (a bundlelib.Bundle).

    `bundle` is never touched when there are no uploads to resolve bytes for -- callers may pass
    None in that case (exercised by tests that don't need a bundle fixture at all).
    """
    upload_ring_labels = {u.ring_label for u in schema.uniform_uploads}
    if not upload_ring_labels:
        return RingResolution({}, "no uniform uploads recorded in this capture")
    if len(upload_ring_labels) > 1:
        return RingResolution(
            {}, f"uploads reference {len(upload_ring_labels)} distinct ring labels in one "
            "capture (expected exactly one); ring bytes not attributed")
    ring_label = next(iter(upload_ring_labels))

    ring_resource = next(
        (r for r in schema.resources if r.kind == "buffer" and r.label == ring_label), None)
    if ring_resource is None:
        return RingResolution(
            {}, f"schema has no buffer resource labelled {ring_label!r}; ring bytes not "
            "attributed")

    parsed = _ring_label_index(ring_label)
    if parsed is None:
        return RingResolution(
            {}, f"ring label {ring_label!r} does not end in a numeric index; ring bytes not "
            "attributed")
    prefix, _ = parsed

    family = sorted(
        (r for r in schema.resources
         if r.kind == "buffer" and (idx := _ring_label_index(r.label)) is not None
         and idx[0] == prefix),
        key=lambda r: _ring_label_index(r.label)[1],
    )

    candidates = [
        b for b in bundle.blobs
        if b.path.name.startswith("MTLBuffer-") and b.size_bytes == ring_resource.size_bytes
    ]

    if len(candidates) == 1:
        data = candidates[0].path.read_bytes()
        return RingResolution(
            {ring_label: data}, f"single ring-sized blob, attributed to {ring_label}")

    if family and len(candidates) == len(family):
        ordered_candidates = sorted(candidates, key=lambda b: (_buffer_id(b.path.name) is None,
                                                                _buffer_id(b.path.name)))
        position = next(i for i, r in enumerate(family) if r.label == ring_label)
        data = ordered_candidates[position].path.read_bytes()
        return RingResolution(
            {ring_label: data},
            "positional (creation-order id) -- untrusted heuristic")

    return RingResolution(
        {}, f"found {len(candidates)} MTLBuffer-* blob(s) sized {ring_resource.size_bytes} "
        f"bytes (expected 1, or {len(family)} to match sibling ring resources); ring bytes not "
        "attributed, uniform uploads skipped")
