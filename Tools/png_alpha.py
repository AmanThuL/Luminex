"""Inspect PNG opacity without external imaging dependencies or changing image bytes."""

from __future__ import annotations

import struct
import zlib
from pathlib import Path


def has_transparency(path: Path) -> bool:
    """Return whether a supported PNG contains nonopaque alpha; reject unknown layouts."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: alpha-mask conversion requires PNG textures")
    compressed = bytearray()
    header = None
    offset = 8
    while offset + 12 <= len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        if offset + length + 12 > len(data):
            raise ValueError(f"{path}: truncated PNG chunk")
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", payload)
        elif kind == b"tRNS":
            # Palette/colour-key transparency also requires the masked rendering path.
            return header is None or header[3] != 3 or any(value < 255 for value in payload)
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
        offset += length + 12
    if header is None:
        raise ValueError(f"{path}: missing PNG header")
    width, height, depth, color, compression, filtering, interlace = header
    if color not in (4, 6):
        return False
    if depth not in (8, 16) or compression or filtering or interlace:
        raise ValueError(f"{path}: unsupported alpha PNG layout")
    sample_bytes = depth // 8
    channels = 4 if color == 6 else 2
    stride = width * channels * sample_bytes
    raw = zlib.decompress(compressed)
    if len(raw) != (stride + 1) * height:
        raise ValueError(f"{path}: PNG scanline size mismatch")
    previous = bytearray(width * sample_bytes)
    for row in range(height):
        start = row * (stride + 1)
        method = raw[start]
        if method > 4:
            raise ValueError(f"{path}: invalid PNG filter")
        current = bytearray(width * sample_bytes)
        for i in range(len(current)):
            pixel, component = divmod(i, sample_bytes)
            encoded = raw[start + 1 + (pixel * channels + channels - 1) * sample_bytes + component]
            left = current[i - sample_bytes] if i >= sample_bytes else 0
            up = previous[i]
            corner = previous[i - sample_bytes] if i >= sample_bytes else 0
            prediction = left + up - corner
            distances = (abs(prediction - left), abs(prediction - up), abs(prediction - corner))
            paeth = (left, up, corner)[distances.index(min(distances))]
            predictor = (0, left, up, (left + up) // 2, paeth)[method]
            current[i] = (encoded + predictor) & 255
            if current[i] != 255:
                return True
        previous = current
    return False
