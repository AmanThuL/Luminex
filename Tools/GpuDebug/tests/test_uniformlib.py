"""Tests for uniformlib.py's schema-driven decode and ring-blob attribution policy.

Buffer blobs are not label-joinable, so `bundlelib.join()` refuses to pick among same-size ring
candidates; `uniformlib.resolve_ring_bytes` is the explicitly untrusted positional fallback.

Run: python3 -m unittest discover -s Tools/GpuDebug/tests -v
"""
import pathlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

import bundlelib  # noqa: E402
import schemalib  # noqa: E402
import uniformlib  # noqa: E402

from test_bundlelib import BundleFixture  # noqa: E402


# Fixture structs and bytes mirror the renderer's uniform ABI.

def _pass_uniforms_struct() -> schemalib.UniformStruct:
    return schemalib.UniformStruct(
        name="PassUniforms", slot=2, size_bytes=288,
        fields=[
            schemalib.UniformField(name="viewProj", offset_bytes=0, type="float4x4"),
            schemalib.UniformField(name="shadowTransform", offset_bytes=64, type="float4x4"),
            schemalib.UniformField(name="eyePos", offset_bytes=128, type="float3"),
            schemalib.UniformField(name="shadowFilter", offset_bytes=272, type="int"),
        ])


def _sky_uniforms_struct() -> schemalib.UniformStruct:
    return schemalib.UniformStruct(
        name="SkyUniforms", slot=2, size_bytes=80,
        fields=[
            schemalib.UniformField(name="viewProj", offset_bytes=0, type="float4x4"),
            schemalib.UniformField(name="eyePos", offset_bytes=64, type="float3"),
        ])


def _pack_mat4_columns(*columns) -> bytes:
    """`columns` are four 4-tuples, packed in glm/Slang column-major storage order (column 0's
    four floats first, matching offsetof-based field layout in Renderer.cpp)."""
    return b"".join(struct.pack("<4f", *col) for col in columns)


def _identity_mat4_bytes() -> bytes:
    return _pack_mat4_columns((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1))


def _shadow_transform_bytes() -> bytes:
    # diag(0.5, -0.5, 1, 1) with translation column (0.5, 0.5, 0, 1) -- Renderer.cpp's
    # fitShadowOrtho texcoord bake comment: "[0][0]=0.5, [1][1]=-0.5, [3][0]=0.5, [3][1]=0.5"
    # (glm m[col][row] indexing -- column 3 is the translation column).
    return _pack_mat4_columns((0.5, 0, 0, 0), (0, -0.5, 0, 0), (0, 0, 1, 0), (0.5, 0.5, 0, 1))


def _build_pass_uniforms_bytes(shadow_filter: int = 0) -> bytes:
    data = bytearray(288)
    data[0:64] = _identity_mat4_bytes()
    data[64:128] = _shadow_transform_bytes()
    data[128:140] = struct.pack("<3f", 1.0, 2.0, 3.0)  # eyePos
    data[272:276] = struct.pack("<i", shadow_filter)
    return bytes(data)


def _build_sky_uniforms_bytes() -> bytes:
    data = bytearray(80)
    data[0:64] = _identity_mat4_bytes()
    data[64:76] = struct.pack("<3f", 5.0, 6.0, 7.0)  # eyePos
    return bytes(data)


def _build_ring(size: int = 4096) -> bytearray:
    """A 4 KiB ring with a full PassUniforms upload at offset 512 and a SkyUniforms upload at
    offset 1024."""
    ring = bytearray(size)
    ring[512:512 + 288] = _build_pass_uniforms_bytes()
    ring[1024:1024 + 80] = _build_sky_uniforms_bytes()
    return ring


def _schema_with(uniform_structs, uniform_uploads, resources=None) -> schemalib.Schema:
    return schemalib.Schema(context={}, resources=resources or [],
                            uniform_structs=uniform_structs, uniform_uploads=uniform_uploads)


class DecodeUploadsTests(unittest.TestCase):
    def setUp(self):
        self.ring = bytes(_build_ring())
        self.ring_bytes_by_label = {"lmx.device.uniformRing.0": self.ring}

    def test_resolves_struct_names_by_slot_and_size(self):
        uploads = [
            schemalib.UniformUpload(ring_label="lmx.device.uniformRing.0", slot=2,
                                    ring_offset=512, size_bytes=288),
            schemalib.UniformUpload(ring_label="lmx.device.uniformRing.0", slot=2,
                                    ring_offset=1024, size_bytes=80),
        ]
        schema = _schema_with([_pass_uniforms_struct(), _sky_uniforms_struct()], uploads)

        decoded = uniformlib.decode_uploads(schema, self.ring_bytes_by_label)

        self.assertEqual([u.struct_name for u in decoded], ["PassUniforms", "SkyUniforms"])
        self.assertEqual([u.index for u in decoded], [0, 1])

    def test_shadow_transform_is_transposed_to_row_major_for_display(self):
        # Column-major storage means the raw 16 floats group into COLUMNS, not rows: naively
        # reading them as 4 rows of 4 would put the translation column (0.5, 0.5, 0, 1) at
        # display row 3, which looks plausible but is wrong. The correct transpose leaves row 3
        # as the matrix's real bottom row ([0,0,0,1], standard for an affine matrix) and places
        # the translation values in column 3 of rows 0 and 1 instead.
        uploads = [schemalib.UniformUpload(ring_label="lmx.device.uniformRing.0", slot=2,
                                           ring_offset=512, size_bytes=288)]
        schema = _schema_with([_pass_uniforms_struct()], uploads)

        decoded = uniformlib.decode_uploads(schema, self.ring_bytes_by_label)

        shadow = decoded[0].values["shadowTransform"]
        self.assertEqual(shadow[0][0], 0.5)
        self.assertEqual(shadow[1][1], -0.5)
        self.assertEqual(shadow[0][3], 0.5)
        self.assertEqual(shadow[1][3], 0.5)
        self.assertEqual(shadow[2][3], 0.0)
        self.assertEqual(shadow[3], [0.0, 0.0, 0.0, 1.0])

    def test_view_proj_identity_round_trips(self):
        uploads = [schemalib.UniformUpload(ring_label="lmx.device.uniformRing.0", slot=2,
                                           ring_offset=512, size_bytes=288)]
        schema = _schema_with([_pass_uniforms_struct()], uploads)

        decoded = uniformlib.decode_uploads(schema, self.ring_bytes_by_label)

        self.assertEqual(decoded[0].values["viewProj"],
                         [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                          [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]])

    def test_eye_pos_and_shadow_filter_and_upload_metadata(self):
        uploads = [schemalib.UniformUpload(ring_label="lmx.device.uniformRing.0", slot=2,
                                           ring_offset=512, size_bytes=288)]
        schema = _schema_with([_pass_uniforms_struct()], uploads)

        decoded = uniformlib.decode_uploads(schema, self.ring_bytes_by_label)

        self.assertEqual(decoded[0].values["eyePos"], [1.0, 2.0, 3.0])
        self.assertEqual(decoded[0].values["shadowFilter"], 0)
        self.assertEqual(decoded[0].slot, 2)
        self.assertEqual(decoded[0].ring_label, "lmx.device.uniformRing.0")
        self.assertEqual(decoded[0].ring_offset, 512)
        self.assertTrue(decoded[0].finite)

    def test_unmatched_slot_size_decodes_as_unknown_with_raw_hex(self):
        uploads = [schemalib.UniformUpload(ring_label="lmx.device.uniformRing.0", slot=9,
                                           ring_offset=512, size_bytes=288)]
        schema = _schema_with([_pass_uniforms_struct()], uploads)  # slot 9 matches no struct

        decoded = uniformlib.decode_uploads(schema, self.ring_bytes_by_label)

        self.assertEqual(decoded[0].struct_name, "unknown")
        self.assertEqual(set(decoded[0].values), {"rawHex"})
        self.assertEqual(decoded[0].values["rawHex"], self.ring[512:512 + 64].hex())

    def test_upload_past_ring_end_raises_uniform_error_naming_label_and_offsets(self):
        uploads = [schemalib.UniformUpload(ring_label="lmx.device.uniformRing.0", slot=2,
                                           ring_offset=4000, size_bytes=288)]
        schema = _schema_with([_pass_uniforms_struct()], uploads)

        with self.assertRaises(uniformlib.UniformError) as ctx:
            uniformlib.decode_uploads(schema, self.ring_bytes_by_label)

        message = str(ctx.exception)
        self.assertIn("lmx.device.uniformRing.0", message)
        self.assertIn("4000", message)

    def test_unresolved_ring_label_raises_uniform_error_not_a_crash(self):
        uploads = [schemalib.UniformUpload(ring_label="lmx.device.uniformRing.9", slot=2,
                                           ring_offset=0, size_bytes=288)]
        schema = _schema_with([_pass_uniforms_struct()], uploads)

        with self.assertRaises(uniformlib.UniformError) as ctx:
            uniformlib.decode_uploads(schema, {})  # nothing attributed to ring.9

        self.assertIn("lmx.device.uniformRing.9", str(ctx.exception))

    def test_nan_field_marks_upload_not_finite_and_renders_as_none(self):
        ring = bytearray(_build_ring())
        # eyePos lives at struct offset 128, and this upload starts at ring offset 512, so its
        # absolute ring position is 512 + 128 = 640.
        ring[640:644] = struct.pack("<f", float("nan"))  # corrupt eyePos.x
        uploads = [schemalib.UniformUpload(ring_label="lmx.device.uniformRing.0", slot=2,
                                           ring_offset=512, size_bytes=288)]
        schema = _schema_with([_pass_uniforms_struct()], uploads)

        decoded = uniformlib.decode_uploads(schema, {"lmx.device.uniformRing.0": bytes(ring)})

        self.assertFalse(decoded[0].finite)
        self.assertIsNone(decoded[0].values["eyePos"][0])
        self.assertEqual(decoded[0].values["eyePos"][1], 2.0)  # untouched sibling stays finite


class ResolveRingBytesTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = pathlib.Path(self._tmp.name) / "test.gputrace"
        self.root.mkdir()

    def _ring_resources(self, size: int = 4096) -> list:
        return [
            schemalib.Resource(label=f"lmx.device.uniformRing.{i}", kind="buffer", format=None,
                              width=None, height=None, mip_levels=None, size_bytes=size)
            for i in range(3)
        ]

    def _upload_for(self, ring_label: str) -> schemalib.UniformUpload:
        return schemalib.UniformUpload(ring_label=ring_label, slot=2, ring_offset=0,
                                       size_bytes=80)

    def test_single_ring_sized_blob_is_attributed_directly(self):
        fx = BundleFixture(self.root)
        fx.write_buffer_blob(20, b"\xab" * 4096)
        fx.add_labelled_resource("lmx.unrelated", None)
        fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)
        schema = _schema_with([], [self._upload_for("lmx.device.uniformRing.1")],
                              resources=self._ring_resources())

        resolution = uniformlib.resolve_ring_bytes(schema, bundle)

        self.assertEqual(resolution.ring_bytes_by_label,
                         {"lmx.device.uniformRing.1": b"\xab" * 4096})
        self.assertIn("single ring-sized blob", resolution.attribution)
        self.assertIn("lmx.device.uniformRing.1", resolution.attribution)

    def test_three_ring_sized_blobs_attributed_positionally_by_creation_id(self):
        # Captured rings use consecutive ids: 13/14/15 <-> uniformRing.0/1/2.
        fx = BundleFixture(self.root)
        fx.write_buffer_blob(13, b"\x00" * 4096)
        fx.write_buffer_blob(14, b"\x11" * 4096)
        fx.write_buffer_blob(15, b"\x22" * 4096)
        fx.add_labelled_resource("lmx.unrelated", None)
        fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)
        schema = _schema_with([], [self._upload_for("lmx.device.uniformRing.2")],
                              resources=self._ring_resources())

        resolution = uniformlib.resolve_ring_bytes(schema, bundle)

        self.assertEqual(resolution.ring_bytes_by_label,
                         {"lmx.device.uniformRing.2": b"\x22" * 4096})
        self.assertIn("positional", resolution.attribution)
        self.assertIn("untrusted heuristic", resolution.attribution)

    def test_ambiguous_candidate_count_declines_to_decode(self):
        # Two same-sized blobs: neither 1 (single-blob route) nor 3 (all-siblings route) fires.
        fx = BundleFixture(self.root)
        fx.write_buffer_blob(1, b"\x00" * 4096)
        fx.write_buffer_blob(2, b"\x00" * 4096)
        fx.add_labelled_resource("lmx.unrelated", None)
        fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)
        schema = _schema_with([], [self._upload_for("lmx.device.uniformRing.0")],
                              resources=self._ring_resources())

        resolution = uniformlib.resolve_ring_bytes(schema, bundle)

        self.assertEqual(resolution.ring_bytes_by_label, {})
        self.assertTrue(resolution.attribution)

    def test_no_ring_sized_blob_declines_to_decode(self):
        fx = BundleFixture(self.root)
        fx.add_labelled_resource("lmx.unrelated", None)
        fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)
        schema = _schema_with([], [self._upload_for("lmx.device.uniformRing.0")],
                              resources=self._ring_resources())

        resolution = uniformlib.resolve_ring_bytes(schema, bundle)

        self.assertEqual(resolution.ring_bytes_by_label, {})
        self.assertTrue(resolution.attribution)

    def test_no_uploads_recorded_short_circuits_without_touching_bundle(self):
        schema = _schema_with([], [], resources=self._ring_resources())

        resolution = uniformlib.resolve_ring_bytes(schema, None)  # bundle unused: never touched

        self.assertEqual(resolution.ring_bytes_by_label, {})
        self.assertIn("no uniform uploads", resolution.attribution)


if __name__ == "__main__":
    unittest.main()
