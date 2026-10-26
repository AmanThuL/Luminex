from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from Tools import check_cpp_layout as layout


def separator(indent: str = "") -> str:
    return indent + "//" + "=" * (118 - len(indent))


def file_header(name: str) -> list[str]:
    return [layout.FILE_RULER, f"/// @file {name}", "/// @brief Describes this file.", layout.FILE_RULER]


class ParsedFixtureClient(layout.ClangdClient):
    """Replay parsed compiler responses without starting an LSP process."""

    def __init__(self, symbols: list, tokens: list[int], ast: dict | None) -> None:
        self._definition_modifier = 1
        self._token_types = ["operator", "function", "method", "variable", "class"]
        self._ast_provider = True
        self.symbols = symbols
        self.tokens = tokens
        self.ast = ast
        self.ast_selections = []

    def notify(self, method: str, params: dict) -> None:
        pass

    def request(self, method: str, params: dict) -> dict:
        if method == "textDocument/documentSymbol":
            return {"result": self.symbols}
        if method == "textDocument/semanticTokens/full":
            return {"result": {"data": self.tokens}}
        if method == "textDocument/ast":
            self.ast_selections.append(params["range"])
            return {"result": self.ast}
        raise AssertionError(f"unexpected request: {method}")


def function_ast(start: int, end: int, kind: str = "Function") -> dict:
    return {
        "role": "declaration", "kind": kind, "detail": "operator==",
        "range": {"start": {"line": start, "character": 4},
                  "end": {"line": end, "character": 74}},
    }


class CppLayoutTests(unittest.TestCase):
    def test_omitted_defaulted_friend_uses_parsed_boundary(self) -> None:
        # Apple clangd exposes the == definition token while omitting its document symbol.
        client = ParsedFixtureClient([], [3, 24, 2, 0, 3], function_ast(3, 3))
        starts = client.definition_lines(Path("/workspace/Source/Friend.cpp"), "")
        self.assertEqual(starts, {3})
        self.assertEqual(client.ast_selections, [{"start": {"line": 3, "character": 24},
                                                 "end": {"line": 3, "character": 26}}])

    def test_recovered_friend_requires_and_claims_indented_separator(self) -> None:
        source = ["struct S {", "    int value;", "", "    friend bool operator==(S, S) = default;", "};"]
        client = ParsedFixtureClient([], [3, 24, 2, 0, 3], function_ast(3, 3))
        starts = client.definition_lines(Path("/workspace/Source/Friend.cpp"), "\n".join(source))
        self.assertTrue(any("missing" in error for error in
                            layout.check_layout(Path("Source/Friend.cpp"), "\n".join(source), starts)))
        source.insert(3, separator("    "))
        client = ParsedFixtureClient([], [4, 24, 2, 0, 3], function_ast(4, 4))
        starts = client.definition_lines(Path("/workspace/Source/Friend.cpp"), "\n".join(source))
        self.assertEqual(layout.check_layout(Path("Source/Friend.cpp"), "\n".join(source), starts), [])

    def test_document_symbol_and_ast_boundary_are_deduplicated(self) -> None:
        uri = "file:///workspace/Source/Friend.cpp"
        symbols = [{"kind": 12, "name": "operator==", "location": {
            "uri": uri, "range": {"start": {"line": 3}, "end": {"line": 3}}}}]
        client = ParsedFixtureClient(symbols, [3, 24, 2, 0, 3], function_ast(3, 3))
        self.assertEqual(client.definition_lines(Path("/workspace/Source/Friend.cpp"), ""), {3})

    def test_multiline_friend_starts_before_its_name_token(self) -> None:
        client = ParsedFixtureClient([], [5, 8, 2, 0, 3], function_ast(3, 6))
        self.assertEqual(client.definition_lines(Path("/workspace/Source/Friend.cpp"), ""), {3})

    def test_outer_symbol_range_does_not_mask_nested_definition(self) -> None:
        uri = "file:///workspace/Source/Local.cpp"
        symbols = [{"kind": 12, "name": "outer", "location": {
            "uri": uri, "range": {"start": {"line": 1}, "end": {"line": 10}}}}]
        client = ParsedFixtureClient(symbols, [5, 8, 3, 2, 3], function_ast(4, 6, "CXXMethod"))
        self.assertEqual(client.definition_lines(Path("/workspace/Source/Local.cpp"), ""), {1, 4})

    def test_non_callable_and_non_definition_tokens_do_not_request_ast(self) -> None:
        # A friend declaration, operator expression, variable and class definition.
        tokens = [1, 4, 2, 0, 1, 1, 4, 1, 0, 0, 1, 4, 3, 3, 3, 1, 4, 1, 4, 3]
        client = ParsedFixtureClient([], tokens, None)
        self.assertEqual(client.definition_lines(Path("/workspace/Source/Declarations.cpp"), ""), set())
        self.assertEqual(client.ast_selections, [])

    def test_implicit_nodes_and_lambda_ast_do_not_invent_named_boundaries(self) -> None:
        # An unrelated implicit function has no source range; a lambda is not a function declaration.
        ast = {"kind": "Lambda", "role": "expression", "children": [
            {"kind": "CXXMethod", "role": "declaration", "detail": "operator()"}]}
        client = ParsedFixtureClient([], [3, 4, 2, 0, 3], ast)
        with self.assertRaises(layout.LayoutSetupError):
            client.definition_lines(Path("/workspace/Source/Lambda.cpp"), "")

    def test_unresolved_callable_definition_fails_closed(self) -> None:
        for ast in (None, {}, {"role": "declaration", "kind": "Function"},
                    function_ast(8, 9), {"role": "declaration", "kind": "Function", "range": {}}):
            with self.subTest(ast=ast):
                client = ParsedFixtureClient([], [3, 24, 2, 0, 3], ast)
                with self.assertRaises(layout.LayoutSetupError):
                    client.definition_lines(Path("/workspace/Source/Friend.cpp"), "")

    def test_missing_ast_capability_does_not_silently_lose_definition(self) -> None:
        client = ParsedFixtureClient([], [3, 24, 2, 0, 3], function_ast(3, 3))
        client._ast_provider = False
        with self.assertRaisesRegex(layout.LayoutSetupError, "AST"):
            client.definition_lines(Path("/workspace/Source/Friend.cpp"), "")

    def test_file_header_hyphen_envelope_is_not_a_decorative_ruler(self) -> None:
        lines = [*file_header("Empty.cpp"), "", "#include <metal>"]
        self.assertEqual(layout.check_layout(Path("Source/Empty.cpp"), "\n".join(lines), set()), [])

    def test_hyphen_ruler_outside_file_header_is_rejected(self) -> None:
        lines = [*file_header("Empty.cpp"), "", layout.FILE_RULER]
        errors = layout.check_layout(Path("Source/Empty.cpp"), "\n".join(lines), set())
        self.assertTrue(any("alternate" in error for error in errors))

    def test_function_and_rationale_comment_claim_one_separator(self) -> None:
        text = "\n".join([separator(), "// Explains a lifetime invariant.", "void draw() {}"])
        self.assertEqual(layout.check_layout(Path("Source/Draw.cpp"), text, {2}), [])

    def test_indented_inline_definition_keeps_physical_width(self) -> None:
        text = "\n".join(["class Local {", "public:", separator("    "), "    void run() {}", "};"])
        self.assertEqual(layout.check_layout(Path("Tests/Local.cpp"), text, {3}), [])
        self.assertEqual(len(text.splitlines()[2]), 120)

    def test_template_prefix_claims_the_function_separator(self) -> None:
        text = "\n".join([separator(), "template <typename T>", "T load() { return {}; }"])
        self.assertEqual(layout.check_layout(Path("Source/Load.cpp"), text, {2}), [])

    def test_missing_orphan_malformed_and_alternate_rulers_are_reported(self) -> None:
        source = "\n".join(
            [
                "void missing() {}",
                "",
                "//==========",
                "",
                separator(),
                "//---------- old section ----------",
            ]
        )
        errors = layout.check_layout(Path("Source/Broken.cpp"), source, {0})
        self.assertTrue(any("missing" in error for error in errors))
        self.assertTrue(any("alternate" in error for error in errors))
        self.assertTrue(any("orphan" in error for error in errors))

    def test_empty_translation_unit_is_valid(self) -> None:
        self.assertEqual(layout.check_layout(Path("Source/Empty.cpp"), "#include <metal>\n", set()), [])

    def test_catch_macro_symbols_at_one_line_are_deduplicated(self) -> None:
        uri = "file:///workspace/Tests/Example.cpp"
        symbols = [
            {
                "kind": 12,
                "name": "CATCH2_INTERNAL_TEST_0",
                "location": {"uri": uri, "range": {"start": {"line": 7}, "end": {"line": 7}}},
            },
            {
                "kind": 12,
                "name": "CATCH2_INTERNAL_TEST_0",
                "location": {"uri": uri, "range": {"start": {"line": 7}, "end": {"line": 9}}},
            },
            {"kind": 13, "location": {"uri": uri, "range": {"start": {"line": 9}}}},
        ]
        self.assertEqual(layout.definition_start_lines(symbols, uri, set()), {7})

    def test_plain_declaration_is_not_treated_as_a_definition(self) -> None:
        uri = "file:///workspace/Source/Example.cpp"
        symbols = [
            {
                "kind": 12,
                "name": "declaredOnly",
                "location": {
                    "uri": uri,
                    "range": {"start": {"line": 3}, "end": {"line": 3}},
                },
            },
            {
                "kind": 12,
                "name": "defined",
                "location": {
                    "uri": uri,
                    "range": {"start": {"line": 5}, "end": {"line": 8}},
                },
            },
        ]
        self.assertEqual(layout.definition_start_lines(symbols, uri, {5}), {5})

    def test_semantic_definition_tokens_are_delta_decoded(self) -> None:
        # line delta, column delta, length, token type, modifier bitset
        data = [2, 4, 3, 1, 0, 3, 1, 5, 3, 2, 0, 8, 4, 3, 2]
        self.assertEqual(layout.semantic_definition_lines(data, definition_modifier=1), {5})

    def test_stale_compilation_database_has_actionable_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "Source" / "New.cpp"
            source.parent.mkdir()
            source.write_text("void newFunction() {}\n", encoding="utf-8")
            database = root / "compile_commands.json"
            database.write_text(json.dumps([]), encoding="utf-8")
            with self.assertRaisesRegex(layout.LayoutSetupError, "rerun.*compile_commands"):
                layout.ensure_database_covers([source], database, root)


if __name__ == "__main__":
    unittest.main()
