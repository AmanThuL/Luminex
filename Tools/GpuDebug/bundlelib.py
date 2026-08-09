"""Walk a .gputrace capture bundle, recover lmx.* labels, and join them to on-disk blob files.

The format assumptions below were measured on real macOS 26 capture bundles:

- A bundle is a directory. Content blobs (MTLTexture-<id>-<n>-mipmap<M>-slice<S>,
  MTLBuffer-<id>-<n>, CAMetalLayer-<id>-index-<n>) sit at its ROOT, not under a subdirectory.
- Texture blobs open with a 256-byte self-describing header (see parse_blob_header); buffer
  blobs have none.
- Labels never appear inside a blob file. They live in the `*device-resources-*` streams
  (device-resources-0x<ptr>, delta-device-resources-0x<ptr>, unused-device-resources-0x<ptr>),
  which are Apple archives of serialized Metal calls. Each record opens with a NUL-terminated
  ASCII type descriptor at a 4-byte-aligned offset, followed (after alignment padding) by an
  8-byte receiver handle. A "CS" descriptor's payload is a setLabel: string; a descriptor
  containing "<b>" names a blob file. Same receiver -> same resource: join a label to a blob
  through the receiver, then the blob's resource-id prefix (e.g. "MTLTexture-16"), then every
  file on disk sharing that prefix (one resource can own many blobs: one per mip/slice).
- Buffers are not label-joinable (their contents record's receiver is the device, not the
  buffer) -- buffers join to blobs by exact, unambiguous size instead.

Run: python3 -c "import bundlelib; b = bundlelib.walk_bundle('x.gputrace'); print(b.blobs)"
"""
import dataclasses
import pathlib
import re
import struct

RUN_COMMAND_HINT = (
    "MTL_CAPTURE_ENABLED=1 LMX_MAX_FRAMES=<N> LMX_CAPTURE_AT_FRAME=<M> "
    "LMX_CAPTURE_PATH=<out>.gputrace ./App   (run from the App's build dir)"
)

# --- Blob-content header -----------------------------------------------------------------------

BLOB_HEADER_MAGIC = b"erutpac\x00"  # ASCII "capture\0" stored as a little-endian u64.
BLOB_HEADER_SIZE = 256
EXPECTED_HEADER_VERSION = 0x00010002

_BLOB_NAME_PREFIXES = ("MTLTexture-", "MTLBuffer-", "CAMetalLayer-")
_DEVICE_RESOURCES_PREFIXES = (
    "device-resources-0x",
    "delta-device-resources-0x",
    "unused-device-resources-0x",
)


class BundleError(Exception):
    """Raised when a bundle directory or its device-resources content is absent/unreadable.

    Every message includes the run command that produces a fresh bundle, since the usual cause
    is "I haven't captured one yet" rather than a bug in this tool.
    """


@dataclasses.dataclass
class Blob:
    path: pathlib.Path
    size_bytes: int


@dataclasses.dataclass
class Bundle:
    root: pathlib.Path
    metadata: dict
    blobs: list  # list[Blob], root-level content files only
    device_resource_files: list  # list[pathlib.Path], the *device-resources-* streams


@dataclasses.dataclass
class LabelHit:
    label: str
    # e.g. "MTLTexture-16" or "MTLBuffer-13": the receiver's resource-id prefix, shared by every
    # blob file (one per mip/slice) that belongs to the same resource. Not a blob_path, because
    # one label legitimately maps to many blob files -- join()
    # resolves the prefix to actual files on disk.
    resource_id_prefix: str
    offset: int  # byte offset of the label string within its device-resources stream (diagnostic)


@dataclasses.dataclass
class JoinResult:
    matched: list  # list[tuple[Resource, Blob]] -- one entry per (resource, blob) pair
    matched_undecodable: list  # list[tuple[Resource, str]] -- resource + reason
    unmatched_blobs: list  # list[Blob]
    missing_resources: list  # list[Resource]


@dataclasses.dataclass
class BlobHeader:
    version: int
    header_size: int
    kind: int
    pixel_format: int
    width: int
    height: int
    depth: int
    bytes_per_row: int
    bytes_per_image: int


def parse_blob_header(data: bytes) -> BlobHeader | None:
    """Decode the 256-byte capture-blob header, or None when `data` doesn't start with one.

    `data` only needs to hold the first BLOB_HEADER_SIZE bytes of the file -- callers should read
    a prefix, not the whole blob (blobs can be tens of megabytes).
    """
    if len(data) < BLOB_HEADER_SIZE or not data.startswith(BLOB_HEADER_MAGIC):
        return None
    version, header_size = struct.unpack_from("<II", data, 8)
    kind, pixel_format, width, height, depth, bytes_per_row, bytes_per_image = struct.unpack_from(
        "<QQQQQQQ", data, 16
    )
    return BlobHeader(
        version=version,
        header_size=header_size,
        kind=kind,
        pixel_format=pixel_format,
        width=width,
        height=height,
        depth=depth,
        bytes_per_row=bytes_per_row,
        bytes_per_image=bytes_per_image,
    )


def _read_prefix(path: pathlib.Path, n: int = BLOB_HEADER_SIZE) -> bytes:
    with path.open("rb") as f:
        return f.read(n)


def _is_blob_name(name: str) -> bool:
    return name.startswith(_BLOB_NAME_PREFIXES)


def _is_device_resources_name(name: str) -> bool:
    return name.startswith(_DEVICE_RESOURCES_PREFIXES)


def walk_bundle(path) -> Bundle:
    """Enumerate a bundle's root-level blobs, metadata plist, and device-resources streams.

    Raises BundleError when `path` is not a directory, or when it has no `*device-resources-*`
    stream (the join algorithm has nothing to work from without one).
    """
    root = pathlib.Path(path)
    if not root.is_dir():
        raise BundleError(
            f"bundle not found: {root}\nProduce one with: {RUN_COMMAND_HINT}"
        )

    metadata = {}
    metadata_path = root / "metadata"
    if metadata_path.is_file():
        import plistlib

        try:
            metadata = plistlib.loads(metadata_path.read_bytes())
        except Exception as exc:  # plistlib raises several exception types; all are I/O-ish here
            raise BundleError(
                f"{metadata_path}: failed to parse metadata plist: {exc}\n"
                f"Produce one with: {RUN_COMMAND_HINT}"
            ) from exc

    entries = sorted(p for p in root.iterdir() if p.is_file())
    device_resource_files = [p for p in entries if _is_device_resources_name(p.name)]
    if not device_resource_files:
        raise BundleError(
            f"bundle {root} has no device-resources content (expected a file named "
            "device-resources-0x<ptr> at the bundle root)\n"
            f"Produce one with: {RUN_COMMAND_HINT}"
        )

    blobs = [Blob(path=p, size_bytes=p.stat().st_size) for p in entries if _is_blob_name(p.name)]

    return Bundle(root=root, metadata=metadata, blobs=blobs,
                  device_resource_files=device_resource_files)


# --- Label recovery ---------------------------------------------------------------------------

# A descriptor: 'C' followed by 0-62 characters from Apple's Objective-C type-encoding alphabet
# used here, terminated by NUL. Matches are filtered to 4-byte-aligned start offsets below --
# the alignment is what keeps this from over-matching inside arbitrary payload bytes.
_DESCRIPTOR_RE = re.compile(rb"C[@<>a-zA-Z0-9]{0,62}\x00")
# A blob file name embedded in a "<b>" record's payload: one of the three blob prefixes followed
# by printable, non-space ASCII up to the terminating NUL.
_BLOB_NAME_RE = re.compile(rb"(?:MTLTexture|MTLBuffer|CAMetalLayer)[!-~]*\x00")


def _iter_aligned_descriptors(data: bytes):
    """Yield (descriptor_bytes_without_nul, start_offset) for each 4-byte-aligned match."""
    for m in _DESCRIPTOR_RE.finditer(data):
        if m.start() % 4 == 0:
            yield m.group(0)[:-1], m.start()


def _resource_id_prefix(blob_name: str) -> str:
    """"MTLTexture-16-0-mipmap0-slice0" -> "MTLTexture-16"; "MTLBuffer-13-0" -> "MTLBuffer-13"."""
    parts = blob_name.split("-")
    return "-".join(parts[:2]) if len(parts) >= 2 else blob_name


def scan_labels(bundle: Bundle) -> list:
    """Recover every lmx.* (and other) label from the bundle's device-resources streams.

    Within each stream, a "CS" record and
    a "<b>" record that share the same receiver handle describe the same resource. The label is
    read as a true NUL-terminated C string starting right after the receiver -- NOT clipped at
    the next descriptor-shaped match, which is what silently truncated "lmx.render.sceneColor" to
    "lmx.render.scene" during format exploration (its own suffix "Color\\0" is itself a
    well-formed, 4-byte-aligned descriptor).
    """
    # receiver -> {"label": str|None, "offset": int|None, "prefix": str|None}
    receivers: dict[int, dict] = {}

    for stream_path in bundle.device_resource_files:
        data = stream_path.read_bytes()
        for desc, start in _iter_aligned_descriptors(data):
            nul_pos = start + len(desc)  # position of the descriptor's terminating NUL
            payload_start = ((nul_pos + 4) // 4) * 4 + 8  # skip alignment pad + 8-byte receiver
            receiver_offset = payload_start - 8
            if receiver_offset + 8 > len(data):
                continue
            receiver = struct.unpack_from("<Q", data, receiver_offset)[0]
            entry = receivers.setdefault(receiver, {"label": None, "offset": None, "prefix": None})

            if desc == b"CS":
                nul = data.find(b"\x00", payload_start)
                if nul == -1:
                    continue
                entry["label"] = data[payload_start:nul].decode("ascii", errors="replace")
                entry["offset"] = payload_start
            elif b"<b>" in desc:
                window = data[payload_start:payload_start + 256]
                match = _BLOB_NAME_RE.search(window)
                if match is None:
                    continue
                blob_name = match.group(0)[:-1].decode("ascii", errors="replace")
                entry["prefix"] = _resource_id_prefix(blob_name)

    hits = []
    for entry in receivers.values():
        if entry["label"] is not None and entry["prefix"] is not None:
            hits.append(LabelHit(label=entry["label"], resource_id_prefix=entry["prefix"],
                                 offset=entry["offset"]))
    return hits


# --- Join --------------------------------------------------------------------------------------

_NO_CONTENTS_CAPTURED = "no contents captured"
_GEOMETRY_DISAGREES = "header geometry disagrees with schema"
_PAYLOAD_EXCEEDS_FILE = "header bytesPerImage exceeds the blob file size"

_MIP_SLICE_RE = re.compile(r"mipmap(\d+)-slice(\d+)")


def _mip_level(blob) -> int | None:
    """The M in "...-mipmap<M>-slice<S>", or None for buffer/drawable blobs (no mip naming)."""
    match = _MIP_SLICE_RE.search(blob.path.name)
    return int(match.group(1)) if match else None


def _select_primary_texture_blob(candidates: list):
    """The lowest-numbered mip level present -- mipmap0-slice0 when fully captured, else
    whichever mip *is* on disk (a partial capture is normal, e.g. only 3 of the sky
    cubemap's 11 mips). Sorted numerically, not lexicographically: "mipmap10-slice0" sorts before
    "mipmap2-slice0" as a string, which would silently pick the wrong (higher) level and false-
    reject an otherwise-fine partial capture against the wrong expected dimensions. Buffers and
    drawables have no mip/slice naming and fall back to the alphabetically-first candidate."""
    numbered = [(level, blob) for blob in candidates if (level := _mip_level(blob)) is not None]
    if numbered:
        numbered.sort(key=lambda pair: (pair[0], pair[1].path.name))
        return numbered[0][1]
    ordered = sorted(candidates, key=lambda b: b.path.name)
    return ordered[0] if ordered else None


def _expected_mip_dims(base_width: int, base_height: int, mip_level: int) -> tuple:
    """Standard mip-pyramid halving, floored at 1 texel -- what the header of a mip-<mip_level>
    blob should show, given the schema's level-0 (base) width/height."""
    return max(1, base_width >> mip_level), max(1, base_height >> mip_level)


def _texture_disagreement(header: BlobHeader | None, resource, primary) -> str | None:
    """None when `primary`'s header is absent (nothing to check) or consistent with `resource`;
    else a reason string for matched_undecodable -- never a silent mismatch and never a match
    failure: the header wins over the schema, and a disagreement is a real anomaly rather than
    "no record at all"."""
    if header is None:
        return None
    mip_level = _mip_level(primary)
    expected_width, expected_height = (
        _expected_mip_dims(resource.width, resource.height, mip_level)
        if mip_level is not None
        else (resource.width, resource.height)
    )
    if header.width != expected_width or header.height != expected_height:
        return _GEOMETRY_DISAGREES
    if header.header_size + header.bytes_per_image > primary.size_bytes:
        return _PAYLOAD_EXCEEDS_FILE
    return None


def join(bundle: Bundle, schema, hits: list) -> JoinResult:
    """Bucket every schema resource and bundle blob according to the measured bundle format.

    - texture2d/cube: label -> receiver -> resource-id prefix -> every blob sharing that prefix.
      A resource with a label hit but no on-disk blob is normal (fallback textures,
      most sky mips) and buckets as matched_undecodable/"no contents captured", still exit 0. A
      resource whose primary blob's header geometry (or bytesPerImage-vs-file-size) disagrees
      with the schema is a real anomaly -- the header wins over the schema, so this
      is never a silent mismatch, but it also must not collapse into "no record at all": it
      buckets into matched_undecodable with a distinct reason for anomaly reporting.
    - buffer: not label-joinable because contents records identify the device receiver, so buffers
      match by exact, unambiguous size among not-yet-claimed MTLBuffer-* blobs -- unambiguous on
      *both* sides: exactly one candidate blob AND exactly one resource wanting that size.
    """
    hits_by_label = {}
    for hit in hits:
        hits_by_label.setdefault(hit.label, hit)

    blobs_by_prefix: dict[str, list] = {}
    for blob in bundle.blobs:
        blobs_by_prefix.setdefault(_resource_id_prefix(blob.path.name), []).append(blob)

    claimed_paths: set = set()
    matched = []
    matched_undecodable = []
    missing_resources = []
    buffer_resources = []

    for resource in schema.resources:
        if resource.kind == "buffer":
            buffer_resources.append(resource)
            continue

        hit = hits_by_label.get(resource.label)
        if hit is None:
            missing_resources.append(resource)
            continue

        candidates = blobs_by_prefix.get(hit.resource_id_prefix, [])
        if not candidates:
            matched_undecodable.append((resource, _NO_CONTENTS_CAPTURED))
            continue

        primary = _select_primary_texture_blob(candidates)
        header = parse_blob_header(_read_prefix(primary.path)) if primary is not None else None
        disagreement = _texture_disagreement(header, resource, primary)
        if disagreement is not None:
            matched_undecodable.append((resource, disagreement))
            continue

        for blob in candidates:
            matched.append((resource, blob))
            claimed_paths.add(blob.path)

    unclaimed_buffer_blobs = [
        b for b in bundle.blobs
        if b.path.name.startswith("MTLBuffer-") and b.path not in claimed_paths
    ]
    size_index: dict[int, list] = {}
    for blob in unclaimed_buffer_blobs:
        size_index.setdefault(blob.size_bytes, []).append(blob)

    # Ambiguity must be judged on BOTH sides of the size key, not just the blob side: three
    # same-size ring resources against one same-size blob is exactly as ambiguous as one resource
    # against three blobs. Counting only blobs would let the first same-sized resource claim one
    # plausible blob arbitrarily.
    resource_size_counts: dict[int, int] = {}
    for resource in buffer_resources:
        resource_size_counts[resource.size_bytes] = resource_size_counts.get(
            resource.size_bytes, 0) + 1

    for resource in buffer_resources:
        candidates = size_index.get(resource.size_bytes, [])
        if not candidates:
            matched_undecodable.append((resource, _NO_CONTENTS_CAPTURED))
        elif resource_size_counts[resource.size_bytes] == 1 and len(candidates) == 1:
            blob = candidates[0]
            matched.append((resource, blob))
            claimed_paths.add(blob.path)
            size_index[resource.size_bytes] = []
        else:
            # Ambiguous: several resources share this size, or several blobs do (or both) --
            # no way to say which blob belongs to which resource.
            missing_resources.append(resource)

    unmatched_blobs = [b for b in bundle.blobs if b.path not in claimed_paths]

    return JoinResult(matched=matched, matched_undecodable=matched_undecodable,
                      unmatched_blobs=unmatched_blobs, missing_resources=missing_resources)


def first_texture_header_version(bundle: Bundle) -> int | None:
    """The `version` field of the first MTLTexture-* blob with a parseable header, else None."""
    for blob in sorted(bundle.blobs, key=lambda b: b.path.name):
        if not blob.path.name.startswith("MTLTexture-"):
            continue
        header = parse_blob_header(_read_prefix(blob.path))
        if header is not None:
            return header.version
    return None
