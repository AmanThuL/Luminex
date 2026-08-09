"""Tests for decodelib.py -- capture-blob texture contents -> RGB8 pixels + stats.

Decode geometry (format/width/height/bytesPerRow) always
comes from the blob's own 256-byte header, never the schema, and rows must be read at the
header's bytesPerRow stride -- never width*bytesPerTexel -- since that stride can be padded.

Run: python3 -m unittest discover -s Tools/GpuDebug/tests -v
"""
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

import bundlelib  # noqa: E402
import decodelib  # noqa: E402
import schemalib  # noqa: E402


def _resource(label="lmx.test.tex", fmt="BGRA8Unorm", width=4, height=4):
    return schemalib.Resource(label=label, kind="texture2d", format=fmt, width=width,
                              height=height, mip_levels=1, size_bytes=None)


def _header_bytes(pixel_format: int, width: int, height: int, bytes_per_row: int,
                  bytes_per_image: int) -> bytes:
    header = bundlelib.BLOB_HEADER_MAGIC
    header += struct.pack("<II", bundlelib.EXPECTED_HEADER_VERSION, bundlelib.BLOB_HEADER_SIZE)
    header += struct.pack("<QQQQQQQ", 1, pixel_format, width, height, 1, bytes_per_row,
                          bytes_per_image)
    header += b"\x00" * (bundlelib.BLOB_HEADER_SIZE - len(header))
    return header


def _blob(pixel_format: int, width: int, height: int, payload: bytes,
         bytes_per_row: int | None = None) -> bytes:
    bpr = bytes_per_row if bytes_per_row is not None else width * 4
    return _header_bytes(pixel_format, width, height, bpr, len(payload)) + payload


class DecodeDepth32Tests(unittest.TestCase):
    def test_ramp_stats_min_max_and_normalization(self):
        values = [i / 15.0 for i in range(16)]  # 4x4 ramp, exactly 0.0 .. 1.0
        payload = b"".join(struct.pack("<f", v) for v in values)
        blob_bytes = _blob(252, 4, 4, payload)  # 252 == Depth32Float

        decoded = decodelib.decode_texture(_resource(fmt="Depth32Float"), blob_bytes)

        self.assertEqual(decoded.width, 4)
        self.assertEqual(decoded.height, 4)
        self.assertEqual(decoded.stats["min"], 0.0)
        self.assertEqual(decoded.stats["max"], 1.0)
        self.assertAlmostEqual(decoded.stats["mean"], sum(values) / 16)
        self.assertFalse(decoded.stats["flat"])
        # min texel maps to black, max texel maps to white; grayscale stored as equal R,G,B.
        self.assertEqual(decoded.png_rgb_rows[0:3], bytes([0, 0, 0]))
        self.assertEqual(decoded.png_rgb_rows[-3:], bytes([255, 255, 255]))

    def test_all_clear_plane_reports_fraction_at_clear_1_and_is_flat(self):
        payload = struct.pack("<4f", 1.0, 1.0, 1.0, 1.0)
        blob_bytes = _blob(252, 2, 2, payload)

        decoded = decodelib.decode_texture(_resource(fmt="Depth32Float", width=2, height=2),
                                          blob_bytes)

        self.assertEqual(decoded.stats["fractionAtClear"], 1.0)
        self.assertTrue(decoded.stats["flat"])
        self.assertEqual(decoded.stats["min"], 1.0)
        self.assertEqual(decoded.stats["max"], 1.0)

    def test_no_clear_texels_reports_fraction_at_clear_0(self):
        payload = struct.pack("<4f", 0.1, 0.2, 0.3, 0.4)
        blob_bytes = _blob(252, 2, 2, payload)

        decoded = decodelib.decode_texture(_resource(fmt="Depth32Float", width=2, height=2),
                                          blob_bytes)

        self.assertEqual(decoded.stats["fractionAtClear"], 0.0)

    def test_bytes_per_row_padding_is_honored_not_width_times_bpp(self):
        # 3 texels wide (12 tight bytes) but each row is padded to 16 bytes; the padding bytes
        # are garbage that a stride-respecting decoder must never interpret as pixel data.
        #
        # min/max alone do not discriminate a stride bug here -- a decoder that
        # wrongly reads at the tight width*4=12 stride instead of the header's 16 would consume
        # the row-0 padding's 0xFFFFFFFF (NaN, silently dropped by the finite-only filter) as the
        # first value of "row 1", shifting everything by 4 bytes; the *set* of finite values
        # collected is still {0.0, 0.25, 0.5, 0.75, 1.0} for the buggy stride (5 elements, mean
        # 0.5) vs {0.0, 0.25, 0.5, 0.75, 1.0, 0.1} for the correct one (6 elements, mean
        # 0.43333...) -- min/max/width/height/output-length are identical either way, so the mean
        # (and thus which values were actually read) is the only assertion that catches the bug.
        rows = []
        for row_values in ([0.0, 0.25, 0.5], [0.75, 1.0, 0.1]):
            row = b"".join(struct.pack("<f", v) for v in row_values)
            row += b"\xff" * (16 - len(row))
            rows.append(row)
        payload = b"".join(rows)
        blob_bytes = _blob(252, 3, 2, payload, bytes_per_row=16)

        decoded = decodelib.decode_texture(_resource(fmt="Depth32Float", width=3, height=2),
                                          blob_bytes)

        self.assertEqual(decoded.width, 3)
        self.assertEqual(decoded.height, 2)
        self.assertAlmostEqual(decoded.stats["min"], 0.0)
        self.assertAlmostEqual(decoded.stats["max"], 1.0)
        self.assertAlmostEqual(decoded.stats["mean"], (0.0 + 0.25 + 0.5 + 0.75 + 1.0 + 0.1) / 6)
        self.assertEqual(len(decoded.png_rgb_rows), 3 * 2 * 3)

    def test_all_nan_plane_does_not_crash_and_reports_null_stats(self):
        # lmx.render.sceneDepth (renderTarget-only, never sampled) can appear as a uniform
        # 0xFFFFFFFF plane -- bit-for-bit NaN as float32,
        # presumably because Xcode's GPU capture can't resolve a texture with no ShaderRead usage.
        # NaN poisons plain min()/max()/sum() silently (NaN != NaN, so "flat" must not be a float
        # equality check either) and round(nan) raises -- must not crash, and must not fabricate a
        # min/max/mean that doesn't exist.
        payload = struct.pack("<4f", float("nan"), float("nan"), float("nan"), float("nan"))
        blob_bytes = _blob(252, 2, 2, payload)

        decoded = decodelib.decode_texture(_resource(fmt="Depth32Float", width=2, height=2),
                                          blob_bytes)

        self.assertIsNone(decoded.stats["min"])
        self.assertIsNone(decoded.stats["max"])
        self.assertIsNone(decoded.stats["mean"])
        self.assertEqual(decoded.stats["fractionAtClear"], 0.0)
        self.assertTrue(decoded.stats["flat"])
        self.assertEqual(decoded.png_rgb_rows, bytes(2 * 2 * 3))  # plain black, no fake contrast

    def test_partially_nan_plane_summarizes_only_finite_texels(self):
        payload = struct.pack("<4f", float("nan"), 0.0, 1.0, float("nan"))
        blob_bytes = _blob(252, 2, 2, payload)

        decoded = decodelib.decode_texture(_resource(fmt="Depth32Float", width=2, height=2),
                                          blob_bytes)

        self.assertEqual(decoded.stats["min"], 0.0)
        self.assertEqual(decoded.stats["max"], 1.0)
        self.assertAlmostEqual(decoded.stats["mean"], 0.5)
        self.assertEqual(decoded.stats["fractionAtClear"], 0.25)  # 1 of 4 texels, incl. the NaNs
        self.assertFalse(decoded.stats["flat"])
        # The two NaN texels render as plain black rather than joining the normalized gradient.
        self.assertEqual(decoded.png_rgb_rows[0:3], bytes([0, 0, 0]))


class DecodeColorTests(unittest.TestCase):
    def test_bgra_swizzle_single_texel(self):
        payload = bytes([10, 20, 30, 40])  # memory order B,G,R,A
        blob_bytes = _blob(80, 1, 1, payload)  # 80 == BGRA8Unorm

        decoded = decodelib.decode_texture(_resource(fmt="BGRA8Unorm", width=1, height=1),
                                          blob_bytes)

        self.assertEqual(decoded.png_rgb_rows, bytes([30, 20, 10]))  # R,G,B

    def test_rgba_swizzle_single_texel(self):
        payload = bytes([10, 20, 30, 40])  # memory order R,G,B,A
        blob_bytes = _blob(70, 1, 1, payload)  # 70 == RGBA8Unorm

        decoded = decodelib.decode_texture(_resource(fmt="RGBA8Unorm", width=1, height=1),
                                          blob_bytes)

        self.assertEqual(decoded.png_rgb_rows, bytes([10, 20, 30]))  # unchanged R,G,B

    def test_bgra_srgb_uses_the_same_byte_order_as_bgra(self):
        payload = bytes([10, 20, 30, 40])
        blob_bytes = _blob(81, 1, 1, payload)  # 81 == BGRA8Unorm_sRGB

        decoded = decodelib.decode_texture(_resource(fmt="BGRA8Unorm_sRGB", width=1, height=1),
                                          blob_bytes)

        self.assertEqual(decoded.png_rgb_rows, bytes([30, 20, 10]))

    def test_flat_true_when_every_texel_is_identical(self):
        payload = bytes([50, 60, 70, 255]) * 4  # 2x2, same BGRA texel everywhere
        blob_bytes = _blob(80, 2, 2, payload)

        decoded = decodelib.decode_texture(_resource(fmt="BGRA8Unorm", width=2, height=2),
                                          blob_bytes)

        self.assertTrue(decoded.stats["flat"])
        self.assertEqual(decoded.stats["r"], {"min": 70, "max": 70, "mean": 70.0})
        self.assertEqual(decoded.stats["g"], {"min": 60, "max": 60, "mean": 60.0})
        self.assertEqual(decoded.stats["b"], {"min": 50, "max": 50, "mean": 50.0})

    def test_flat_false_when_texels_differ(self):
        payload = (bytes([50, 60, 70, 255]) + bytes([10, 20, 30, 255])
                  + bytes([50, 60, 70, 255]) * 2)
        blob_bytes = _blob(80, 2, 2, payload)

        decoded = decodelib.decode_texture(_resource(fmt="BGRA8Unorm", width=2, height=2),
                                          blob_bytes)

        self.assertFalse(decoded.stats["flat"])

    def test_bytes_per_row_padding_is_honored(self):
        # 3 texels wide (12 tight bytes), row padded to 16 bytes.
        row0 = bytes([1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255]) + b"\xff" * 4
        row1 = bytes([9, 8, 7, 255, 6, 5, 4, 255, 3, 2, 1, 255]) + b"\xff" * 4
        payload = row0 + row1
        blob_bytes = _blob(70, 3, 2, payload, bytes_per_row=16)  # 70 == RGBA8Unorm

        decoded = decodelib.decode_texture(_resource(fmt="RGBA8Unorm", width=3, height=2),
                                          blob_bytes)

        self.assertEqual(len(decoded.png_rgb_rows), 3 * 2 * 3)
        self.assertEqual(decoded.png_rgb_rows[0:3], bytes([1, 2, 3]))
        self.assertEqual(decoded.png_rgb_rows[9:12], bytes([9, 8, 7]))  # row 1, first texel


class DecodeErrorTests(unittest.TestCase):
    def test_bc1_is_undecodable_with_a_scope_reason_not_a_broken_manifest(self):
        blob_bytes = _blob(131, 4, 4, b"\x00" * 8)  # 131 == BC1_RGBA_sRGB

        with self.assertRaises(decodelib.DecodeError) as ctx:
            decodelib.decode_texture(_resource(fmt="BC1_RGBA_sRGB"), blob_bytes)

        self.assertIn("out of scope", str(ctx.exception))

    def test_unknown_pixel_format_numeric_is_named_in_the_reason(self):
        blob_bytes = _blob(9999, 1, 1, b"\x00" * 4)

        with self.assertRaises(decodelib.DecodeError) as ctx:
            decodelib.decode_texture(_resource(fmt="Unknown"), blob_bytes)

        self.assertIn("9999", str(ctx.exception))

    def test_missing_header_raises_decode_error(self):
        with self.assertRaises(decodelib.DecodeError):
            decodelib.decode_texture(_resource(), b"\x00" * 256)


class GeometrySanityCheckTests(unittest.TestCase):
    """bundlelib.join() validates width/height against the schema and that
    headerSize + bytesPerImage fits the blob FILE size, but nothing validates bytesPerRow -- a
    header that passes join() with an internally inconsistent bytesPerRow can raise a raw
    ValueError/ZeroDivisionError or silently produce a partially-black image. Every reachable
    case is regression-tested here and raises DecodeError."""

    def test_bytes_per_row_smaller_than_tight_row_raises_for_color(self):
        # This reaches the slice-assignment guard in _decode_color8. bytesPerRow(8)
        # * height(2) == len(payload), so the payload-length check is satisfied and only the
        # tight-row check can fire -- isolating this case from the payload-length case below.
        payload = b"\x00" * 16
        blob_bytes = _blob(80, 4, 2, payload, bytes_per_row=8)  # tight row for width=4 is 16

        with self.assertRaises(decodelib.DecodeError) as ctx:
            decodelib.decode_texture(_resource(fmt="BGRA8Unorm", width=4, height=2), blob_bytes)

        self.assertIn("bytesPerRow", str(ctx.exception))

    def test_payload_shorter_than_bytes_per_row_times_height_raises_for_color(self):
        # bytesPerImage < bytesPerRow*height would produce a partially-black image without a guard.
        payload = b"\x00" * 32  # header claims height=4 needs 64 bytes at bytesPerRow=16
        blob_bytes = _blob(80, 4, 4, payload, bytes_per_row=16)

        with self.assertRaises(decodelib.DecodeError) as ctx:
            decodelib.decode_texture(_resource(fmt="BGRA8Unorm", width=4, height=4), blob_bytes)

        self.assertIn("bytesPerRow", str(ctx.exception))

    def test_zero_height_raises_for_color(self):
        # An empty image would divide by zero while computing per-channel means without a guard.
        blob_bytes = _blob(80, 4, 0, b"")

        with self.assertRaises(decodelib.DecodeError) as ctx:
            decodelib.decode_texture(_resource(fmt="BGRA8Unorm", width=4, height=0), blob_bytes)

        self.assertIn("non-positive dimensions", str(ctx.exception))

    def test_bytes_per_row_smaller_than_tight_row_raises_for_depth(self):
        # A truncated row reaches array.frombytes with a non-multiple-of-four byte count unless
        # geometry validation rejects it first.
        payload = b"\x00" * 16
        blob_bytes = _blob(252, 4, 2, payload, bytes_per_row=8)  # tight row for 4 floats is 16

        with self.assertRaises(decodelib.DecodeError) as ctx:
            decodelib.decode_texture(_resource(fmt="Depth32Float", width=4, height=2), blob_bytes)

        self.assertIn("bytesPerRow", str(ctx.exception))

    def test_payload_shorter_than_bytes_per_row_times_height_raises_for_depth(self):
        # bytesPerImage < bytesPerRow*height: the depth twin of the color case above.
        payload = b"\x00" * 16  # header claims height=4 needs 64 bytes at bytesPerRow=16
        blob_bytes = _blob(252, 4, 4, payload, bytes_per_row=16)

        with self.assertRaises(decodelib.DecodeError) as ctx:
            decodelib.decode_texture(_resource(fmt="Depth32Float", width=4, height=4), blob_bytes)

        self.assertIn("bytesPerRow", str(ctx.exception))


if __name__ == "__main__":
    unittest.main()
