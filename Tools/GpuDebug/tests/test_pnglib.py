"""Tests for pnglib.py -- a minimal RGB8 PNG writer (stdlib zlib/struct only).

Run: python3 -m unittest discover -s Tools/GpuDebug/tests -v
"""
import pathlib
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

import pnglib  # noqa: E402


class WritePngTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.path = pathlib.Path(self._tmp.name) / "out.png"

    def test_golden_2x1_red_green(self):
        # One row: a red texel then a green texel.
        rgb = bytes([255, 0, 0, 0, 255, 0])

        pnglib.write_png(self.path, 2, 1, rgb)

        data = self.path.read_bytes()
        self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")

        # IHDR is always the first chunk right after the signature: 4-byte length + "IHDR" +
        # 13-byte payload + 4-byte CRC.
        self.assertEqual(data[12:16], b"IHDR")
        ihdr_payload = data[16:29]
        width, height, bit_depth, color_type, compression, filter_method, interlace = (
            struct.unpack(">IIBBBBB", ihdr_payload)
        )
        self.assertEqual((width, height), (2, 1))
        self.assertEqual(bit_depth, 8)
        self.assertEqual(color_type, 2)  # truecolor RGB, no alpha, no palette
        self.assertEqual((compression, filter_method, interlace), (0, 0, 0))

        # Locate IDAT: signature(8) + IHDR chunk(4 len + 4 tag + 13 payload + 4 crc = 25) = 33.
        idat_len = struct.unpack(">I", data[33:37])[0]
        self.assertEqual(data[37:41], b"IDAT")
        idat_payload = data[41:41 + idat_len]
        raw = zlib.decompress(idat_payload)
        # One scanline: filter-type byte (0 == "None") + the row's 6 RGB bytes, unchanged.
        self.assertEqual(raw, bytes([0]) + rgb)

        # IEND is a fixed 12-byte trailer: 4-byte length(0) + "IEND" + 4-byte CRC.
        self.assertEqual(data[-8:-4], b"IEND")

    def test_two_row_image_gets_one_filter_byte_per_row(self):
        rgb = bytes(range(1, 1 + 2 * 3 * 2))  # 2x2 image, distinct bytes so rows are identifiable

        pnglib.write_png(self.path, 2, 2, rgb)

        data = self.path.read_bytes()
        idat_len = struct.unpack(">I", data[33:37])[0]
        idat_payload = data[41:41 + idat_len]
        raw = zlib.decompress(idat_payload)
        stride = 2 * 3
        self.assertEqual(raw, bytes([0]) + rgb[:stride] + bytes([0]) + rgb[stride:])

    def test_wrong_length_rgb8_rows_raises(self):
        with self.assertRaises(ValueError):
            pnglib.write_png(self.path, 2, 1, bytes([0, 0, 0]))  # only one texel's worth


if __name__ == "__main__":
    unittest.main()
