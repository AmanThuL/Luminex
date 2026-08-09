"""Decode a captured texture blob's primary image to RGB8 pixels and statistics.

- Every texture blob opens with a 256-byte self-describing header (bundlelib.parse_blob_header).
  Decode geometry -- pixel format, width, height, bytesPerRow, the payload's byte extent -- always
  comes from THAT header, never from the schema sidecar's `resource`: a matched blob can be a
  higher mip with mip-adjusted dims, and the header is always the blob's own.
- Rows are read at the header's bytesPerRow stride, never width*bytesPerTexel -- that stride can
  be (and in BC1's case, is defined to be) wider than the tight row.
- Header pixelFormat is an MTLPixelFormat numeric. Only five are meaningful here: 70 RGBA8Unorm,
  80 BGRA8Unorm, 81 BGRA8Unorm_sRGB, 131 BC1_RGBA_sRGB, 252 Depth32Float. BC1 is a deliberate,
  expected DecodeError (it's the single most common format in a real capture -- all scene albedo
  plus the sky cubemap -- so matched_undecodable is the normal path for it, not a broken
  manifest). Any other numeric is genuinely unrecognized and also raises, with the numeric named
  in the reason so a caller/operator can look it up.

Run: python3 -c "import decodelib"  (see tests/test_decodelib.py for worked examples)
"""
import array
import dataclasses
import math
import struct
import sys

import bundlelib

# Header pixelFormat numerics are MTLPixelFormat values.
_RGBA8_UNORM = 70
_BGRA8_UNORM = 80
_BGRA8_UNORM_SRGB = 81
_BC1_RGBA_SRGB = 131
_DEPTH32_FLOAT = 252

_BGRA_FORMATS = frozenset((_BGRA8_UNORM, _BGRA8_UNORM_SRGB))
_RGBA_FORMATS = frozenset((_RGBA8_UNORM,))
_COLOR_FORMATS = _BGRA_FORMATS | _RGBA_FORMATS

_CLEAR_DEPTH = 1.0  # fractionAtClear counts texels exactly at the clear value.

# Bytes per texel for every format decodelib actually reads row data for (D32Float and the three
# 8-bit-per-channel color formats are all 4 bytes/texel). BC1 never reaches _validate_geometry --
# it raises unconditionally before any payload read, since bytesPerRow there is a block-row
# stride, not a per-texel one, and this module doesn't decode compressed payloads at all.
_BYTES_PER_TEXEL = 4


class DecodeError(Exception):
    """Raised when a texture blob cannot be decoded to an image.

    The message always names the reason (an out-of-scope compressed format, an unrecognized
    MTLPixelFormat numeric, a missing/short header) -- callers bucket the resource into
    matched_undecodable with this text, so it must read as "expected", not "something broke".
    """


@dataclasses.dataclass
class Decoded:
    png_rgb_rows: bytes
    width: int
    height: int
    stats: dict


def decode_texture(resource, blob_bytes: bytes) -> Decoded:
    """Decode one texture blob's contents (the full blob file's bytes, header included).

    `resource` (a schemalib.Resource) is used only to name the resource in DecodeError messages;
    all decode geometry comes from the blob's own header, per the module docstring.
    """
    header = bundlelib.parse_blob_header(blob_bytes)
    if header is None:
        raise DecodeError(f"{resource.label}: blob has no capture-blob header, cannot decode")

    payload_end = header.header_size + header.bytes_per_image
    if payload_end > len(blob_bytes):
        raise DecodeError(
            f"{resource.label}: header's payload extent ({payload_end} bytes) exceeds the blob "
            f"file size ({len(blob_bytes)} bytes)"
        )
    payload = blob_bytes[header.header_size:payload_end]

    if header.pixel_format == _DEPTH32_FLOAT:
        _validate_geometry(resource, header, len(payload))
        return _decode_depth32(header, payload)
    if header.pixel_format in _COLOR_FORMATS:
        _validate_geometry(resource, header, len(payload))
        return _decode_color8(header, payload, is_bgra=header.pixel_format in _BGRA_FORMATS)
    if header.pixel_format == _BC1_RGBA_SRGB:
        raise DecodeError(
            f"{resource.label}: BC1_RGBA_sRGB (compressed) is out of scope for pixel decode -- "
            "this is the normal case for scene albedo and the sky cubemap, not a broken manifest"
        )
    raise DecodeError(
        f"{resource.label}: unsupported MTLPixelFormat numeric {header.pixel_format}, cannot "
        "decode"
    )


def _validate_geometry(resource, header: bundlelib.BlobHeader, payload_len: int) -> None:
    """Reject a header whose own fields are internally inconsistent, before any row-decode loop
    touches the payload.

    bundlelib.join() only validates width/height against the schema and that headerSize +
    bytesPerImage fits the blob *file* size -- it never checks bytesPerRow at all. A header that
    passes join() but has bytesPerRow < the tight row size, or a bytesPerRow*height that doesn't
    fit the *payload* (== bytesPerImage, already sliced out by the caller), can otherwise trigger
    a raw ValueError/ZeroDivisionError or silently produce a partially-black image. Rejecting it
    here produces an explicit DecodeError that names the inconsistent fields.
    """
    width, height, bytes_per_row = header.width, header.height, header.bytes_per_row
    if width <= 0 or height <= 0:
        raise DecodeError(
            f"{resource.label}: header reports non-positive dimensions {width}x{height}, "
            "cannot decode"
        )
    tight_row = width * _BYTES_PER_TEXEL
    if bytes_per_row < tight_row:
        raise DecodeError(
            f"{resource.label}: header bytesPerRow ({bytes_per_row}) is smaller than the tight "
            f"row size (width {width} * {_BYTES_PER_TEXEL} bytes/texel = {tight_row}), cannot "
            "decode"
        )
    if bytes_per_row * height > payload_len:
        raise DecodeError(
            f"{resource.label}: header bytesPerRow * height ({bytes_per_row} * {height} = "
            f"{bytes_per_row * height}) exceeds the blob's payload size ({payload_len} bytes), "
            "cannot decode"
        )


def _decode_depth32(header: bundlelib.BlobHeader, payload: bytes) -> Decoded:
    width, height, bytes_per_row = header.width, header.height, header.bytes_per_row

    values = array.array("f")
    for y in range(height):
        row_start = y * bytes_per_row
        row = array.array("f")
        row.frombytes(payload[row_start:row_start + width * 4])
        if sys.byteorder != "little":
            row.byteswap()
        values.extend(row)

    total = len(values)
    # Not every captured depth plane holds real depth: lmx.render.sceneDepth (renderTarget-only,
    # never sampled -- see Renderer.cpp's createTexture call) came back from a live capture as a
    # uniform plane of 0xFFFFFFFF, i.e. bit-for-bit NaN, because Xcode's GPU capture cannot
    # resolve a texture with no ShaderRead usage. min/max/mean/fractionAtClear must only ever
    # summarize finite samples -- a NaN silently poisoning a Python sum() would otherwise turn an
    # honest "no valid data" plane into a manifest that just says "mean": NaN (invalid JSON, and a
    # worse lie than fake contrast would have been).
    valid = [v for v in values if math.isfinite(v)]
    at_clear = sum(1 for v in valid if v == _CLEAR_DEPTH)
    if valid:
        min_v, max_v = min(valid), max(valid)
        mean_v = sum(valid) / len(valid)
    else:
        min_v = max_v = mean_v = None

    # Bit-pattern equality, not float equality: NaN != NaN would otherwise report "not flat" for
    # a plane whose every texel is the identical non-finite pattern.
    flat = total == 0 or values.tobytes() == struct.pack("<f", values[0]) * total

    # Record the TRUE value range in stats before normalizing -- a depth map hovering at 1.0
    # must not silently gain fake contrast in the numbers, only in the (necessarily normalized)
    # grayscale PNG. Texels with no valid range to normalize against (flat, or non-finite) render
    # as plain black rather than participating in arithmetic that would raise or fabricate a value.
    can_normalize = bool(valid) and max_v != min_v
    scale = 255.0 / (max_v - min_v) if can_normalize else 0.0
    rgb_rows = bytearray(width * height * 3)
    for i, v in enumerate(values):
        gray = (max(0, min(255, round((v - min_v) * scale)))
               if can_normalize and math.isfinite(v) else 0)
        rgb_rows[i * 3] = gray
        rgb_rows[i * 3 + 1] = gray
        rgb_rows[i * 3 + 2] = gray

    stats = {
        "min": min_v,
        "max": max_v,
        "mean": mean_v,
        "fractionAtClear": (at_clear / total) if total else 0.0,
        "flat": flat,
    }
    return Decoded(png_rgb_rows=bytes(rgb_rows), width=width, height=height, stats=stats)


def _decode_color8(header: bundlelib.BlobHeader, payload: bytes, is_bgra: bool) -> Decoded:
    width, height, bytes_per_row = header.width, header.height, header.bytes_per_row
    rgb_rows = bytearray(width * height * 3)

    r_min = g_min = b_min = 255
    r_max = g_max = b_max = 0
    r_sum = g_sum = b_sum = 0

    for y in range(height):
        row_start = y * bytes_per_row
        row = payload[row_start:row_start + width * 4]
        if is_bgra:
            r_ch, g_ch, b_ch = row[2::4], row[1::4], row[0::4]
        else:
            r_ch, g_ch, b_ch = row[0::4], row[1::4], row[2::4]

        out_start = y * width * 3
        rgb_rows[out_start:out_start + width * 3:3] = r_ch
        rgb_rows[out_start + 1:out_start + 1 + width * 3:3] = g_ch
        rgb_rows[out_start + 2:out_start + 2 + width * 3:3] = b_ch

        r_min, r_max = min(r_min, min(r_ch)), max(r_max, max(r_ch))
        g_min, g_max = min(g_min, min(g_ch)), max(g_max, max(g_ch))
        b_min, b_max = min(b_min, min(b_ch)), max(b_max, max(b_ch))
        r_sum += sum(r_ch)
        g_sum += sum(g_ch)
        b_sum += sum(b_ch)

    total = width * height
    stats = {
        "r": {"min": r_min, "max": r_max, "mean": r_sum / total},
        "g": {"min": g_min, "max": g_max, "mean": g_sum / total},
        "b": {"min": b_min, "max": b_max, "mean": b_sum / total},
        # isFlatImage oracle: true only when every texel is identical, i.e. each channel is
        # independently constant (and therefore so is the R,G,B tuple as a whole).
        "flat": r_min == r_max and g_min == g_max and b_min == b_max,
    }
    return Decoded(png_rgb_rows=bytes(rgb_rows), width=width, height=height, stats=stats)
