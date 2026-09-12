from __future__ import annotations

import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from Tools.png_alpha import has_transparency


def png_bytes(rows: list[bytes], width: int, method: int, depth: int = 8) -> bytes:
    bpp = 4 * depth // 8
    raw = bytearray()
    previous = bytes(len(rows[0]))
    for row in rows:
        raw.append(method)
        for i, value in enumerate(row):
            left = row[i - bpp] if i >= bpp else 0
            up = previous[i]
            corner = previous[i - bpp] if i >= bpp else 0
            p = left + up - corner
            a, b, c = abs(p - left), abs(p - up), abs(p - corner)
            paeth = left if a <= b and a <= c else up if b <= c else corner
            prediction = (0, left, up, (left + up) // 2, paeth)[method]
            raw.append((value - prediction) & 255)
        previous = row

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, len(rows), depth, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


class PngAlphaTests(unittest.TestCase):
    def test_opacity_for_all_filters_and_later_scanlines(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.png"
            for depth in (8, 16):
                factor = depth // 8
                opaque = bytes([19, 220, 71, 255, 6, 90, 150, 255])
                opaque = b"".join(bytes([x]) * factor for x in opaque)
                transparent = bytearray(opaque)
                transparent[-1] = 254
                for method in range(5):
                    with self.subTest(depth=depth, method=method):
                        path.write_bytes(png_bytes([opaque, opaque], 2, method, depth))
                        self.assertFalse(has_transparency(path))
                        path.write_bytes(png_bytes([opaque, bytes(transparent)], 2, method, depth))
                        self.assertTrue(has_transparency(path))

    def test_rejects_non_png(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.png"
            path.write_bytes(b"not PNG")
            with self.assertRaisesRegex(ValueError, "requires PNG"):
                has_transparency(path)


if __name__ == "__main__":
    unittest.main()
