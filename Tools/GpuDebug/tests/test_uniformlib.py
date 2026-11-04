"""Tests for uniformlib.py: frame roots decoded by name and GPU address from captured buffers.

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

PAGE_ADDRESS = 0x10016030000
PAGE_SIZE = 4096
LIGHTS_ADDRESS = 0x1000003F000


def _field(name: str, offset: int, type_: str) -> schemalib.UniformField:
    return schemalib.UniformField(name=name, offset_bytes=offset, type=type_)


def _pass_uniforms_struct() -> schemalib.UniformStruct:
    return schemalib.UniformStruct(name="PassUniforms", size_bytes=160, fields=[
        _field("viewProj", 0, "float4x4"),
        _field("shadowTransform", 64, "float4x4"),
        _field("eyePos", 128, "float3"),
        _field("alignmentPadding", 140, "float[2]"),
        _field("shadowFilter", 148, "int"),
        _field("tailPadding", 152, "int[2]"),
    ])


def _pass_root_struct() -> schemalib.UniformStruct:
    return schemalib.UniformStruct(name="ScenePassRoot", size_bytes=40, fields=[
        _field("passUniforms", 0, "Addr"),
        _field("lights", 8, "Addr"),
        _field("exposure", 16, "Addr"),
        _field("stray", 24, "Addr"),
        _field("jitter", 32, "float2"),
    ])


def _pack_mat4_columns(*columns) -> bytes:
    """Four 4-tuples in glm/Slang column-major storage order (column 0's floats first)."""
    return b"".join(struct.pack("<4f", *col) for col in columns)


def _shadow_transform_bytes() -> bytes:
    # diag(0.5, -0.5, 1, 1) with translation column (0.5, 0.5, 0, 1): fitShadowOrtho's texcoord
    # bake, glm m[col][row] indexing (column 3 is the translation column).
    return _pack_mat4_columns((0.5, 0, 0, 0), (0, -0.5, 0, 0), (0, 0, 1, 0), (0.5, 0.5, 0, 1))


def _pass_uniforms_bytes() -> bytes:
    data = bytearray(160)
    data[0:64] = _pack_mat4_columns((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1))
    data[64:128] = _shadow_transform_bytes()
    data[128:140] = struct.pack("<3f", 1.5, 2.5, 3.5)
    data[148:152] = struct.pack("<i", 1)
    data[152:160] = struct.pack("<2i", -1, 7)
    return bytes(data)


class DecodeRootsTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmp = pathlib.Path(self._tmp.name)
        self.page = bytearray(PAGE_SIZE)

    def _buffers(self, page_blob: bool = True) -> list:
        blob = None
        if page_blob:
            path = self.tmp / "MTLBuffer-7-0"
            path.write_bytes(bytes(self.page))
            blob = bundlelib.Blob(path=path, size_bytes=PAGE_SIZE)
        return [
            bundlelib.BufferRecord(gpu_address=LIGHTS_ADDRESS, length=1024,
                                   label="lmx.scene.lights.0", blob=None),
            bundlelib.BufferRecord(gpu_address=PAGE_ADDRESS, length=PAGE_SIZE,
                                   label="rojorhi.frameArena.page", blob=blob),
        ]

    def _schema(self, roots, structs=None) -> schemalib.Schema:
        return schemalib.Schema(
            context={}, resources=[],
            uniform_structs=structs if structs is not None else [_pass_uniforms_struct(),
                                                                 _pass_root_struct()],
            frame_roots=[schemalib.FrameRoot(name=n, address=a) for n, a in roots])

    def test_decodes_a_root_at_its_offset_in_the_buffer_holding_its_address(self):
        self.page[512:672] = _pass_uniforms_bytes()

        result = uniformlib.decode_roots(
            self._schema([("PassUniforms", PAGE_ADDRESS + 512)]), self._buffers())

        self.assertEqual(result.undecoded, [])
        (root,) = result.decoded
        self.assertEqual((root.index, root.name), (0, "PassUniforms"))
        self.assertEqual((root.buffer_label, root.buffer_offset), ("rojorhi.frameArena.page", 512))
        self.assertEqual(root.size_bytes, 160)
        self.assertEqual(root.values["eyePos"], [1.5, 2.5, 3.5])
        self.assertEqual(root.values["alignmentPadding"], [0.0, 0.0])
        self.assertEqual(root.values["shadowFilter"], 1)
        self.assertEqual(root.values["tailPadding"], [-1, 7])
        self.assertTrue(root.finite)
        self.assertEqual(result.attribution, "1 of 1 frame roots decoded from captured buffers")

    def test_matrices_are_reshaped_row_major_for_display(self):
        self.page[0:160] = _pass_uniforms_bytes()

        (root,) = uniformlib.decode_roots(
            self._schema([("PassUniforms", PAGE_ADDRESS)]), self._buffers()).decoded

        # Storage column 3 (translation 0.5, 0.5, 0, 1) is display column 3, row by row.
        self.assertEqual(root.values["shadowTransform"], [
            [0.5, 0.0, 0.0, 0.5],
            [0.0, -0.5, 0.0, 0.5],
            [0.0, 0.0, 1.0, 0.0],
            [0.0, 0.0, 0.0, 1.0]])

    def test_addr_fields_follow_one_level_without_raw_addresses(self):
        uniforms_address = PAGE_ADDRESS + 256
        self.page[256:416] = _pass_uniforms_bytes()
        self.page[0:40] = struct.pack("<QQQQ2f", uniforms_address, LIGHTS_ADDRESS + 64, 0,
                                      0x9_0000_0000, 0.25, -0.25)

        result = uniformlib.decode_roots(
            self._schema([("ScenePassRoot", PAGE_ADDRESS), ("PassUniforms", uniforms_address)]),
            self._buffers())

        root = result.decoded[0]
        self.assertEqual(root.values["passUniforms"], {"root": "PassUniforms"})
        self.assertEqual(root.values["lights"], {"buffer": "lmx.scene.lights.0", "offset": 64})
        self.assertEqual(root.values["exposure"], 0)
        self.assertEqual(root.values["stray"], {"unresolved": "0x900000000"})
        self.assertEqual(root.values["jitter"], [0.25, -0.25])

    def test_same_size_layouts_decode_by_name_and_repeated_names_each_decode(self):
        """Layouts are chosen by root name, never by size: two 8-byte structs with different field
        types each decode with their own layout, and one name registered at two addresses (two
        draws of one pass) yields two roots in registration order."""
        ints = schemalib.UniformStruct(name="Ints", size_bytes=8,
                                       fields=[_field("a", 0, "uint"), _field("b", 4, "int")])
        floats = schemalib.UniformStruct(name="Floats", size_bytes=8,
                                         fields=[_field("v", 0, "float2")])
        self.page[0:8] = struct.pack("<Ii", 7, -3)
        self.page[256:264] = struct.pack("<2f", 0.5, 2.0)
        self.page[512:520] = struct.pack("<Ii", 9, 4)

        result = uniformlib.decode_roots(
            self._schema([("Ints", PAGE_ADDRESS), ("Floats", PAGE_ADDRESS + 256),
                          ("Ints", PAGE_ADDRESS + 512)], structs=[ints, floats]),
            self._buffers())

        self.assertEqual([(r.index, r.name, r.values) for r in result.decoded],
                         [(0, "Ints", {"a": 7, "b": -3}),
                          (1, "Floats", {"v": [0.5, 2.0]}),
                          (2, "Ints", {"a": 9, "b": 4})])

    def test_nan_field_marks_the_root_not_finite_and_renders_as_none(self):
        data = bytearray(_pass_uniforms_bytes())
        data[128:132] = struct.pack("<f", float("nan"))
        self.page[0:160] = data

        (root,) = uniformlib.decode_roots(
            self._schema([("PassUniforms", PAGE_ADDRESS)]), self._buffers()).decoded

        self.assertFalse(root.finite)
        self.assertEqual(root.values["eyePos"], [None, 2.5, 3.5])

    def test_root_without_a_layout_decodes_as_raw_hex(self):
        self.page[64:72] = b"\x01\x02\x03\x04\x05\x06\x07\x08"

        (root,) = uniformlib.decode_roots(
            self._schema([("Mystery", PAGE_ADDRESS + 64)]), self._buffers()).decoded

        self.assertIsNone(root.size_bytes)
        self.assertTrue(root.values["rawHex"].startswith("0102030405060708"))
        self.assertEqual(len(root.values["rawHex"]), 2 * 64)

    def test_undecodable_roots_carry_reasons_and_the_rest_still_decode(self):
        self.page[0:160] = _pass_uniforms_bytes()
        bad_type = schemalib.UniformStruct(name="Odd", size_bytes=4,
                                           fields=[_field("x", 0, "half")])
        schema = self._schema(
            [("PassUniforms", PAGE_ADDRESS), ("PassUniforms", 0),
             ("PassUniforms", 0x9_0000_0000), ("PassUniforms", PAGE_ADDRESS + PAGE_SIZE - 16),
             ("PassUniforms", LIGHTS_ADDRESS), ("Odd", PAGE_ADDRESS + 1024)],
            structs=[_pass_uniforms_struct(), bad_type])

        result = uniformlib.decode_roots(schema, self._buffers())

        self.assertEqual([r.index for r in result.decoded], [0])
        reasons = {r.index: r.reason for r in result.undecoded}
        self.assertIn("zero address", reasons[1])
        self.assertIn("no single captured buffer", reasons[2])
        self.assertIn("runs past", reasons[3])
        self.assertIn("dumped no contents", reasons[4])
        self.assertIn("'half'", reasons[5])
        self.assertTrue(result.attribution.startswith("1 of 6 frame roots decoded"))
        self.assertIn("root 1 PassUniforms", result.attribution)

    def test_no_roots_is_unremarkable(self):
        result = uniformlib.decode_roots(self._schema([]), self._buffers(page_blob=False))

        self.assertEqual((result.decoded, result.undecoded), ([], []))
        self.assertEqual(result.attribution, "no frame roots recorded in this capture")


if __name__ == "__main__":
    unittest.main()
