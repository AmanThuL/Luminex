from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from Tools import bake_gltf_textures as bake


def _write_gltf_with_external_image(root: Path) -> Path:
    (root / "textures").mkdir()
    (root / "textures" / "a.png").write_bytes(b"fixture-bytes")
    gltf = {
        "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
        "textures": [{"source": 0}],
        "images": [{"uri": "textures/a.png"}],
    }
    gltf_path = root / "Scene.gltf"
    gltf_path.write_text(json.dumps(gltf), encoding="utf-8")
    return gltf_path


class BakeGltfTexturesTests(unittest.TestCase):
    def _run_with_recorded_args(self, gltf_path: Path) -> list[list[str]]:
        recorded: list[list[str]] = []

        def fake_run(args: list[str], check: bool) -> None:
            recorded.append(args)
            # bake_gltf_textures never reads the .dds itself, so a stub is enough to let the
            # manifest_is_current check on a hypothetical rerun see a real (if empty) file.
            Path(args[2]).write_bytes(b"")

        with mock.patch.object(bake.subprocess, "run", side_effect=fake_run):
            baked, skipped = bake.bake_gltf_textures(gltf_path, "fake-texture-bake")
        self.assertEqual((baked, skipped), (1, 0))
        return recorded

    def test_external_image_source_name_is_the_relative_uri_even_from_an_absolute_gltf_path(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            gltf_path = _write_gltf_with_external_image(root)
            self.assertTrue(gltf_path.is_absolute(), "tempfile paths are always absolute")

            args = self._run_with_recorded_args(gltf_path)[0]

            self.assertIn("--source-name", args)
            source_name = args[args.index("--source-name") + 1]
            self.assertEqual(source_name, "textures/a.png")
            # The regression this pins: source-name must never carry the machine-specific
            # temp-directory prefix that in_path (args[1]) legitimately does.
            self.assertNotIn(str(root), source_name)
            self.assertNotEqual(source_name, args[1])

    def test_embedded_image_source_name_is_glb_name_hash_image_name_not_the_temp_path(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            glb_path = root / "Model.glb"
            # bake_gltf_textures only reads glb_path.suffix to pick the GLB branch and passes the
            # rest through image_roles/encoded_image_bytes; stub read_glb so this test does not
            # need a real binary GLB container.
            gltf = {
                "materials": [{"normalTexture": {"index": 0}}],
                "textures": [{"source": 0}],
                "images": [{"bufferView": 0, "mimeType": "image/jpeg"}],
                "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 4}],
                "buffers": [{"byteLength": 4}],
            }
            glb_path.write_bytes(b"not a real glb; read_glb is mocked below")

            args_holder: list[list[str]] = []

            def fake_run(args: list[str], check: bool) -> None:
                args_holder.append(args)
                Path(args[2]).write_bytes(b"")

            with (
                mock.patch.object(bake, "read_glb", return_value=(gltf, b"\x01\x02\x03\x04")),
                mock.patch.object(bake.subprocess, "run", side_effect=fake_run),
            ):
                baked, skipped = bake.bake_gltf_textures(glb_path, "fake-texture-bake")
            self.assertEqual((baked, skipped), (1, 0))

            args = args_holder[0]
            self.assertIn("--source-name", args)
            source_name = args[args.index("--source-name") + 1]
            self.assertEqual(source_name, "Model.glb#image0.jpg")
            self.assertNotIn(str(root), source_name)
            self.assertNotEqual(source_name, args[1])

    def test_manifest_is_current_requires_source_filter_and_tool_version(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dds = root / "image0.dds"
            manifest = root / "image0.dds.json"
            dds.write_bytes(b"dds")
            manifest.write_text(
                json.dumps(
                    {
                        "sourceSha256": "abc",
                        "filter": "box-linear",
                        "toolVersion": bake.TEXTURE_BAKE_TOOL_VERSION,
                    }
                ),
                encoding="utf-8",
            )

            self.assertTrue(bake.manifest_is_current(manifest, dds, "abc", "box-linear"))
            self.assertFalse(bake.manifest_is_current(manifest, dds, "different", "box-linear"))
            self.assertFalse(bake.manifest_is_current(manifest, dds, "abc", "box-normal"))
            self.assertFalse(
                bake.manifest_is_current(manifest, dds, "abc", "box-linear", "new-version")
            )


if __name__ == "__main__":
    unittest.main()
