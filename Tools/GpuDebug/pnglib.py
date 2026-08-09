"""Minimal RGB8 PNG writer -- stdlib only (zlib + struct), no Pillow/PIL dependency.

Emits the smallest valid PNG decodelib.py's decoded images need: the 8-byte signature, one IHDR
(bit depth 8, color type 2 == truecolor RGB -- no palette, no alpha channel), one IDAT holding the
zlib-compressed, filter-0-prefixed scanlines, and IEND. Every chunk is length-prefixed (big-endian
u32) and CRC-32'd over its tag+payload, per the PNG spec (ISO/IEC 15948 / RFC 2083).

Run: python3 -c "import pnglib; pnglib.write_png('/tmp/x.png', 2, 1, bytes([255,0,0, 0,255,0]))"
"""
import pathlib
import struct
import zlib

_SIGNATURE = b"\x89PNG\r\n\x1a\n"
_BIT_DEPTH = 8
_COLOR_TYPE_RGB = 2  # truecolor, no alpha
_FILTER_TYPE_NONE = 0  # one filter-type byte prefixes every scanline; "None" leaves it verbatim


def _chunk(tag: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))


def write_png(path, width: int, height: int, rgb8_rows: bytes) -> None:
    """Write a minimal RGB8 PNG to `path`.

    `rgb8_rows` must be exactly width*height*3 bytes: tightly packed, row-major R,G,B texels, no
    filter bytes and no row padding -- both of those are PNG scanline framing added here, not
    something callers should pre-apply.
    """
    expected_len = width * height * 3
    if len(rgb8_rows) != expected_len:
        raise ValueError(
            f"rgb8_rows must be {expected_len} bytes (width*height*3), got {len(rgb8_rows)}"
        )

    stride = width * 3
    filtered = bytearray()
    for y in range(height):
        filtered.append(_FILTER_TYPE_NONE)
        filtered += rgb8_rows[y * stride:(y + 1) * stride]

    ihdr = struct.pack(">IIBBBBB", width, height, _BIT_DEPTH, _COLOR_TYPE_RGB, 0, 0, 0)
    png = (
        _SIGNATURE
        + _chunk(b"IHDR", ihdr)
        + _chunk(b"IDAT", zlib.compress(bytes(filtered)))
        + _chunk(b"IEND", b"")
    )
    pathlib.Path(path).write_bytes(png)
