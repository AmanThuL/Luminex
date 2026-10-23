from __future__ import annotations

import contextlib
import hashlib
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from Tools import check_cpp_layout as layout
from Tools import check_module_deps as modules
from Tools.pending_sources import PendingSourceError, validate_pending_sources


class PendingSourceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name).resolve()
        self.names = ["Benchmarks/Pending.cpp", "Source/Render/Pending.cpp", "Tests/Pending.cpp"]
        self.kept = "Source/Render/Kept.cpp"
        self.kept_tests = "Tests/Kept.cpp"
        for name in [*self.names, self.kept, self.kept_tests]:
            file = self.root / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text("", encoding="utf-8")
        self.port = self.root / "xmake" / "port.lua"
        self.port.parent.mkdir()
        self.port.write_text("return {}\n", encoding="utf-8")
        self.metadata = {
            "version": 1,
            "portSha256": hashlib.sha256(self.port.read_bytes()).hexdigest(),
            "cpp": self.names.copy(),
        }
        self.targets = {
            "Render": {"files": [self.kept], "pending": self.metadata},
            "Tests": {"files": [self.kept_tests]},
        }
        self.contract = {
            "schemaVersion": 1,
            "roots": ["Source", "Tests", "Benchmarks"],
            "budgets": {"production": 1000, "tests": 1500},
            "units": {
                "render": {"paths": ["Source/Render"], "targets": ["Render"], "units": [], "thirdParty": []},
                "tests": {"paths": ["Tests"], "targets": ["Tests"], "units": [], "thirdParty": []},
                "benchmarks": {"paths": ["Benchmarks"], "targets": ["FrameDataBench"], "units": [], "thirdParty": []},
            },
            "targets": {"Render": {"deps": []}, "Tests": {"deps": []}, "FrameDataBench": {"deps": []}},
        }
        contract_path = self.root / "Tools" / "module_contract.json"
        contract_path.parent.mkdir()
        contract_path.write_text(json.dumps(self.contract), encoding="utf-8")
        self.contract = modules.load_contract(contract_path, self.root)
        self.database = self.root / "compile_commands.json"
        self.database.write_text(json.dumps([{
            "file": name, "directory": str(self.root),
            "arguments": ["clang++", "-c", name],
        } for name in (self.kept, self.kept_tests)]), encoding="utf-8")
        self.dump = self.root / "targets.json"

    def validate(self, compiled: list[str | Path] = ()) -> frozenset[str]:
        return validate_pending_sources(
            self.root, self.targets, owner=lambda path: modules.owner_of(path, self.contract),
            compiled=compiled,
        )

    def dump_targets(self) -> None:
        self.dump.write_text(json.dumps(self.targets), encoding="utf-8")

    def test_exact_partition_is_shared_by_module_ownership_and_layout(self) -> None:
        pending = self.validate([self.kept])
        self.assertEqual(pending, frozenset(self.names))
        errors: list[str] = []
        modules.check_ownership(modules.project_files(self.root, self.contract), self.targets,
                                self.contract, [], errors, pending)
        self.assertEqual(errors, [])
        self.dump_targets()
        files, count = layout.files_for_check(self.root, self.database, self.dump)
        self.assertEqual(files, [self.root / self.kept, self.root / self.kept_tests])
        self.assertEqual(count, 3)

    def test_inactive_benchmark_include_roots_are_explicitly_deferred(self) -> None:
        (self.root / "Benchmarks/Pending.h").write_text("", encoding="utf-8")
        files = modules.project_files(self.root, self.contract)
        errors: list[str] = []
        units, deferred = modules.check_includes(
            files, self.targets, modules.load_compile_commands(self.database, self.root),
            self.contract, [], errors, self.root, self.validate(),
        )
        self.assertEqual((units, deferred), (1, 2))
        self.assertEqual(errors, [])

    def test_active_include_cannot_reach_an_inactive_header(self) -> None:
        (self.root / "Benchmarks/Pending.h").write_text("", encoding="utf-8")
        (self.root / self.kept).write_text('#include "../../Benchmarks/Pending.h"\n', encoding="utf-8")
        with self.assertRaisesRegex(modules.ModuleContractError, "active include traversal.*benchmarks"):
            modules.check_includes(
                modules.project_files(self.root, self.contract), self.targets,
                modules.load_compile_commands(self.database, self.root), self.contract, [], [],
                self.root, self.validate(),
            )

    def test_header_only_unit_is_not_vacuously_inactive(self) -> None:
        file = self.root / "Source/HeaderOnly/Value.h"
        file.parent.mkdir()
        file.write_text("", encoding="utf-8")
        self.contract["units"]["header-only"] = {
            "paths": ["Source/HeaderOnly"], "targets": ["HeaderOnly"],
            "units": [], "headers": [], "thirdParty": [],
        }
        files = modules.project_files(self.root, self.contract)
        database = modules.load_compile_commands(self.database, self.root)
        inactive = modules.inactive_include_units(files, self.targets, database,
                                                  self.contract, self.validate())
        self.assertNotIn("header-only", inactive)
        with self.assertRaisesRegex(modules.ModuleContractError, "no compilation context.*Value.h"):
            modules.check_includes(files, self.targets, database, self.contract, [], [],
                                    self.root, self.validate())

    def test_configured_target_or_database_context_prevents_inactive_skip(self) -> None:
        files = modules.project_files(self.root, self.contract)
        database = modules.load_compile_commands(self.database, self.root)
        pending = self.validate()
        self.targets["FrameDataBench"] = {"files": []}
        self.assertNotIn("benchmarks", modules.inactive_include_units(
            files, self.targets, database, self.contract, pending
        ))
        del self.targets["FrameDataBench"]
        database["Benchmarks/Context.h"] = database[self.kept]
        self.assertNotIn("benchmarks", modules.inactive_include_units(
            files, self.targets, database, self.contract, pending
        ))

    def test_private_identity_check_still_covers_deferred_roots(self) -> None:
        private = self.root / "Source/Render/Private.h"
        private.write_text("", encoding="utf-8")
        self.contract["units"]["render"]["privateHeaders"] = ["Source/Render/Private.h"]
        os.link(private, self.root / "Benchmarks/Alias.h")
        errors: list[str] = []
        modules.check_includes(
            modules.project_files(self.root, self.contract), self.targets,
            modules.load_compile_commands(self.database, self.root), self.contract, [], errors,
            self.root, self.validate(),
        )
        self.assertEqual(errors, ["Benchmarks/Alias.h: benchmarks aliases private header Source/Render/Private.h owned by render"])

    def test_unlisted_missing_source_still_fails_both_checks(self) -> None:
        file = self.root / "Source/Render/Unexpected.cpp"
        file.write_text("", encoding="utf-8")
        errors: list[str] = []
        modules.check_ownership(modules.project_files(self.root, self.contract), self.targets,
                                self.contract, [], errors, self.validate())
        self.assertEqual(errors, ["Source/Render/Unexpected.cpp: no target compiles this source"])
        self.dump_targets()
        with self.assertRaisesRegex(layout.LayoutSetupError, "Unexpected.cpp"):
            layout.files_for_check(self.root, self.database, self.dump)

    def test_module_entry_point_uses_the_validated_partition(self) -> None:
        (self.root / "Tools/module_allowlist.json").write_text(
            json.dumps({"schemaVersion": 1, "entries": []}), encoding="utf-8"
        )
        self.dump_targets()
        arguments = ["--root", str(self.root), "--targets", str(self.dump)]
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            self.assertEqual(modules.main(arguments), 0, output.getvalue())
            (self.root / "Source/Render/Unexpected.cpp").write_text("", encoding="utf-8")
            self.assertEqual(modules.main(arguments), 1)
            self.metadata["portSha256"] = "0" * 64
            self.dump_targets()
            self.assertEqual(modules.main(arguments), 2)

    def test_no_port_preserves_full_coverage(self) -> None:
        self.port.unlink()
        del self.targets["Render"]["pending"]
        self.assertEqual(self.validate(), frozenset())
        with patch.object(modules, "load_targets", side_effect=AssertionError("must not load xmake")):
            with self.assertRaisesRegex(layout.LayoutSetupError, "Pending.cpp"):
                layout.files_for_check(self.root, self.database)

    def test_no_port_rejects_stale_metadata_instead_of_exempting_files(self) -> None:
        self.port.unlink()
        with self.assertRaisesRegex(PendingSourceError, "without xmake/port.lua"):
            self.validate()
        self.dump_targets()
        with self.assertRaisesRegex(layout.LayoutSetupError, "without xmake/port.lua"):
            layout.files_for_check(self.root, self.database, self.dump)
        self.targets["Render"]["pending"] = {}
        self.assertEqual(self.validate(), frozenset())

    def test_empty_partition_still_requires_current_metadata(self) -> None:
        self.metadata["cpp"] = []
        self.assertEqual(self.validate(), frozenset())
        self.metadata["portSha256"] = "0" * 64
        with self.assertRaisesRegex(PendingSourceError, "stale"):
            self.validate()

    def test_pending_target_or_database_membership_is_rejected(self) -> None:
        self.targets["Another"] = {"files": [self.names[0]]}
        with self.assertRaisesRegex(PendingSourceError, "still compiled"):
            self.validate()
        del self.targets["Another"]
        with self.assertRaisesRegex(PendingSourceError, "still compiled"):
            self.validate([self.root / self.names[1]])

    def test_pending_ownership_is_still_required(self) -> None:
        (self.root / "Source/Unknown.cpp").write_text("", encoding="utf-8")
        self.metadata["cpp"] = ["Source/Unknown.cpp"]
        with self.assertRaisesRegex(PendingSourceError, "no unit owns"):
            self.validate()

    def test_missing_export_wrong_producer_and_wrong_schema_fail(self) -> None:
        for targets in ({"Render": {}}, {"Render": {"pending": None}},
                        {"Other": {"pending": self.metadata}}):
            with self.subTest(targets=targets):
                with self.assertRaises(PendingSourceError):
                    validate_pending_sources(self.root, targets, owner=lambda path: "render")
        for version in (0, 2, True, "1", [1]):
            with self.subTest(version=version):
                self.metadata["version"] = version
                with self.assertRaisesRegex(PendingSourceError, "version"):
                    self.validate()
        self.metadata["version"] = 1
        self.metadata["extra"] = []
        with self.assertRaisesRegex(PendingSourceError, "only"):
            self.validate()

    def test_hash_must_match_current_file_bytes(self) -> None:
        for fingerprint in ("invalid", "A" * 64, None, "0" * 64):
            with self.subTest(fingerprint=fingerprint):
                self.metadata["portSha256"] = fingerprint
                with self.assertRaises(PendingSourceError):
                    self.validate()
        self.metadata["portSha256"] = hashlib.sha256(self.port.read_bytes()).hexdigest()
        self.port.write_text("return {changed=true}\n", encoding="utf-8")
        with self.assertRaisesRegex(PendingSourceError, "stale"):
            self.validate()

    def test_port_source_cannot_be_a_directory_or_symlink(self) -> None:
        contents = self.port.read_bytes()
        self.port.unlink()
        self.port.mkdir()
        with self.assertRaisesRegex(PendingSourceError, "regular repository file"):
            self.validate()
        self.port.rmdir()
        other = self.root / "xmake/other.lua"
        other.write_bytes(contents)
        self.port.symlink_to(other)
        with self.assertRaisesRegex(PendingSourceError, "regular repository file"):
            self.validate()

    def test_paths_must_be_existing_canonical_unique_cpp_entries(self) -> None:
        invalid = [str(self.root / self.names[0]), "../Source/Render/Pending.cpp",
                   "Source/./Render/Pending.cpp", "Source//Render/Pending.cpp",
                   "Source/Render/../Render/Pending.cpp", "Source/Render/*.cpp",
                   "Source\\Render\\Pending.cpp", "Tools/Pending.cpp", "Source/Render/Missing.cpp",
                   "Source/Render/Pending.h", ".", "", 7]
        for name in invalid:
            with self.subTest(name=name):
                self.metadata["cpp"] = [name]
                with self.assertRaises(PendingSourceError):
                    self.validate()
        for names in (self.names[::-1], [self.names[0]] * 2, "Source/Render/Pending.cpp"):
            with self.subTest(names=names):
                self.metadata["cpp"] = names
                with self.assertRaises(PendingSourceError):
                    self.validate()

    def test_symlink_and_case_aliases_are_rejected(self) -> None:
        alias = self.root / "Source/Render/Alias.cpp"
        alias.symlink_to("Pending.cpp")
        self.metadata["cpp"] = ["Source/Render/Alias.cpp"]
        with self.assertRaisesRegex(PendingSourceError, "symlink"):
            self.validate()
        with tempfile.TemporaryDirectory() as outside:
            file = Path(outside) / "Outside.cpp"
            file.write_text("", encoding="utf-8")
            alias.unlink()
            alias.symlink_to(file)
            with self.assertRaisesRegex(PendingSourceError, "escapes"):
                self.validate()
        if (self.root / "Source/Render/pending.cpp").is_file():
            self.metadata["cpp"] = ["Source/Render/pending.cpp"]
            with self.assertRaisesRegex(PendingSourceError, "canonical"):
                self.validate()

    def test_loader_failure_does_not_become_an_empty_partition(self) -> None:
        with patch.object(modules, "load_targets", side_effect=modules.ModuleContractError("dump failed")):
            with self.assertRaisesRegex(layout.LayoutSetupError, "dump failed"):
                layout.files_for_check(self.root, self.database)

    def test_target_loader_preserves_metadata_without_coercion(self) -> None:
        self.dump_targets()
        loaded = modules.load_targets(self.dump, self.root)
        self.assertEqual(loaded["Render"]["pending"], self.metadata)
        self.targets["Render"]["pending"] = "bad export"
        self.dump_targets()
        loaded = modules.load_targets(self.dump, self.root)
        with self.assertRaises(PendingSourceError):
            validate_pending_sources(self.root, loaded, owner=lambda path: "render")


if __name__ == "__main__":
    unittest.main()
