from __future__ import annotations

import json
import struct
import tempfile
import unittest
from pathlib import Path

from Tools.convert_obj_to_gltf import convert
from Tools.tree_digest import tree_digest


_MTL = """\
newmtl stone
Kd 0.25 0.5 0.75
Ns 64
map_Kd textures/stone.png
"""

_OBJ = """\
mtllib scene.mtl
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
vt 0.0 0.25
vt 1.0 0.25
vt 1.0 0.75
vt 0.0 0.75
vn 0 0 1
usemtl stone
f 1/1/1 2/2/1 3/3/1 4/4/1
"""


class ObjToGltfTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "textures").mkdir()
        (self.root / "scene.obj").write_text(_OBJ, encoding="utf-8")
        (self.root / "scene.mtl").write_text(_MTL, encoding="utf-8")
        (self.root / "textures" / "stone.png").write_bytes(b"fixture-png")

    def convert_to(self, name: str) -> Path:
        output = self.root / name
        convert(self.root / "scene.obj", self.root / "scene.mtl", output, scale=1.0)
        return output

    def test_quad_triangulation_uv_flip_and_material_texture(self) -> None:
        output = self.convert_to("output")
        gltf = json.loads((output / "Sponza.gltf").read_text(encoding="utf-8"))
        primitive = gltf["meshes"][0]["primitives"][0]

        index_accessor = gltf["accessors"][primitive["indices"]]
        self.assertEqual(index_accessor["count"], 6)
        self.assertEqual(gltf["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"],
                         [0.25, 0.5, 0.75, 1.0])
        self.assertEqual(gltf["images"], [{"uri": "textures/stone.png"}])
        self.assertEqual((output / "textures" / "stone.png").read_bytes(), b"fixture-png")

        uv_accessor = gltf["accessors"][primitive["attributes"]["TEXCOORD_0"]]
        uv_view = gltf["bufferViews"][uv_accessor["bufferView"]]
        binary = (output / "Sponza.bin").read_bytes()
        self.assertEqual(struct.unpack_from("<2f", binary, uv_view["byteOffset"]), (0.0, 0.75))

    def test_output_is_deterministic(self) -> None:
        first = self.convert_to("first")
        second = self.convert_to("second")
        self.assertEqual(tree_digest(first), tree_digest(second))

    def test_rejects_texture_outside_source_tree(self) -> None:
        (self.root / "scene.mtl").write_text(
            _MTL.replace("textures/stone.png", "../outside.png"), encoding="utf-8"
        )
        with self.assertRaisesRegex(ValueError, "missing texture"):
            self.convert_to("output")

    def test_rejects_out_of_range_obj_index(self) -> None:
        (self.root / "scene.obj").write_text(_OBJ.replace("4/4/1", "5/4/1"), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "position index 5 is out of range"):
            self.convert_to("output")


if __name__ == "__main__":
    unittest.main()
