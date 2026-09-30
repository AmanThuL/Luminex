"""Regression contract for generated editor themes."""
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "Tools/Theme/generate_tokens.py"


class ThemeTokensTests(unittest.TestCase):
    def load_generator(self):
        spec = importlib.util.spec_from_file_location("theme_tokens", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_record_semantic_hex_values(self):
        generator = self.load_generator()
        record = (ROOT / "docs/milestones/ux/ux4.md").read_text()
        matches = re.findall(r"`([a-z-]+/[a-z-]+)`[^|]*\| `(#\w{6})` \| `(#\w{6})`", record)
        self.assertEqual(len(matches), 30)
        generated = generator.outputs()
        document = json.loads(generated["Tools/Theme/figma-variables.json"])
        cpp = generated["Source/App/Model/Workspace/EditorThemeTokens.cpp"]
        for role, dark, light in matches:
            for theme, expected in (("dark", dark), ("light", light)):
                with self.subTest(role=role, theme=theme):
                    self.assertEqual(generator.hexs(generator.sem(theme)[role]), expected)
                    self.assertEqual(document["themes"][theme][role]["hex"], expected)
                    rgb = [int(expected[index:index + 2], 16) for index in (1, 3, 5)]
                    self.assertEqual(document["themes"][theme][role]["rgb"], [v / 255 for v in rgb])
                    table = cpp.split(f"k{theme.title()}Palette = {{{{")[1].split("}};")[0]
                    entry = next(line for line in table.splitlines() if line.endswith("// " + generator.cpp_label(role)))
                    channels = [int(v) for v in re.findall(r"(\d+)\.0f / 255\.0f", entry)]
                    self.assertEqual(channels, rgb)

    def test_outline_is_fixed_encoded_blue_in_both_themes(self):
        generator = self.load_generator()
        generated = generator.outputs()
        document = json.loads(generated["Tools/Theme/figma-variables.json"])
        cpp = generated["Source/App/Model/Workspace/EditorThemeTokens.cpp"]
        expected = [76, 171, 253]
        for theme in ("dark", "light"):
            with self.subTest(theme=theme):
                self.assertEqual(list(generator.palette(theme)["overlay/outline"]), [v / 255 for v in expected])
                outline = document["themes"][theme]["overlay/outline"]
                self.assertEqual(outline["hex"], "#4CABFD")
                self.assertEqual(outline["rgb"], [v / 255 for v in expected])
                table = cpp.split(f"k{theme.title()}Palette = {{{{")[1].split("}};")[0]
                entry = next(line for line in table.splitlines() if line.endswith("// overlay/outline"))
                self.assertEqual([int(v) for v in re.findall(r"(\d+)\.0f / 255\.0f", entry)], expected)
        self.assertEqual(document["themes"]["light"]["accent/operator"]["hex"], "#056FB8")

    def test_slot_lists_complete_unique_and_in_upstream_order(self):
        generator = self.load_generator()
        for slots, count, path, prefix, end in (
            (generator.IMGUI_SLOTS, 63, "ThirdParty/imgui/imgui.h", "ImGuiCol_", "COUNT"),
            (generator.NODE_EDITOR_SLOTS, 19, "ThirdParty/imgui-node-editor/imgui_node_editor.h", "StyleColor_", "Count"),
        ):
            names = [slot[0] for slot in slots]
            self.assertEqual(len(names), count)
            self.assertEqual(len(set(names)), count)
            upstream = ROOT / path
            if upstream.exists():
                text = upstream.read_text().split(prefix + end)[0]
                expected = re.findall(r"^    " + prefix + r"(\w+),", text, re.M)
                if prefix == "StyleColor_":
                    implementation = (upstream.parent / "imgui_node_editor.cpp").read_text()
                    display_names = dict(re.findall(
                        r'case StyleColor_(\w+): return "([^"]+)";', implementation))
                    expected = [display_names[name] for name in expected]
                self.assertEqual(names, expected)
            for _, role, alpha in slots:
                self.assertIn(role, generator.sem("dark"))
                self.assertTrue(0 <= alpha <= 1)

    def test_check_names_tampered_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Tools/Theme").mkdir(parents=True)
            script = root / "Tools/Theme/generate_tokens.py"
            shutil.copyfile(SCRIPT, script)
            subprocess.run([sys.executable, str(script)], check=True, capture_output=True)
            result = subprocess.run([sys.executable, str(script), "--check"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for relative in ("Tools/Theme/figma-variables.json", "Source/App/Model/Workspace/EditorThemeTokens.h", "Source/App/Model/Workspace/EditorThemeTokens.cpp"):
                path = root / relative
                original = path.read_text()
                path.write_text(original + "tampered")
                result = subprocess.run([sys.executable, str(script), "--check"], capture_output=True, text=True)
                self.assertEqual(result.returncode, 1)
                self.assertIn(relative, result.stdout + result.stderr)
                self.assertEqual(path.read_text(), original + "tampered")
                path.write_text(original)

    @unittest.skipUnless(shutil.which("clang-format"), "clang-format unavailable on this host")
    def test_generated_cpp_is_clang_format_stable(self):
        generator = self.load_generator()
        for relative, content in generator.outputs().items():
            if relative.endswith((".h", ".cpp")):
                formatted = subprocess.run(["clang-format", "--assume-filename=" + str(ROOT / relative)], input=content, text=True, capture_output=True, check=True).stdout
                self.assertEqual(content, formatted, relative)

    def test_audit_all_48_and_encoded_selection(self):
        generator = self.load_generator()
        for theme in ("dark", "light"):
            colors = generator.palette(theme)
            rows = generator.audit(theme, colors)
            self.assertEqual(len(rows), 24)
            self.assertTrue(all(row[3] for row in rows), rows)
            def luminance(rgb):
                linear = [v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4 for v in rgb]
                return sum(v * weight for v, weight in zip(linear, (0.2126, 0.7152, 0.0722)))
            for row, pair in zip(rows, generator.contrast_pairs(theme)):
                _, fg, bg, underlay, minimum = pair
                background = colors[bg]
                if underlay:
                    coverage = 0.28 if theme == "dark" else 0.20
                    background = [v * coverage + u * (1 - coverage) for v, u in zip(background, colors[underlay])]
                a, b = sorted((luminance(colors[fg]), luminance(background)))
                self.assertAlmostEqual(row[1], (b + 0.05) / (a + 0.05))
                self.assertGreaterEqual((b + 0.05) / (a + 0.05), minimum)
            a = 0.28 if theme == "dark" else 0.20
            bg = tuple(f * a + b * (1 - a) for f, b in zip(colors["selection/bg"], colors["surface/panel"]))
            self.assertAlmostEqual(rows[22][1], generator.contrast(colors["text/primary"], bg))


if __name__ == "__main__":
    unittest.main()
