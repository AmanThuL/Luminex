"""Decode schema-declared frame-data uploads (`Schema.frame_data_uploads`/`uniform_structs`)
against the arena-page bytes captured in a `.gputrace` bundle, plus the page-blob attribution
policy those bytes need in the first place.

Buffer attribution follows constraints measured from real capture bundles:

- **Buffer blobs are not label-joinable.** The buffer-contents record's
  receiver is the *device*, not the buffer, so `bundlelib.join()` never attaches a label to a
  `MTLBuffer-*` blob -- it can only match a buffer resource by exact, unambiguous size, and
  correctly refuses to guess among same-size candidates (a frame-data slot's arena pages are all
  the same 256 KiB normal-page size, so several pages sharing one capture are the textbook
  ambiguous case). `resolve_page_bytes` below is the positional fallback `join()` deliberately does
  not attempt:
  1. Every upload in one capture names one page (`pageLabel`); look up that page's `sizeBytes`
     from the matching schema resource.
  2. Candidates = every root-level `MTLBuffer-*` blob whose FILE SIZE equals that `sizeBytes`
     exactly (buffer blobs carry no header, so file size is buffer length).
  3. Exactly one candidate -> use it; Metal typically only dumps the frame's referenced page.
  4. Exactly as many candidates as sibling page resources (every *other frame-in-flight slot's*
     page at the same page index, matched by label shape) -> sort blobs by the numeric `<id>` in
     `MTLBuffer-<id>-<n>` ascending and pick positionally. The real ambiguity a capture hits is
     cross-slot, not within one slot: `Metal4Device` creates exactly one page per slot up front
     (`Metal4FrameArena::create`), so three same-sized page-0 buffers, one per slot, is the common
     case -- the same shape three sibling uniform rings used to have, one per slot, sharing every
     part of the label except the slot number. A single slot growing to several pages in one frame
     is a real but different situation: those pages differ in *index*, not slot, and are never each
     other's attribution family (a within-slot arena never confuses its own pages for one another
     at allocation time, so there is nothing to disambiguate there). Captures show a slot's page 0
     created before the next slot's, so ascending id order lines up with ascending slot number.
     This is explicitly an *untrusted* heuristic -- ordering, not identity -- and is labelled as
     such in the returned attribution.
  5. Any other candidate count -> decline to decode; the caller gets an explanatory note instead
     of a guess.
- **Non-finite floats render as JSON `null`**, matching how `CaptureSchema.cpp`'s `appendFloat`
  already serializes a non-finite context float into the schema sidecar (schemalib.py's
  `test_context_tolerates_null_floats`). A page can legitimately hold uninitialized/NaN bytes
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
# Metal4FrameArena.cpp labels a page "lmx.device.frameData.<slot>.page.<index>" -- the constant
# prefix, the frame-in-flight slot, the literal "page", and the page index within that slot. The
# attribution family groups by (prefix, index) with the slot wildcarded, i.e. every slot's page at
# the same index: that is the cross-slot ambiguity the ring family used to have (three same-size
# rings, one per slot), because a real device creates exactly one page per slot up front.
_PAGE_LABEL_RE = re.compile(r"^(.+)\.(\d+)\.page\.(\d+)$")


class UniformError(Exception):
    """Raised when a schema-declared upload's byte range doesn't fit inside the page bytes
    actually available for its `pageLabel`. Names the page label plus the declared offset/size
    and the available length, so a caller can tell a stale schema from a mis-attributed page
    blob (see the module docstring's `resolve_page_bytes` policy) at a glance.
    """


@dataclasses.dataclass
class DecodedUpload:
    # Deliberately no `gpu_address`: it is a real virtual address that varies run to run even for
    # two captures with byte-identical content, and this record feeds `uniforms.json`, which is
    # meant to be diffable across runs -- `page_label` + `page_offset` + the block's size + this
    # alignment already name the range deterministically, which is what a reader needs to trace an
    # upload back to its arena range without the address itself.
    index: int
    struct_name: str
    slot: int
    page_label: str
    page_offset: int
    alignment_bytes: int
    values: dict
    finite: bool  # False if any decoded float in `values` was NaN/Inf (rendered as None/null)


@dataclasses.dataclass
class PageResolution:
    page_bytes_by_label: dict  # page label -> bytes, only for pages the policy could attribute
    attribution: str  # human-readable note; written verbatim as uniforms.json's "pageAttribution"


def _sanitize_float(value: float):
    """(json_safe_value, finite) -- non-finite floats become None so json.dumps writes `null`
    instead of the non-standard `NaN`/`Infinity` tokens Python would otherwise emit."""
    if math.isfinite(value):
        return value, True
    return None, False


def _decode_field(field, page_bytes: bytes, base_offset: int):
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
        raw = struct.unpack_from("<f", page_bytes, offset)[0]
        return _sanitize_float(raw)
    if field.type == "float3":
        raw = struct.unpack_from("<3f", page_bytes, offset)
        values, finite = [], True
        for component in raw:
            v, ok = _sanitize_float(component)
            values.append(v)
            finite = finite and ok
        return values, finite
    if field.type == "float4":
        raw = struct.unpack_from("<4f", page_bytes, offset)
        values, finite = [], True
        for component in raw:
            v, ok = _sanitize_float(component)
            values.append(v)
            finite = finite and ok
        return values, finite
    if field.type == "float4x4":
        raw = struct.unpack_from("<16f", page_bytes, offset)
        flat, finite = [], True
        for component in raw:
            v, ok = _sanitize_float(component)
            flat.append(v)
            finite = finite and ok
        columns = [flat[c * 4:(c + 1) * 4] for c in range(4)]
        rows = [[columns[c][r] for c in range(4)] for r in range(4)]
        return rows, finite
    if field.type == "int":
        return struct.unpack_from("<i", page_bytes, offset)[0], True
    if field.type == "uint":
        return struct.unpack_from("<I", page_bytes, offset)[0], True
    raise UniformError(
        f"field {field.name!r}: unrecognized uniform field type {field.type!r}"
    )


def _decode_struct_fields(struct_, page_bytes: bytes, page_offset: int):
    values, finite = {}, True
    for field in struct_.fields:
        value, ok = _decode_field(field, page_bytes, page_offset)
        values[field.name] = value
        finite = finite and ok
    return values, finite


def decode_uploads(schema, page_bytes_by_label: dict) -> list:
    """Decode every `schema.frame_data_uploads` entry against `page_bytes_by_label`.

    `page_bytes_by_label` maps a page label to the bytes attributed to it (see
    `resolve_page_bytes`) -- an upload whose page has no entry is treated as an empty page (bounds
    check below fails immediately, raising UniformError) rather than silently skipped, so a caller
    that forgets to resolve a page finds out from the exception rather than a quietly-empty
    manifest.

    Struct resolution is by exact (slot, sizeBytes) match against `schema.uniform_structs` -- the
    plan Reference states that pair is unique across the four registered structs. No match decodes
    as `struct_name="unknown"` with the upload's first 64 raw bytes as hex, so the sidecar is
    always legible even for a struct this schema version doesn't recognize.
    """
    struct_by_key = {(s.slot, s.size_bytes): s for s in schema.uniform_structs}

    decoded = []
    for index, upload in enumerate(schema.frame_data_uploads):
        page_bytes = page_bytes_by_label.get(upload.page_label, b"")
        end = upload.page_offset + upload.size_bytes
        if end > len(page_bytes):
            raise UniformError(
                f"upload {index} on page {upload.page_label!r}: slice "
                f"[{upload.page_offset}:{end}] (size {upload.size_bytes}) exceeds the "
                f"{len(page_bytes)}-byte page available for that label"
            )

        struct_ = struct_by_key.get((upload.slot, upload.size_bytes))
        if struct_ is None:
            raw = page_bytes[upload.page_offset:upload.page_offset + min(_RAW_HEX_BYTES,
                                                                          upload.size_bytes)]
            decoded.append(DecodedUpload(
                index=index, struct_name="unknown", slot=upload.slot,
                page_label=upload.page_label, page_offset=upload.page_offset,
                alignment_bytes=upload.alignment_bytes, values={"rawHex": raw.hex()}, finite=True))
            continue

        values, finite = _decode_struct_fields(struct_, page_bytes, upload.page_offset)
        decoded.append(DecodedUpload(
            index=index, struct_name=struct_.name, slot=upload.slot,
            page_label=upload.page_label, page_offset=upload.page_offset,
            alignment_bytes=upload.alignment_bytes, values=values, finite=finite))
    return decoded


def _page_label_parts(label: str):
    """(prefix, slot, index) from "lmx.device.frameData.0.page.1" ->
    ("lmx.device.frameData", 0, 1), or None when `label` doesn't match a frame-data page label's
    shape."""
    match = _PAGE_LABEL_RE.match(label)
    if match is None:
        return None
    return match.group(1), int(match.group(2)), int(match.group(3))


def _buffer_id(name: str):
    match = _BUFFER_ID_RE.match(name)
    return int(match.group(1)) if match else None


def resolve_page_bytes(schema, bundle) -> PageResolution:
    """Apply the module docstring's positional-attribution policy for every page `schema.
    frame_data_uploads` references, reading blob bytes from `bundle.blobs` (a bundlelib.Bundle).

    Every upload of one capture is expected to name the same page label -- a frame's blocks
    virtually always fit in the one page its slot starts with, and this declines to decode rather
    than guess when that expectation is violated, exactly as it declines on any other ambiguity.

    `bundle` is never touched when there are no uploads to resolve bytes for -- callers may pass
    None in that case (exercised by tests that don't need a bundle fixture at all).
    """
    upload_page_labels = {u.page_label for u in schema.frame_data_uploads}
    if not upload_page_labels:
        return PageResolution({}, "no frame-data uploads recorded in this capture")
    if len(upload_page_labels) > 1:
        return PageResolution(
            {}, f"uploads reference {len(upload_page_labels)} distinct page labels in one "
            "capture (expected exactly one); page bytes not attributed")
    page_label = next(iter(upload_page_labels))

    page_resource = next(
        (r for r in schema.resources if r.kind == "buffer" and r.label == page_label), None)
    if page_resource is None:
        return PageResolution(
            {}, f"schema has no buffer resource labelled {page_label!r}; page bytes not "
            "attributed")

    parsed = _page_label_parts(page_label)
    if parsed is None:
        return PageResolution(
            {}, f"page label {page_label!r} does not match the frame-data page label shape; page "
            "bytes not attributed")
    prefix, _slot, page_index = parsed

    # The family is every slot's page at the SAME index, slot wildcarded -- the cross-slot
    # ambiguity a real capture hits (one page-0 per frame-in-flight slot). A different slot's page
    # at a different index is not a sibling: within one slot's own arena, pages differ from each
    # other in index, never in identity, so there is nothing there to disambiguate.
    family = sorted(
        (r for r in schema.resources
         if r.kind == "buffer" and (parts := _page_label_parts(r.label)) is not None
         and parts[0] == prefix and parts[2] == page_index),
        key=lambda r: _page_label_parts(r.label)[1],  # ascending slot
    )

    candidates = [
        b for b in bundle.blobs
        if b.path.name.startswith("MTLBuffer-") and b.size_bytes == page_resource.size_bytes
    ]

    if len(candidates) == 1:
        data = candidates[0].path.read_bytes()
        return PageResolution(
            {page_label: data}, f"single page-sized blob, attributed to {page_label}")

    if family and len(candidates) == len(family):
        ordered_candidates = sorted(candidates, key=lambda b: (_buffer_id(b.path.name) is None,
                                                                _buffer_id(b.path.name)))
        position = next(i for i, r in enumerate(family) if r.label == page_label)
        data = ordered_candidates[position].path.read_bytes()
        return PageResolution(
            {page_label: data},
            "positional (creation-order id) -- untrusted heuristic")

    return PageResolution(
        {}, f"found {len(candidates)} MTLBuffer-* blob(s) sized {page_resource.size_bytes} "
        f"bytes (expected 1, or {len(family)} to match sibling page resources); page bytes not "
        "attributed, frame-data uploads skipped")
