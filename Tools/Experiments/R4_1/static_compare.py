#!/usr/bin/env python3
"""Static comparison for the R4.1 shader-dedup experiment.

Compares generated MSL, reflected resource layouts and entry-point bindings for
the four ScenePass family variants (ScenePass, ScenePassAuto, ScenePassMask,
ScenePassAutoMask) between a parent worktree (H) and a candidate worktree (A or
B), plus a Shaders/ inventory that expects the candidate to add exactly
ScenePassShared. See docs/milestones/r/r4.1.md's Measurement section.
"""
from __future__ import annotations

import argparse
import collections
import copy
import difflib
import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

VARIANTS = ["ScenePass", "ScenePassAuto", "ScenePassMask", "ScenePassAutoMask"]
SHARED_MODULE_BASENAME = "ScenePassShared"

# Matches the build rule in xmake/shaders.lua: outputs land beside the App
# binary, not under an "app/" subdirectory.
BUILD_SHADERS_RELATIVE = Path("build/macosx/arm64/release/Shaders")
SLANG_SOURCE_DIR = "Shaders/Passes/Scene"
SLANG_INCLUDE_RELATIVE = Path("Shaders/Common")


_LINE_DIRECTIVE_RE = re.compile(r"^\s*#line\b")


def normalize_msl(text: str) -> list[str]:
    """Return text's lines with every `#line` directive dropped.

    slangc stamps a `#line N "path"` before each chunk it copies from an
    imported module; the path is relative for the entry file itself but
    absolute for Shaders/Common modules (the build rule's -I argument is
    absolute), so two otherwise-identical builds from different worktree
    paths would differ only in these directives without this step.
    """
    return [line for line in text.splitlines() if not _LINE_DIRECTIVE_RE.match(line)]


# An entry point's Metal signature is emitted on one line by this slangc
# version: `[[vertex|fragment]] <ReturnType> <name>(<params>)`. Bindings sit
# directly on entry parameters (no argument-buffer struct indirection), so a
# regex over the header plus a paren-balanced scan of the parameter list is
# enough; no separate struct lookup is needed.
_ENTRY_HEADER_RE = re.compile(r"\[\[(vertex|fragment)\]\]\s+(\S+)\s+(\w+)\s*\(")
_ATTR_RE = re.compile(r"\[\[(buffer|texture|sampler)\((\d+)\)\]\]")
# Slang appends a numeric disambiguator to every identifier it emits
# (`DrawUniforms_0`, `gDraw_1`); the same source produces different suffixes
# in the parent and the candidate purely from unrelated declarations earlier
# in the file, so the suffix must be stripped before two types compare equal.
_IDENT_SUFFIX_RE = re.compile(r"_\d+\b")
_TRAILING_TOKEN_RE = re.compile(r"\s*\S+\s*$")


def _find_matching_paren(text: str, open_index: int) -> int:
    """Index of the ')' matching the '(' at open_index, counting only parens."""
    depth = 0
    for i in range(open_index, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return i
    raise ValueError("unbalanced parentheses in entry signature")


def _split_top_level(params_text: str) -> list[str]:
    """Split a parameter list on top-level commas.

    `()` and `<>` both count as nesting so a template argument list such as
    `texture2d<float, access::sample>` is not split on its internal comma.
    """
    parts: list[str] = []
    depth = 0
    current: list[str] = []
    for ch in params_text:
        if ch in "(<":
            depth += 1
        elif ch in ")>":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append("".join(current))
            current = []
        else:
            current.append(ch)
    parts.append("".join(current))
    return [p.strip() for p in parts if p.strip()]


def entry_bindings(msl: str) -> dict[str, "collections.Counter"]:
    """Map each [[vertex]]/[[fragment]] entry name to a Counter of
    (type, name, attribute) for every argument carrying [[buffer(n)]],
    [[texture(n)]] or [[sampler(n)]], with Slang's `_<digits>` identifier
    suffixes stripped from both the type and the argument name.

    The argument name is part of the key, not just the type and attribute:
    without it, swapping two same-type arguments' slots (e.g. texture(4) and
    texture(5) between gDiffuse and gNormalMap) would leave the multiset of
    (type, attribute) pairs unchanged and hide a real binding regression.
    """
    result: dict[str, collections.Counter] = {}
    for match in _ENTRY_HEADER_RE.finditer(msl):
        name = match.group(3)
        open_paren = match.end() - 1
        close_paren = _find_matching_paren(msl, open_paren)
        params_text = msl[open_paren + 1 : close_paren]
        counter: collections.Counter = collections.Counter()
        for param in _split_top_level(params_text):
            attr_match = _ATTR_RE.search(param)
            if not attr_match:
                continue
            attribute = f"{attr_match.group(1)}({attr_match.group(2)})"
            before_attr = param[: attr_match.start()].rstrip()
            name_match = _TRAILING_TOKEN_RE.search(before_attr)
            arg_name = _IDENT_SUFFIX_RE.sub("", name_match.group(0).strip()) if name_match else ""
            type_text = _TRAILING_TOKEN_RE.sub("", before_attr).strip()
            type_text = _IDENT_SUFFIX_RE.sub("", type_text)
            counter[(type_text, arg_name, attribute)] += 1
        result[name] = counter
    return result


def _attribute_occurrence_count(msl: str) -> int:
    """Count every [[buffer(n)]]/[[texture(n)]]/[[sampler(n)]] occurrence in
    the whole file, anywhere -- not just inside a recognized entry signature.
    Compared against what entry_bindings actually captured, this guards
    against a vacuous pass: a signature format entry_bindings fails to parse
    would otherwise silently drop arguments and still report equality.
    """
    return len(_ATTR_RE.findall(msl))


EXPECTED_ENTRY_POINTS = frozenset({"vertexMain", "fragmentMain", "vertexMainMotion", "fragmentMainMotion"})


_MASKED_RE = re.compile(r"\bmasked(_\d+)?\b")


def masked_hits(msl: str) -> list[str]:
    """Lines of msl matching \\bmasked(_\\d+)?\\b (candidate B's coverage scan)."""
    return [line for line in msl.splitlines() if _MASKED_RE.search(line)]


def _sorted_by_name(items: list) -> list:
    return sorted(items, key=lambda item: item.get("name", "") if isinstance(item, dict) else "")


def normalize_reflection(reflection: dict) -> dict:
    """Return a copy of a parsed -reflection-json document with its top-level
    `parameters` sorted by `name`, its `entryPoints` sorted by `name`, and,
    within each entry point, its own `bindings` and `parameters` lists each
    sorted by `name`.

    These are all enumeration orders, not layout: a generic-dispatch entry
    (candidate A's shadeSurfaceDetailed<E, C>) walks its resources in a
    different order than a monomorphic one even when every binding index,
    type and offset is identical, so their raw ordering must not count as a
    layout difference. Nothing else is touched or dropped -- a real binding
    index, type or uniform-offset change still breaks equality, because nested
    struct/field order is a layout property, not an enumeration artifact.
    """
    normalized = copy.deepcopy(reflection)
    if isinstance(normalized.get("parameters"), list):
        normalized["parameters"] = _sorted_by_name(normalized["parameters"])
    entry_points = normalized.get("entryPoints")
    if isinstance(entry_points, list):
        for entry in entry_points:
            if not isinstance(entry, dict):
                continue
            for key in ("bindings", "parameters"):
                sub = entry.get(key)
                if isinstance(sub, list):
                    entry[key] = _sorted_by_name(sub)
        normalized["entryPoints"] = _sorted_by_name(entry_points)
    return normalized


def msl_diff(parent_text: str, candidate_text: str, parent_label: str, candidate_label: str) -> tuple[str, int]:
    """Unified diff of two MSL sources after #line normalization, plus the
    count of differing (added/removed) content lines."""
    parent_lines = normalize_msl(parent_text)
    candidate_lines = normalize_msl(candidate_text)
    diff = list(
        difflib.unified_diff(
            parent_lines, candidate_lines, fromfile=parent_label, tofile=candidate_label, lineterm=""
        )
    )
    differing = sum(1 for line in diff if line[:1] in ("+", "-") and not line.startswith(("+++", "---")))
    text = "\n".join(diff)
    if text:
        text += "\n"
    return text, differing


def _basenames(directory: Path, suffix: str) -> set[str]:
    if not directory.is_dir():
        return set()
    return {p.stem for p in directory.glob(f"*{suffix}") if p.is_file()}


def compare_inventory(parent_wt: Path, candidate_wt: Path) -> dict:
    """Compare *.metal and *.metallib basenames under each side's build
    Shaders/ directory. The candidate must equal the parent plus exactly
    ScenePassShared for both extensions; any other difference is reported,
    never raised, so a self-comparison (no candidate yet) reports cleanly.
    """
    parent_dir = parent_wt / BUILD_SHADERS_RELATIVE
    candidate_dir = candidate_wt / BUILD_SHADERS_RELATIVE

    parent_metal = _basenames(parent_dir, ".metal")
    candidate_metal = _basenames(candidate_dir, ".metal")
    parent_metallib = _basenames(parent_dir, ".metallib")
    candidate_metallib = _basenames(candidate_dir, ".metallib")

    metal_extra = sorted(candidate_metal - parent_metal)
    metal_missing = sorted(parent_metal - candidate_metal)
    metallib_extra = sorted(candidate_metallib - parent_metallib)
    metallib_missing = sorted(parent_metallib - candidate_metallib)

    ok = (
        metal_extra == [SHARED_MODULE_BASENAME]
        and not metal_missing
        and metallib_extra == [SHARED_MODULE_BASENAME]
        and not metallib_missing
    )
    reason = None
    if not ok:
        reason = (
            f"expected the candidate's .metal/.metallib inventory to equal the parent's plus "
            f"exactly '{SHARED_MODULE_BASENAME}'; got metal extra={metal_extra} "
            f"missing={metal_missing}, metallib extra={metallib_extra} missing={metallib_missing}"
        )
    return {
        "metalExtra": metal_extra,
        "metalMissing": metal_missing,
        "metallibExtra": metallib_extra,
        "metallibMissing": metallib_missing,
        "ok": ok,
        "reason": reason,
    }


# --- Selftests --------------------------------------------------------------

SAMPLE_VERTEX = (
    "[[vertex]] vertexMain_Result_0 vertexMain(uint vid_1 [[vertex_id]], "
    "DrawUniforms_0 constant* gDraw_1 [[buffer(1)]], "
    "texture2d<float, access::sample> gDiffuse_1 [[texture(0)]], "
    "sampler gLinearSampler_1 [[sampler(0)]])\n{ return x; }\n"
)

# A minimal synthetic stand-in for a ScenePass-family .metal file: exactly the four expected
# entry points, one bound argument each, nothing else. Used by the evaluate_gate1 selftests,
# which exercise the vacuous-pass guards without needing a real slangc/worktree.
FOUR_ENTRY_MSL = "\n".join(
    [
        "[[vertex]] R0 vertexMain(uint vid [[vertex_id]], T0 constant* gA_0 [[buffer(0)]])",
        "{ return x; }",
        "[[fragment]] R1 fragmentMain(P0 input [[stage_in]], T1 constant* gB_0 [[buffer(1)]])",
        "{ return x; }",
        "[[vertex]] R2 vertexMainMotion(uint vid [[vertex_id]], T2 constant* gC_0 [[buffer(2)]])",
        "{ return x; }",
        "[[fragment]] R3 fragmentMainMotion(P1 input [[stage_in]], T3 constant* gD_0 [[buffer(3)]])",
        "{ return x; }",
    ]
) + "\n"

BASE_REFLECTION = {
    "parameters": [
        {"name": "gDraw", "binding": {"kind": "constantBuffer", "index": 1}},
        {
            "name": "gPass",
            "binding": {"kind": "constantBuffer", "index": 2},
            "type": {
                "kind": "struct",
                "fields": [
                    {
                        "name": "preExposure",
                        "type": {"kind": "scalar", "scalarType": "float32"},
                        "binding": {"kind": "uniform", "offset": 0, "size": 4},
                    }
                ],
            },
        },
    ],
    "entryPoints": [
        {
            "name": "fragmentMain",
            "stage": "fragment",
            "bindings": [
                {"name": "gPass", "binding": {"kind": "constantBuffer", "index": 2}},
                {"name": "gDiffuse", "binding": {"kind": "shaderResource", "index": 0}},
            ],
        },
        {
            "name": "vertexMain",
            "stage": "vertex",
            "bindings": [
                {"name": "gDraw", "binding": {"kind": "constantBuffer", "index": 1}},
            ],
            "parameters": [{"name": "vid"}, {"name": "instanceIndex"}],
        },
    ],
}


class StaticCompareTests(unittest.TestCase):
    def test_normalize_msl_drops_line_directives(self):
        text = '#line 5 "foo.slang"\nfloat4 x;\n#line 9\nfloat4 y;\n'
        self.assertEqual(normalize_msl(text), ["float4 x;", "float4 y;"])

    def test_entry_bindings_extracts_and_strips_suffixes(self):
        bindings = entry_bindings(SAMPLE_VERTEX)
        self.assertEqual(set(bindings), {"vertexMain"})
        self.assertEqual(
            bindings["vertexMain"],
            collections.Counter(
                {
                    ("DrawUniforms constant*", "gDraw", "buffer(1)"): 1,
                    ("texture2d<float, access::sample>", "gDiffuse", "texture(0)"): 1,
                    ("sampler", "gLinearSampler", "sampler(0)"): 1,
                }
            ),
        )

    def test_entry_bindings_includes_argument_name_so_swapped_indices_differ(self):
        # Same two types, same two indices, but swapped between arguments: a multiset that
        # ignored argument name would see these as equal, hiding a real slot swap.
        base = (
            "[[fragment]] pixelOutput_0 fragmentMain(pixelInput_0 _S1 [[stage_in]], "
            "texture2d<float, access::sample> gDiffuse_1 [[texture(4)]], "
            "texture2d<float, access::sample> gNormalMap_1 [[texture(5)]])\n{ return x; }\n"
        )
        swapped = base.replace("gDiffuse_1 [[texture(4)]]", "gDiffuse_1 [[texture(5)]]").replace(
            "gNormalMap_1 [[texture(5)]]", "gNormalMap_1 [[texture(4)]]"
        )
        self.assertEqual(
            entry_bindings(base)["fragmentMain"],
            collections.Counter(
                {
                    ("texture2d<float, access::sample>", "gDiffuse", "texture(4)"): 1,
                    ("texture2d<float, access::sample>", "gNormalMap", "texture(5)"): 1,
                }
            ),
        )
        self.assertNotEqual(entry_bindings(base), entry_bindings(swapped))

    def test_entry_bindings_suffix_stripping_makes_renumbered_shaders_equal(self):
        renumbered = SAMPLE_VERTEX.replace("_0", "_7").replace("_1", "_9")
        self.assertNotEqual(SAMPLE_VERTEX, renumbered)
        self.assertEqual(entry_bindings(SAMPLE_VERTEX), entry_bindings(renumbered))

    def test_entry_bindings_changed_binding_index_breaks_equality(self):
        changed = SAMPLE_VERTEX.replace("[[buffer(1)]]", "[[buffer(2)]]")
        self.assertNotEqual(entry_bindings(SAMPLE_VERTEX), entry_bindings(changed))

    def test_entry_bindings_changed_type_breaks_equality(self):
        changed = SAMPLE_VERTEX.replace("DrawUniforms_0 constant*", "OtherUniforms_0 constant*")
        self.assertNotEqual(entry_bindings(SAMPLE_VERTEX), entry_bindings(changed))

    def test_masked_hits_scan(self):
        text = "\n".join(
            [
                "bool masked = true;",
                "float maskedValue = 0.0;",
                "if (masked_3) { }",
                "float unmasked = 1.0;",
            ]
        )
        self.assertEqual(masked_hits(text), ["bool masked = true;", "if (masked_3) { }"])

    def test_normalize_reflection_sorts_parameters_and_entry_points(self):
        reversed_reflection = copy.deepcopy(BASE_REFLECTION)
        reversed_reflection["parameters"] = list(reversed(reversed_reflection["parameters"]))
        reversed_reflection["entryPoints"] = list(reversed(reversed_reflection["entryPoints"]))
        self.assertNotEqual(BASE_REFLECTION, reversed_reflection)
        self.assertEqual(
            normalize_reflection(BASE_REFLECTION), normalize_reflection(reversed_reflection)
        )

    def test_normalize_reflection_does_not_mutate_input(self):
        original = copy.deepcopy(BASE_REFLECTION)
        normalize_reflection(BASE_REFLECTION)
        self.assertEqual(BASE_REFLECTION, original)

    def test_reflection_parameter_reorder_keeps_layout_equal_not_raw_equal(self):
        a = copy.deepcopy(BASE_REFLECTION)
        b = copy.deepcopy(BASE_REFLECTION)
        b["parameters"] = list(reversed(b["parameters"]))
        self.assertNotEqual(a, b)
        self.assertEqual(normalize_reflection(a), normalize_reflection(b))

    def test_reflection_changed_binding_index_breaks_equality(self):
        a = copy.deepcopy(BASE_REFLECTION)
        b = copy.deepcopy(BASE_REFLECTION)
        b["parameters"][0]["binding"]["index"] = 9
        self.assertNotEqual(a, b)
        self.assertNotEqual(normalize_reflection(a), normalize_reflection(b))

    def test_reflection_changed_type_breaks_equality(self):
        a = copy.deepcopy(BASE_REFLECTION)
        b = copy.deepcopy(BASE_REFLECTION)
        b["parameters"][1]["type"]["fields"][0]["type"]["scalarType"] = "uint32"
        self.assertNotEqual(normalize_reflection(a), normalize_reflection(b))

    def test_reflection_changed_uniform_offset_breaks_equality(self):
        a = copy.deepcopy(BASE_REFLECTION)
        b = copy.deepcopy(BASE_REFLECTION)
        b["parameters"][1]["type"]["fields"][0]["binding"]["offset"] = 4
        self.assertNotEqual(normalize_reflection(a), normalize_reflection(b))

    def test_reflection_entry_point_binding_reorder_keeps_layout_equal_not_raw_equal(self):
        # Candidate A's generic dispatch visits gExposureOverride before gPass/gDiffuse where H
        # visits it between them; the binding assignments are identical, only the enumeration
        # order of each entry point's own `bindings` list differs.
        a = copy.deepcopy(BASE_REFLECTION)
        b = copy.deepcopy(BASE_REFLECTION)
        b["entryPoints"][0]["bindings"] = list(reversed(b["entryPoints"][0]["bindings"]))
        self.assertNotEqual(a, b)
        self.assertEqual(normalize_reflection(a), normalize_reflection(b))

    def test_reflection_entry_point_parameter_reorder_keeps_layout_equal_not_raw_equal(self):
        a = copy.deepcopy(BASE_REFLECTION)
        b = copy.deepcopy(BASE_REFLECTION)
        b["entryPoints"][1]["parameters"] = list(reversed(b["entryPoints"][1]["parameters"]))
        self.assertNotEqual(a, b)
        self.assertEqual(normalize_reflection(a), normalize_reflection(b))

    def test_reflection_entry_point_binding_index_change_breaks_layout_equal(self):
        # A real layout regression hiding inside one entry point's own `bindings` list (as
        # opposed to the top-level `parameters` list) must not be swallowed by the sort.
        a = copy.deepcopy(BASE_REFLECTION)
        b = copy.deepcopy(BASE_REFLECTION)
        b["entryPoints"][0]["bindings"][0]["binding"]["index"] = 99
        self.assertNotEqual(a, b)
        self.assertNotEqual(normalize_reflection(a), normalize_reflection(b))

    def test_msl_diff_ignores_line_directives_counts_real_changes(self):
        parent = '#line 1 "a"\nfloat4 x = 1.0;\n#line 2 "b"\nfloat4 y = 2.0;\n'
        candidate = '#line 99 "a"\nfloat4 x = 1.0;\n#line 100 "b"\nfloat4 y = 3.0;\n'
        diff_text, count = msl_diff(parent, candidate, "parent", "A")
        self.assertEqual(count, 2)
        self.assertNotIn("#line", diff_text)

    def test_msl_diff_identical_after_normalization_is_zero(self):
        parent = '#line 1 "a"\nfloat4 x = 1.0;\n'
        candidate = '#line 2 "a"\nfloat4 x = 1.0;\n'
        _, count = msl_diff(parent, candidate, "parent", "A")
        self.assertEqual(count, 0)

    def test_compare_inventory_flags_unmet_rule_when_identical(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            parent, candidate = root / "parent", root / "candidate"
            for wt in (parent, candidate):
                shaders = wt / BUILD_SHADERS_RELATIVE
                shaders.mkdir(parents=True)
                (shaders / "ScenePass.metal").write_text("x")
                (shaders / "ScenePass.metallib").write_bytes(b"x")
            result = compare_inventory(parent, candidate)
            self.assertEqual(result["metalExtra"], [])
            self.assertEqual(result["metallibExtra"], [])
            self.assertFalse(result["ok"])
            self.assertIsNotNone(result["reason"])

    def test_compare_inventory_accepts_shared_module_addition(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            parent, candidate = root / "parent", root / "candidate"
            shaders_p = parent / BUILD_SHADERS_RELATIVE
            shaders_c = candidate / BUILD_SHADERS_RELATIVE
            shaders_p.mkdir(parents=True)
            shaders_c.mkdir(parents=True)
            (shaders_p / "ScenePass.metal").write_text("x")
            (shaders_p / "ScenePass.metallib").write_bytes(b"x")
            (shaders_c / "ScenePass.metal").write_text("x")
            (shaders_c / "ScenePass.metallib").write_bytes(b"x")
            (shaders_c / f"{SHARED_MODULE_BASENAME}.metal").write_text("x")
            (shaders_c / f"{SHARED_MODULE_BASENAME}.metallib").write_bytes(b"x")
            result = compare_inventory(parent, candidate)
            self.assertTrue(result["ok"])
            self.assertEqual(result["metalExtra"], [SHARED_MODULE_BASENAME])
            self.assertEqual(result["metallibExtra"], [SHARED_MODULE_BASENAME])

    def test_compare_inventory_flags_missing_parent_file(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            parent, candidate = root / "parent", root / "candidate"
            shaders_p = parent / BUILD_SHADERS_RELATIVE
            shaders_c = candidate / BUILD_SHADERS_RELATIVE
            shaders_p.mkdir(parents=True)
            shaders_c.mkdir(parents=True)
            (shaders_p / "ScenePass.metal").write_text("x")
            (shaders_p / "ScenePassAuto.metal").write_text("x")
            (shaders_c / "ScenePass.metal").write_text("x")
            (shaders_c / f"{SHARED_MODULE_BASENAME}.metal").write_text("x")
            result = compare_inventory(parent, candidate)
            self.assertFalse(result["ok"])
            self.assertEqual(result["metalMissing"], ["ScenePassAuto"])

    def test_evaluate_gate1_true_despite_msl_text_difference(self):
        # Gate 1 answers decision item 1 (reflected layout + entry-point bindings), not MSL
        # identity: deduplication is expected to change the generated MSL text.
        parent_metal = FOUR_ENTRY_MSL
        candidate_metal = FOUR_ENTRY_MSL.replace("gA_0", "gA_9")
        self.assertNotEqual(parent_metal, candidate_metal)
        reflection = copy.deepcopy(BASE_REFLECTION)
        gate1, reasons, mismatches = evaluate_gate1(
            parent_metal, candidate_metal, parent_metal, candidate_metal, reflection, reflection
        )
        self.assertTrue(gate1)
        self.assertEqual(reasons, [])
        self.assertEqual(mismatches, [])

    def test_evaluate_gate1_false_and_names_stale_side_on_reproduction_mismatch(self):
        drifted_parent_reflect = FOUR_ENTRY_MSL + "\n// drifted\n"
        reflection = copy.deepcopy(BASE_REFLECTION)
        gate1, reasons, _ = evaluate_gate1(
            FOUR_ENTRY_MSL, FOUR_ENTRY_MSL, drifted_parent_reflect, FOUR_ENTRY_MSL, reflection, reflection
        )
        self.assertFalse(gate1)
        self.assertTrue(any("stale side: parent" in r for r in reasons), reasons)

    def test_evaluate_gate1_false_when_entry_point_set_is_wrong(self):
        missing_one = "\n".join(FOUR_ENTRY_MSL.splitlines()[:-2]) + "\n"
        self.assertEqual(set(entry_bindings(missing_one)), {"vertexMain", "fragmentMain", "vertexMainMotion"})
        reflection = copy.deepcopy(BASE_REFLECTION)
        gate1, reasons, _ = evaluate_gate1(
            missing_one, FOUR_ENTRY_MSL, missing_one, FOUR_ENTRY_MSL, reflection, reflection
        )
        self.assertFalse(gate1)
        self.assertTrue(any("expected entry points" in r for r in reasons), reasons)

    def test_evaluate_gate1_false_when_attribute_count_does_not_match_whole_file(self):
        # A [[buffer(9)]]-looking attribute sits outside any recognized entry signature;
        # entry_bindings must not silently under-count and call it a pass.
        stray = FOUR_ENTRY_MSL + "\nT9 constant* gStray_0 [[buffer(9)]];\n"
        reflection = copy.deepcopy(BASE_REFLECTION)
        gate1, reasons, _ = evaluate_gate1(stray, FOUR_ENTRY_MSL, stray, FOUR_ENTRY_MSL, reflection, reflection)
        self.assertFalse(gate1)
        self.assertTrue(any("attributes in the file" in r for r in reasons), reasons)


# --- Real comparison (not exercised by --selftest, which is stdlib-only and
# has no worktree to build against) -----------------------------------------


def read_build_metal(worktree: Path, variant: str) -> str:
    path = worktree / BUILD_SHADERS_RELATIVE / f"{variant}.metal"
    if not path.is_file():
        raise FileNotFoundError(f"missing build output: {path}")
    return path.read_text()


def compile_reflection(worktree: Path, variant: str, out_dir: Path) -> tuple[str, dict]:
    """Recompile <variant>.slang with the build rule's own arguments
    (xmake/shaders.lua) plus -reflection-json, from worktree as cwd.

    The source path is relative and the include path absolute, exactly as
    xmake's slang2metallib rule invokes slangc, which is what makes the
    output byte-identical to the build's own <variant>.metal.
    """
    out_dir.mkdir(parents=True, exist_ok=True)
    metal_path = out_dir / f"{variant}.metal"
    json_path = out_dir / f"{variant}.json"
    slangc = worktree / "ThirdParty" / "slang" / "bin" / "slangc"
    source = f"{SLANG_SOURCE_DIR}/{variant}.slang"
    include_dir = worktree / SLANG_INCLUDE_RELATIVE
    command = [
        str(slangc),
        source,
        "-I",
        str(include_dir),
        "-target",
        "metal",
        "-o",
        str(metal_path),
        "-reflection-json",
        str(json_path),
    ]
    proc = subprocess.run(command, cwd=str(worktree), capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(
            f"slangc failed for {variant} in {worktree}: {proc.stderr.strip()}\ncommand: {' '.join(command)}"
        )
    return metal_path.read_text(), json.loads(json_path.read_text())


def _serialize_counter(counter: "collections.Counter") -> list[dict]:
    return [
        {"type": t, "name": n, "attribute": a, "count": c} for (t, n, a), c in sorted(counter.items())
    ]


def _binding_mismatches(parent: dict, candidate: dict) -> list[dict]:
    mismatches = []
    for name in sorted(set(parent) | set(candidate)):
        p = parent.get(name, collections.Counter())
        c = candidate.get(name, collections.Counter())
        if p != c:
            mismatches.append(
                {
                    "entry": name,
                    "onlyInParent": _serialize_counter(p - c),
                    "onlyInCandidate": _serialize_counter(c - p),
                }
            )
    return mismatches


def evaluate_gate1(
    parent_metal: str,
    candidate_metal: str,
    parent_reflect_metal: str,
    candidate_reflect_metal: str,
    parent_reflection: dict,
    candidate_reflection: dict,
) -> tuple[bool, list[str], list[dict]]:
    """Decision item 1 only: every variant's reflected layout and entry
    signatures equal H's. Returns (gate1, reasons, entryBindingMismatches);
    reasons is empty iff gate1 is true.

    MSL text identity plays no part here -- deduplication is expected to
    change the generated MSL, so that difference is reported as evidence
    elsewhere and never gates the decision. Reproduction is enforced first:
    if either side's freshly recompiled reflection run does not byte-for-byte
    reproduce that side's own build .metal, the comparison below would be
    checking a stale artifact, so gate1 fails and names the stale side before
    anything else is evaluated. The entry-point-set and attribute-count checks
    guard against a vacuous pass: a signature entry_bindings fails to parse
    would otherwise silently drop arguments and still report equality.
    """
    reasons: list[str] = []

    if parent_reflect_metal != parent_metal:
        reasons.append(
            "parent: reflection run does not reproduce the parent build's .metal (stale side: parent)"
        )
    if candidate_reflect_metal != candidate_metal:
        reasons.append(
            "candidate: reflection run does not reproduce the candidate build's .metal "
            "(stale side: candidate)"
        )

    parent_bindings = entry_bindings(parent_metal)
    candidate_bindings = entry_bindings(candidate_metal)
    for side, metal, bindings in (
        ("parent", parent_metal, parent_bindings),
        ("candidate", candidate_metal, candidate_bindings),
    ):
        names = set(bindings)
        if names != EXPECTED_ENTRY_POINTS:
            reasons.append(
                f"{side}: expected entry points {sorted(EXPECTED_ENTRY_POINTS)}, found {sorted(names)}"
            )
        captured = sum(sum(counter.values()) for counter in bindings.values())
        total = _attribute_occurrence_count(metal)
        if captured != total:
            reasons.append(
                f"{side}: captured {captured} of {total} [[buffer|texture|sampler(n)]] "
                "attributes in the file"
            )

    if normalize_reflection(parent_reflection) != normalize_reflection(candidate_reflection):
        reasons.append("reflected layout differs (layoutEqual=false)")

    mismatches = _binding_mismatches(parent_bindings, candidate_bindings)
    if mismatches:
        reasons.append(
            "entry-point binding multisets differ: " + ", ".join(m["entry"] for m in mismatches)
        )

    return not reasons, reasons, mismatches


def compare_variant(parent_wt: Path, candidate_wt: Path, variant: str, label: str, label_dir: Path) -> dict:
    parent_metal = read_build_metal(parent_wt, variant)
    candidate_metal = read_build_metal(candidate_wt, variant)

    # MSL diff is evidence only (line count + diff file); deduplication is expected to change
    # the generated text, so it never gates decision item 1.
    diff_text, diff_lines = msl_diff(parent_metal, candidate_metal, "parent", label)
    diff_path = label_dir / f"{variant}.msl.diff"
    diff_path.write_text(diff_text)

    reflect_root = label_dir / "_reflect"
    parent_reflect_metal, parent_reflection = compile_reflection(parent_wt, variant, reflect_root / "parent")
    candidate_reflect_metal, candidate_reflection = compile_reflection(candidate_wt, variant, reflect_root / label)

    gate1, gate1_reasons, mismatches = evaluate_gate1(
        parent_metal, candidate_metal, parent_reflect_metal, candidate_reflect_metal,
        parent_reflection, candidate_reflection,
    )

    return {
        "msl": {
            "diffPath": diff_path.name,
            "diffLines": diff_lines,
            "equal": diff_lines == 0,
        },
        "reflection": {
            "parentReproducesBuild": parent_reflect_metal == parent_metal,
            "candidateReproducesBuild": candidate_reflect_metal == candidate_metal,
            "layoutEqual": normalize_reflection(parent_reflection) == normalize_reflection(candidate_reflection),
            "rawEqual": parent_reflection == candidate_reflection,
        },
        "entryBindings": {
            "equal": not mismatches,
            "mismatches": mismatches,
        },
        "masked": {
            "parentHits": masked_hits(parent_metal),
            "candidateHits": masked_hits(candidate_metal),
        },
        "gate1": {
            "value": gate1,
            "reasons": gate1_reasons,
        },
    }


def render_markdown(result: dict) -> str:
    lines = [
        f"# Static comparison — {result['label']}",
        "",
        f"- parent: `{result['parent']}`",
        f"- candidate: `{result['candidate']}`",
        "",
        "Gate 1 is decision item 1 (reflected layout + entry-point bindings equal H's, with "
        "reproduction enforced); it decides the exit code together with the inventory rule. MSL "
        "diff lines are evidence only -- deduplication is expected to change the generated text.",
        "",
        "| Variant | Gate 1 | MSL diff lines (evidence) | layoutEqual | rawEqual | "
        "reflects build (P/C) | entry bindings equal | masked hits (candidate) |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for variant in VARIANTS:
        v = result["variants"][variant]
        lines.append(
            f"| {variant} | {v['gate1']['value']} | {v['msl']['diffLines']} | "
            f"{v['reflection']['layoutEqual']} | {v['reflection']['rawEqual']} | "
            f"{v['reflection']['parentReproducesBuild']}/{v['reflection']['candidateReproducesBuild']} | "
            f"{v['entryBindings']['equal']} | {len(v['masked']['candidateHits'])} |"
        )
    inv = result["inventory"]
    lines += [
        "",
        "## Inventory",
        "",
        f"- metal extra: {inv['metalExtra']}",
        f"- metal missing: {inv['metalMissing']}",
        f"- metallib extra: {inv['metallibExtra']}",
        f"- metallib missing: {inv['metallibMissing']}",
        f"- ok: {inv['ok']}",
    ]
    if inv["reason"]:
        lines.append(f"- reason: {inv['reason']}")
    lines += ["", "## Gate 1 reasons (when not met)", ""]
    any_reason = False
    for variant in VARIANTS:
        reasons = result["variants"][variant]["gate1"]["reasons"]
        if reasons:
            any_reason = True
            lines.append(f"- **{variant}**:")
            lines.extend(f"  - {reason}" for reason in reasons)
    if not any_reason:
        lines.append("- (none)")
    lines += [
        "",
        f"**Gate 1 — decision item 1, all variants:** {result['summary']['gate1AllVariants']}",
        f"**Inventory rule met:** {result['summary']['inventoryOk']}",
        f"**Overall (exit-code basis: gate1AllVariants and inventoryOk):** {result['summary']['pass']}",
    ]
    return "\n".join(lines) + "\n"


def compare(parent_wt: Path, candidate_wt: Path, label: str, output_dir: Path) -> dict:
    label_dir = output_dir / label
    label_dir.mkdir(parents=True, exist_ok=True)

    variants_result = {
        variant: compare_variant(parent_wt, candidate_wt, variant, label, label_dir) for variant in VARIANTS
    }
    inventory = compare_inventory(parent_wt, candidate_wt)
    gate1_all_variants = all(v["gate1"]["value"] for v in variants_result.values())

    result = {
        "label": label,
        "parent": str(parent_wt),
        "candidate": str(candidate_wt),
        "variants": variants_result,
        "inventory": inventory,
        "summary": {
            "gate1AllVariants": gate1_all_variants,
            "inventoryOk": inventory["ok"],
            "pass": gate1_all_variants and inventory["ok"],
        },
    }

    (label_dir / "static.json").write_text(json.dumps(result, indent=2) + "\n")
    (label_dir / "static.md").write_text(render_markdown(result))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--parent", type=Path)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--label")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        result = unittest.TextTestRunner(verbosity=2).run(
            unittest.defaultTestLoader.loadTestsFromTestCase(StaticCompareTests)
        )
        return 0 if result.wasSuccessful() else 1
    if None in (args.parent, args.candidate, args.label, args.output):
        parser.error("--parent, --candidate, --label and --output are required unless --selftest is used")
    try:
        result = compare(args.parent.resolve(), args.candidate.resolve(), args.label, args.output.resolve())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"static_compare refused: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result["summary"], indent=2))
    return 0 if result["summary"]["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
