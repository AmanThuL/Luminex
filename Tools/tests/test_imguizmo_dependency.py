"""Keep the gizmo dependency pinned and confined to the editor shell."""
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
PIN = "18cef5e031d8c6973d80284c67f60549fafd78c1"


class ImGuizmoDependencyTests(unittest.TestCase):
    def test_setup_fetches_exact_pin_and_rejects_a_different_checkout(self):
        setup = (ROOT / "xmake/setup.lua").read_text()
        self.assertIn(f'local imguizmo_pin = "{PIN}"', setup)
        self.assertIn('"https://github.com/CedricGuillemet/ImGuizmo.git"', setup)
        self.assertRegex(setup, r'"fetch", "--depth", "1",\s*"origin", imguizmo_pin')
        self.assertRegex(setup, r'local imguizmo_head = .*?"ThirdParty/ImGuizmo",\s*"rev-parse", "HEAD"')
        self.assertIn('assert(imguizmo_head == imguizmo_pin,', setup)

    def test_contract_allows_only_app_to_use_imguizmo(self):
        contract = json.loads((ROOT / "Tools/module_contract.json").read_text())
        self.assertIn("ImGuizmo", contract["thirdPartyTargets"])
        consumers = [name for name, row in contract["units"].items()
                     if "ImGuizmo" in row["thirdParty"]]
        self.assertEqual(consumers, ["app-shell"])
        dependents = [name for name, row in contract["targets"].items()
                      if "ImGuizmo" in row["deps"]]
        self.assertEqual(dependents, ["App"])

    def test_build_compiles_only_gizmo_source_with_shared_imgui(self):
        definitions = (ROOT / "xmake/dependencies.lua").read_text()
        match = re.search(r'target\("ImGuizmo"\)(.*?)(?=\ntarget\(|\Z)', definitions, re.S)
        self.assertIsNotNone(match, "ImGuizmo needs its own static target")
        body = match.group(1)
        self.assertIn('set_kind("static")', body)
        self.assertEqual(re.findall(r'add_files\("([^"]+)"\)', body),
                         ["../ThirdParty/ImGuizmo/src/ImGuizmo.cpp"])
        self.assertIn('add_includedirs("../ThirdParty/ImGuizmo/src", {public = true})', body)
        self.assertIn('add_deps("ImGui")', body)
        consumers = []
        for path in (ROOT / "Source").rglob("xmake.lua"):
            for args in re.findall(r'add_deps\(([^)]+)\)', path.read_text()):
                if '"ImGuizmo"' in args:
                    consumers.append(path.relative_to(ROOT).as_posix())
        self.assertEqual(consumers, ["Source/App/xmake.lua"])


if __name__ == "__main__":
    unittest.main()
