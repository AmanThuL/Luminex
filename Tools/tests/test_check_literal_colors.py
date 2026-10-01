from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from Tools.check_literal_colors import check_text


ROOT = Path(__file__).resolve().parents[2]


class LiteralColorTests(unittest.TestCase):
    def test_collapsing_headers_require_the_shared_neutral_helper(self) -> None:
        for expression in (
            'ImGui::CollapsingHeader("Topic");',
            '::ImGui /* namespace */ :: CollapsingHeader ("Topic", flags);',
            'ImGui::\nCollapsingHeader("Topic");',
        ):
            with self.subTest(expression=expression):
                errors = check_text(expression, "Source/App/Panels/Panel.cpp")
                self.assertEqual(len(errors), 1)
                self.assertIn("editor_style::collapsingHeader", errors[0])
                self.assertEqual(
                    check_text(expression, "Source/App/Panels/Shared/EditorStyle.cpp"), []
                )
        for source in (
            '// ImGui::CollapsingHeader("Topic");',
            'const char* text = "ImGui::CollapsingHeader(";',
            'editor_style::collapsingHeader("Topic");',
        ):
            self.assertEqual(check_text(source, "Panel.cpp"), [])
        self.assertEqual(
            len(check_text('ImGui::CollapsingHeader("Topic");', "Elsewhere/EditorStyle.cpp")), 1
        )

    def test_framed_tree_nodes_require_the_shared_neutral_helper(self) -> None:
        for expression in (
            'ImGui::TreeNodeEx("Topic", ImGuiTreeNodeFlags_Framed);',
            "const ImGuiTreeNodeFlags flags =\n    ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed;",
        ):
            with self.subTest(expression=expression):
                errors = check_text(expression, "Source/App/Panels/Panel.cpp")
                self.assertEqual(len(errors), 1)
                self.assertIn("ImGuiTreeNodeFlags_Framed", errors[0])
                self.assertIn("editor_style::collapsingHeader", errors[0])
                self.assertEqual(
                    check_text(expression, "Source/App/Panels/Shared/EditorStyle.cpp"), []
                )
        self.assertIn(
            "Panel.cpp:2:", check_text("int a;\nauto f = ImGuiTreeNodeFlags_Framed;", "Panel.cpp")[0]
        )
        for source in (
            "// ImGuiTreeNodeFlags_Framed",
            'const char* text = "ImGuiTreeNodeFlags_Framed";',
            "ImGuiTreeNodeFlags_FramePadding",
            "MyImGuiTreeNodeFlags_Framed",
        ):
            self.assertEqual(check_text(source, "Panel.cpp"), [])

    def test_packed_and_imcolor_calls_are_rejected(self) -> None:
        for expression in (
            "IM_COL32(0, 0, 0, 0)",
            "IM_COL32 (255, 80, 90, 255)",
            "ImColor(paletteColor)",
            "ImColor {0, 0, 0, 0}",
        ):
            with self.subTest(expression=expression):
                self.assertEqual(len(check_text(expression, "Panel.cpp")), 1)

    def test_numeric_vectors_are_rejected_in_every_initialization_form(self) -> None:
        for expression in (
            "ImVec4(0.1f, 0.2f, 0.3f, 1.f)",
            "ImVec4{1, 0, 0, 1}",
            "const ImVec4 ink{.1f, 2e-1F, +.3f, 1.0f};",
            "ImVec4 ink = {0xFF, 0, 0, 255u};",
            "ImVec4 ink(0, 0, 0, 1);",
            "ImVec4 ink() { if (ready) { return {1, 0, 0, 1}; } return {}; }",
        ):
            with self.subTest(expression=expression):
                self.assertEqual(len(check_text(expression, "Panel.cpp")), 1)

    def test_transparent_sentinels_and_data_colors_are_allowed(self) -> None:
        source = """
            ImVec4(0.0f, -0.0f, 0e0f, 0);
            const ImVec4 transparent{0, 0, 0, 0};
            ImVec4 empty = {0.f, 0.F, .0f, 0x0};
            ImVec4 rgba(data.r, data.g, data.b, 1.0f);
            ImVec4 color() { return {c.r, c.g, c.b, c.a}; }
            ImVec4 emptyColor() { return {0, 0, 0, 0}; }
        """
        self.assertEqual(check_text(source, "Panel.cpp"), [])

    def test_trailing_list_commas_keep_four_color_channels(self) -> None:
        for declaration in (
            "auto ink = ImVec4{%s};",
            "ImVec4 ink{%s};",
            "const ImVec4 ink = {%s};",
            "ImVec4 ink({%s});",
            "ImVec4 ink() { return {%s}; }",
            "ImVec4 inks[] = {{%s},};",
            "std::array<ImVec4, 1> inks{{{%s},}};",
            "std::array<ImVec4, 1> inks() { return {{{%s},}}; }",
        ):
            for channels, count in (("1, 0, 0, 1", 1), ("0, 0, 0, 0", 0), ("r, g, b, a", 0)):
                for suffix in (",", ", \n "):
                    source = declaration % (channels + suffix)
                    with self.subTest(source=source):
                        self.assertEqual(len(check_text(source, "Panel.cpp")), count)

    def test_trailing_list_commas_keep_data_expressions_opaque(self) -> None:
        data = "sampleColor(std::array<int, 4>{1, 2, 3, 4,})"
        for declaration in (
            "ImVec4 inks[]{%s,};",
            "std::array<ImVec4, 1> inks{{%s,}};",
            "std::array<ImVec4, 1> inks() { return {{%s,}}; }",
        ):
            with self.subTest(declaration=declaration):
                self.assertEqual(check_text(declaration % data, "Panel.cpp"), [])

    def test_cli_rejects_trailing_comma_color_on_its_source_line(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "Source/App/Panels/Probe.cpp"
            path.parent.mkdir(parents=True)
            path.write_text("#include <imgui.h>\n\n// Fixed editor color\nImVec4 ink{1, 0, 0, 1,};\n", encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(ROOT / "Tools/check_literal_colors.py"), "--root", str(root)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 1)
            self.assertIn("Source/App/Panels/Probe.cpp:4:", result.stderr)
            self.assertIn("ImVec4", result.stderr)

    def test_postfix_qualifiers_and_typed_arrays_are_rejected(self) -> None:
        for source in (
            "ImVec4 const ink{1, 0, 0, 1};",
            "ImVec4 const ink = {1, 0, 0, 1};",
            "const ImVec4 inks[] = {{1, 0, 0, 1}};",
            "ImVec4 const inks[2] = {{0, 0, 0, 0}, {1, 0, 0, 1}};",
            "std::array<ImVec4, 2> inks = {{{0, 0, 0, 0}, {1, 0, 0, 1}}};",
        ):
            with self.subTest(source=source):
                self.assertEqual(len(check_text(source, "Panel.cpp")), 1)
        self.assertEqual(
            len(check_text("ImVec4 inks[] = {{1, 0, 0, 1}, {0, 1, 0, 1}};", "Panel.cpp")),
            2,
        )

    def test_qualified_and_trailing_color_returns_are_rejected(self) -> None:
        for source in (
            "ImVec4 Panel::ink() const { return {1, 0, 0, 1}; }",
            "ImVec4 const Panel::ink() const noexcept { return {1, 0, 0, 1}; }",
            "auto ink() -> ImVec4 { return {1, 0, 0, 1}; }",
            "auto Panel::ink() const -> ImVec4 { return {1, 0, 0, 1}; }",
        ):
            with self.subTest(source=source):
                self.assertEqual(len(check_text(source, "Panel.cpp")), 1)

    def test_nested_noncolor_callable_returns_are_allowed(self) -> None:
        source = """
            ImVec4 ink() {
                auto rect = []() -> std::array<int, 4> { return {1, 2, 3, 4}; };
                auto nested = [] { return [] { return std::array<int, 4>{1, 2, 3, 4}; }; };
                struct Rect { std::array<int, 4> value() const { return {1, 2, 3, 4}; } };
                if (ready) { return paletteColor; }
                return paletteColor;
            }
        """
        self.assertEqual(check_text(source, "Panel.cpp"), [])

    def test_nested_explicit_color_callable_is_checked_independently(self) -> None:
        for source in (
            "ImVec4 ink() { auto f = []() -> ImVec4 { return {1, 0, 0, 1}; }; return color; }",
            "auto f = []() -> ImVec4 { if (ready) { return {1, 0, 0, 1}; } return color; };",
            "ImVec4 ink() { struct Local { ImVec4 value() { return {1, 0, 0, 1}; } }; return color; }",
        ):
            with self.subTest(source=source):
                self.assertEqual(len(check_text(source, "Panel.cpp")), 1)

    def test_extended_zero_and_data_color_forms_are_allowed(self) -> None:
        source = """
            ImVec4 const transparent{0, 0, 0, 0};
            const ImVec4 inks[] = {{0, 0, 0, 0}, {r, g, b, a}};
            std::array<ImVec4, 2> colors = {{{0, 0, 0, 0}, {r, g, b, a}}};
            ImVec4 Panel::ink() const { return {r, g, b, a}; }
            auto ink() -> ImVec4 { return {0, 0, 0, 0}; }
            auto f = []() -> ImVec4 { return {r, g, b, a}; };
        """
        self.assertEqual(check_text(source, "Panel.cpp"), [])

    def test_complete_control_headers_keep_color_returns_in_the_owner(self) -> None:
        for branch in (
            "if constexpr (true)",
            "if consteval",
            "if (const auto value = ready(); value)",
            "else if constexpr (true)",
            "while (ready)",
            "for (int i = 0; i < 1; ++i)",
            "switch (value)",
            "do",
            "try",
            "catch (const Error& error)",
            "[[likely]] if constexpr (true)",
        ):
            prefix = "if (!ready) return color;" if branch.startswith("else") else ""
            suffix = "while (ready);" if branch == "do" else ""
            if branch == "try":
                suffix = "catch (...) { return color; }"
            elif branch.startswith("catch"):
                prefix = "try { return color; }"
            source = f"ImVec4 ink() {{ {prefix} {branch} {{ return {{1, 0, 0, 1}}; }} {suffix} return color; }}"
            with self.subTest(branch=branch):
                self.assertEqual(len(check_text(source, "Panel.cpp")), 1)

    def test_template_lambda_data_returns_are_not_outer_color_returns(self) -> None:
        for signature in (
            "[]<typename T>() -> std::array<int, 4>",
            "[]<typename T = std::array<int, 4>>() -> std::array<int, 4>",
            "[]<typename T, template<typename> class C>() -> std::array<int, 4>",
            "[rect = std::array<int, 4>{1, 2, 3, 4}]() -> std::array<int, 4>",
        ):
            source = f"ImVec4 ink() {{ auto rect = {signature} {{ return {{1, 2, 3, 4}}; }}; return color; }}"
            with self.subTest(signature=signature):
                self.assertEqual(check_text(source, "Panel.cpp"), [])

    def test_template_lambda_color_returns_are_checked_separately(self) -> None:
        for signature in (
            "[]<typename T>() -> ImVec4",
            "[]<typename T = std::array<int, 4>>() constexpr noexcept -> ImVec4",
        ):
            source = f"ImVec4 ink() {{ auto rect = {signature} {{ if constexpr (true) {{ return {{1, 0, 0, 1}}; }} return color; }}; return color; }}"
            with self.subTest(signature=signature):
                self.assertEqual(len(check_text(source, "Panel.cpp")), 1)

    def test_template_lambda_zero_and_data_returns_are_allowed(self) -> None:
        source = """
            ImVec4 ink() {
                auto zero = []<typename T>() -> ImVec4 { return {0, 0, 0, 0}; };
                auto data = []<typename T>() -> ImVec4 { return {r, g, b, a}; };
                return paletteColor;
            }
        """
        self.assertEqual(check_text(source, "Panel.cpp"), [])

    def test_subscripted_calls_in_controls_are_not_lambda_captures(self) -> None:
        for condition in ("callbacks[index]()", "callbacks[0]()", "factory()[index]()"):
            source = f"ImVec4 ink() {{ if ({condition}) {{ return {{1, 0, 0, 1}}; }} return color; }}"
            with self.subTest(condition=condition):
                self.assertEqual(len(check_text(source, "Panel.cpp")), 1)

    def test_comments_and_strings_are_not_code(self) -> None:
        source = '''
            // IM_COL32(1, 2, 3, 4)
            /* ImVec4(1, 2, 3, 4) */
            const char* label = "ImColor(1, 2, 3, 4)";
            const char* raw = R"sample(ImVec4(1, 2, 3, 4))sample";
            const char escaped = '\\'';
        '''
        self.assertEqual(check_text(source, "Panel.cpp"), [])

    def test_defaulted_function_templates_keep_explicit_color_return_types(self) -> None:
        for prefix in (
            "template<typename T = int>",
            "template<int N = 4>",
            "template<typename T = std::array<int, 4>>",
            "template<template<typename> class C = Container, typename T = int>",
            "template<int N = (1 < 2 ? 4 : 0)>",
        ):
            for signature in ("ImVec4 ink()", "auto ink() -> ImVec4"):
                for returned, count in (("1, 0, 0, 1", 1), ("0, 0, 0, 0", 0), ("r, g, b, a", 0)):
                    source = f"{prefix} {signature} {{ return {{{returned}}}; }}"
                    with self.subTest(prefix=prefix, signature=signature, returned=returned):
                        self.assertEqual(len(check_text(source, "Panel.cpp")), count)

    def test_operator_callables_own_their_returns(self) -> None:
        for declarator in (
            "operator[](int i) const",
            "operator()(int i) const",
            "operator+(int i) const",
            "operator=(int i)",
            "operator<=>(int i) const",
        ):
            source = f"ImVec4 ink() {{ struct Rect {{ std::array<int, 4> {declarator} {{ return {{1, 2, 3, 4}}; }} }}; return paletteColor; }}"
            with self.subTest(declarator=declarator):
                self.assertEqual(check_text(source, "Panel.cpp"), [])
            for scope in ("", "Rect::"):
                for returned, count in (("1, 0, 0, 1", 1), ("0, 0, 0, 0", 0), ("r, g, b, a", 0)):
                    source = f"ImVec4 {scope}{declarator} {{ return {{{returned}}}; }}"
                    with self.subTest(declarator=declarator, scope=scope, returned=returned):
                        self.assertEqual(len(check_text(source, "Panel.cpp")), count)

    def test_conversion_operators_and_defaulted_local_templates_own_returns(self) -> None:
        source = """
            ImVec4 ink() {
                struct Rect {
                    operator std::array<int, 4>() const { return {1, 2, 3, 4}; }
                    operator Container<ImVec4>() const { return {1, 2, 3, 4}; }
                    template<typename T = int> std::array<int, 4> data() { return {1, 2, 3, 4}; }
                };
                return paletteColor;
            }
        """
        self.assertEqual(check_text(source, "Panel.cpp"), [])
        for returned, count in (("1, 0, 0, 1", 1), ("0, 0, 0, 0", 0), ("r, g, b, a", 0)):
            source = f"Rect::operator ImVec4() const {{ return {{{returned}}}; }}"
            with self.subTest(returned=returned):
                self.assertEqual(len(check_text(source, "Panel.cpp")), count)

    def test_template_qualified_declarators_keep_explicit_color_return_types(self) -> None:
        for signature in (
            "template<typename T = int> ImVec4 Panel<T>::ink() const",
            "template<typename T = int> ImVec4 Panel<T>::operator[](int i) const",
            "template<> ImVec4 ink<int>()",
            "ImVec4 Panel<std::array<int, 4>>::ink() const",
        ):
            for returned, count in (("1, 0, 0, 1", 1), ("0, 0, 0, 0", 0), ("r, g, b, a", 0)):
                source = f"{signature} {{ return {{{returned}}}; }}"
                with self.subTest(signature=signature, returned=returned):
                    self.assertEqual(len(check_text(source, "Panel.cpp")), count)

    def test_assignment_and_calls_are_not_color_callable_declarations(self) -> None:
        source = """
            ImVec4 ink() {
                auto values = factory() + std::array<int, 4>{1, 2, 3, 4};
                if (values.operator[](0)) { return {1, 0, 0, 1}; }
                return paletteColor;
            }
        """
        self.assertEqual(len(check_text(source, "Panel.cpp")), 1)

    def test_cpp_numeric_literal_spellings_are_rejected(self) -> None:
        for source in (
            "ImVec4{0b1u, 0, 0, 1};",
            "ImVec4{0B10UL, 0, 0, 1};",
            "ImVec4{0x1p-1f, 0, 0, 1};",
            "ImVec4{0X1.FP+1L, 0, 0, 1};",
            "ImVec4{1'000, 2'000, 0, 1};",
            "ImVec4{0xFF'FF, 0b1'000, 0, 1};",
            "ImVec4{1z, 0, 0, 1uz};",
            "ImVec4{1.0f32, 0, 0, 1.0BF16};",
        ):
            with self.subTest(source=source):
                self.assertEqual(len(check_text(source, "Panel.cpp")), 1)

    def test_numeric_zero_sentinels_and_character_literals_are_allowed(self) -> None:
        source = r"""
            ImVec4{0b0, -0B00u, 0x0p+0f, 0X.0P-1L};
            ImVec4{0'000, 0x00'00, 0b0'000, 0};
            ImVec4{0z, 0uz, 0.0f32, 0.0BF16};
            ImVec4{data.r, data.g, data.b, 1.0f};
            auto label = 'ImColor(1, 2, 3, 4)';
            auto wide = L'ImVec4{1, 0, 0, 1}';
            auto utf = u8'\'';
        """
        self.assertEqual(check_text(source, "Panel.cpp"), [])

    def test_each_explicit_color_declarator_is_checked(self) -> None:
        for source in (
            "ImVec4 empty{}, ink{1, 0, 0, 1};",
            "const ImVec4 empty{}, ink = {1, 0, 0, 1};",
            "ImVec4 empty, ink{1, 0, 0, 1};",
            "ImVec4 base = paletteColor, ink{1, 0, 0, 1};",
            "ImVec4 base = getColor(1, 2, 3, 4), ink{1, 0, 0, 1};",
            "ImVec4 base = getColor<ImVec4, int>(), ink{1, 0, 0, 1};",
            "ImVec4 base(r, g, b, a), ink(1, 0, 0, 1);",
            "ImVec4 empty{}, ink[2] = {{0, 0, 0, 0}, {1, 0, 0, 1}};",
            "ImVec4 empty[1]{}, ink[1]{{1, 0, 0, 1}};",
            "std::array<ImVec4, 1> empty{}, ink = {{{1, 0, 0, 1}}};",
            "std::array<ImVec4, 1> base = getColors(), ink{{{1, 0, 0, 1}}};",
        ):
            with self.subTest(source=source):
                self.assertEqual(len(check_text(source, "Panel.cpp")), 1)
        source = "ImVec4 a{1, 0, 0, 1}, b[1]{{0, 1, 0, 1}}, c = {0, 0, 1, 1};"
        self.assertEqual(len(check_text(source, "Panel.cpp")), 3)

    def test_declaration_type_does_not_escape_nested_arguments_or_statements(self) -> None:
        source = """
            ImVec4 zero{}, data{r, g, b, a}, transparent(0, 0, 0, 0);
            ImVec4 array[2]{{0, 0, 0, 0}, {r, g, b, a}}, more[1]{{0, 0, 0, 0}};
            std::array<ImVec4, 1> first = getColors(1, 2, 3, 4), second{{{0, 0, 0, 0}}};
            ImVec4 base = getColor(data, std::array<int, 4>{1, 2, 3, 4}), copy = base;
            std::array<int, 4> integers{1, 2, 3, 4};
            ImVec4 function(int a, int b, int c, int d);
            ImVec4 temporary = ImVec4(r, g, b, a);
            int unrelated[] = {1, 2, 3, 4};
        """
        self.assertEqual(check_text(source, "Panel.cpp"), [])

    def test_function_try_handlers_share_explicit_return_ownership(self) -> None:
        for signature in (
            "ImVec4 ink()",
            "auto ink() -> ImVec4",
            "template<typename T = int> ImVec4 ink()",
            "ImVec4 Panel::operator[](int i) const",
        ):
            for returned, count in (("1, 0, 0, 1", 1), ("0, 0, 0, 0", 0), ("r, g, b, a", 0)):
                source = f"{signature} try {{ return paletteColor; }} catch (const Error&) {{ return color; }} catch (...) {{ return {{{returned}}}; }}"
                with self.subTest(signature=signature, returned=returned):
                    self.assertEqual(len(check_text(source, "Panel.cpp")), count)
        source = "ImVec4 ink() try { return {1, 0, 0, 1}; } catch (...) { return {0, 1, 0, 1}; }"
        self.assertEqual(len(check_text(source, "Panel.cpp")), 2)

    def test_nested_data_try_handlers_keep_their_noncolor_owner(self) -> None:
        source = """
            ImVec4 ink() try {
                struct Data {
                    std::array<int, 4> operator[](int i) const try { return values; }
                    catch (...) { return {1, 2, 3, 4}; }
                    std::array<int, 4> value() try { return values; }
                    catch (const Error&) { return {1, 2, 3, 4}; }
                    catch (...) { return {4, 3, 2, 1}; }
                };
                return paletteColor;
            } catch (...) {
                auto data = []() -> std::array<int, 4> {
                    try { return values; } catch (...) { return {1, 2, 3, 4}; }
                };
                return paletteColor;
            }
        """
        self.assertEqual(check_text(source, "Panel.cpp"), [])
        source = "ImVec4 ink() { struct Local { ImVec4 value() try { return color; } catch (...) { return {1, 0, 0, 1}; } }; return paletteColor; }"
        self.assertEqual(len(check_text(source, "Panel.cpp")), 1)

    def test_handler_diagnostic_keeps_the_source_line(self) -> None:
        source = "ImVec4 ink() try {\n return paletteColor;\n} catch (...) {\n return {1, 0, 0, 1};\n}"
        errors = check_text(source, "Panel.cpp")
        self.assertEqual(len(errors), 1)
        self.assertIn("Panel.cpp:4:", errors[0])

    def test_qualified_explicit_array_elements_keep_their_vector_type(self) -> None:
        for element in ("::ImVec4", "const ImVec4", "ImVec4 const", "const ::ImVec4", "volatile ImVec4"):
            for returned, count in (("1, 0, 0, 1", 1), ("0, 0, 0, 0", 0), ("r, g, b, a", 0)):
                for declaration in (
                    "std::array<%s, 1> palette{{{%s}}};",
                    "std::array<%s, 1> empty{}, palette{{{%s}}};",
                    "auto palette = std::array<%s, 1>{{{%s}}};",
                ):
                    source = declaration % (element, returned)
                    with self.subTest(source=source):
                        self.assertEqual(len(check_text(source, "Panel.cpp")), count)

    def test_explicit_vector_array_return_types_are_checked(self) -> None:
        for signature in (
            "std::array<ImVec4, 1> palette()",
            "std::array<const ::ImVec4, 1> palette()",
            "auto palette() -> std::array<ImVec4 const, 1>",
            "auto palette = []() -> std::array<ImVec4, 1>",
            "Palette::operator std::array<ImVec4, 1>() const",
        ):
            for returned, count in (("1, 0, 0, 1", 1), ("0, 0, 0, 0", 0), ("r, g, b, a", 0)):
                source = signature + " { return {{{" + returned + "}}}; }"
                with self.subTest(signature=signature, returned=returned):
                    self.assertEqual(len(check_text(source, "Panel.cpp")), count)
        source = "std::array<ImVec4, 1> palette() try { return values; } catch (...) { return {{{1, 0, 0, 1}}}; }"
        self.assertEqual(len(check_text(source, "Panel.cpp")), 1)

    def test_nested_noncolor_array_returns_are_not_vector_array_elements(self) -> None:
        source = """
            std::array<ImVec4, 1> palette() {
                auto data = []() -> std::array<std::array<int, 4>, 1> { return {{{1, 2, 3, 4}}}; };
                struct Data {
                    std::array<std::array<int, 4>, 1> value() try { return values; }
                    catch (...) { return {{{1, 2, 3, 4}}}; }
                };
                return {{{r, g, b, a}}};
            }
        """
        self.assertEqual(check_text(source, "Panel.cpp"), [])
        source = "ImVec4 ink() { auto palette = []() -> std::array<ImVec4, 1> { return {{{1, 0, 0, 1}}}; }; return color; }"
        self.assertEqual(len(check_text(source, "Panel.cpp")), 1)

    def test_operator_name_arrows_are_separate_from_trailing_return_arrows(self) -> None:
        for declarator in ("operator->() const", "operator->*(int i) const"):
            for color_type, opening, closing in (
                ("ImVec4", "{", "}"),
                ("std::array<ImVec4, 1>", "{{{", "}}}"),
            ):
                for signature in (
                    f"{color_type} Panel::{declarator}",
                    f"auto Panel::{declarator} -> {color_type}",
                ):
                    for returned, count in (("1, 0, 0, 1", 1), ("0, 0, 0, 0", 0), ("r, g, b, a", 0)):
                        source = f"{signature} {{ return {opening}{returned}{closing}; }}"
                        with self.subTest(signature=signature, returned=returned):
                            self.assertEqual(len(check_text(source, "Panel.cpp")), count)

    def test_nested_noncolor_arrow_operators_keep_their_own_returns(self) -> None:
        for declarator in ("operator->() const", "operator->*(int i) const"):
            source = f"""
                std::array<ImVec4, 1> palette() {{
                    struct Data {{
                        auto {declarator} -> std::array<std::array<int, 4>, 1> {{
                            return {{{{{{1, 2, 3, 4}}}}}};
                        }}
                    }};
                    return {{{{{{r, g, b, a}}}}}};
                }}
            """
            with self.subTest(declarator=declarator):
                self.assertEqual(check_text(source, "Panel.cpp"), [])

    def test_array_return_declarators_are_not_anonymous_array_constructors(self) -> None:
        for signature in (
            "auto palette() -> std::array<ImVec4, 1>",
            "auto palette = []() -> std::array<const ::ImVec4, 1>",
        ):
            source = signature + """ {
                auto data = []() -> std::array<std::array<int, 4>, 1> { return {{{1, 2, 3, 4}}}; };
                return values;
            }
            """
            with self.subTest(signature=signature):
                self.assertEqual(check_text(source, "Panel.cpp"), [])

    def test_array_initializers_do_not_reclassify_nested_callable_data(self) -> None:
        source = """
            std::array<ImVec4, 1> colors{{[]() -> ImVec4 {
                auto data = []() -> std::array<int, 4> { return {1, 2, 3, 4}; };
                struct Data {
                    std::array<int, 4> value() try { return data; }
                    catch (...) { return {1, 2, 3, 4}; }
                };
                return paletteColor;
            }()}};
        """
        self.assertEqual(check_text(source, "Panel.cpp"), [])
        source = "std::array<ImVec4, 1> colors{{[]() -> ImVec4 { return {1, 0, 0, 1}; }()}};"
        self.assertEqual(len(check_text(source, "Panel.cpp")), 1)

    def test_array_data_expressions_are_not_vector_subobjects(self) -> None:
        prefix = """
            ImVec4 sampleColor(std::array<int, 4> sample);
            struct Sample { std::array<int, 4> values; operator ImVec4() const; };
            ImVec4 sampleColor(Sample sample);
        """
        for expression in (
            "sampleColor(std::array<int, 4>{1, 2, 3, 4})",
            "sampleColor(std::array<std::array<int, 4>, 1>{{{1, 2, 3, 4}}}[0])",
            "sampleColor(Sample{{1, 2, 3, 4}})",
            "Sample{{1, 2, 3, 4}}",
            "static_cast<ImVec4>(Sample{{1, 2, 3, 4}})",
            "(ready ? sampleColor(std::array<int, 4>{1, 2, 3, 4}) : paletteColor)",
        ):
            for declaration in (
                "std::array<ImVec4, 1> colors{{%s}};",
                "ImVec4 colors[]{%s};",
                "ImVec4 empty{}, colors[]{%s};",
                "std::array<ImVec4, 1> empty{}, colors{{%s}};",
                "std::array<ImVec4, 1> colors() { return {{%s}}; }",
                "auto colors() -> std::array<ImVec4, 1> { return {{%s}}; }",
                "auto colors = []() -> std::array<ImVec4, 1> { return {{%s}}; };",
            ):
                source = prefix + declaration % expression
                with self.subTest(expression=expression, declaration=declaration):
                    self.assertEqual(check_text(source, "Panel.cpp"), [])

    def test_array_data_clauses_do_not_hide_neighboring_literal_elements(self) -> None:
        data = "sampleColor(std::array<int, 4>{1, 2, 3, 4})"
        for element, count in (("{1, 0, 0, 1}", 1), ("{0, 0, 0, 0}", 0), ("{r, g, b, a}", 0)):
            for declaration in (
                "ImVec4 colors[]{%s, %s};",
                "ImVec4 colors[]{%s, %s}, more[]{%s};",
                "std::array<ImVec4, 2> colors{{%s, %s}};",
                "std::array<ImVec4, 2> colors() { return {{%s, %s}}; }",
            ):
                arguments = (data, element, data) if declaration.count("%s") == 3 else (data, element)
                source = declaration % arguments
                with self.subTest(element=element, declaration=declaration):
                    self.assertEqual(len(check_text(source, "Panel.cpp")), count)

    def test_explicit_color_constructors_in_data_expressions_are_checked(self) -> None:
        for element, count in (("1, 0, 0, 1", 1), ("0, 0, 0, 0", 0), ("r, g, b, a", 0)):
            for declaration in (
                "ImVec4 colors[]{sampleColor(ImVec4{%s})};",
                "std::array<ImVec4, 1> colors{{sampleColor(ImVec4{%s})}};",
                "std::array<ImVec4, 1> colors() { return {{sampleColor(ImVec4{%s})}}; }",
            ):
                with self.subTest(element=element, declaration=declaration):
                    self.assertEqual(len(check_text(declaration % element, "Panel.cpp")), count)

    def test_anonymous_new_vector_arrays_are_checked(self) -> None:
        for element, count in (("1, 0, 0, 1", 1), ("0, 0, 0, 0", 0), ("r, g, b, a", 0)):
            for declaration in (
                "auto colors = new ImVec4[1]{{%s}};",
                "auto colors = new const ::ImVec4[1]{{%s}};",
                "auto colors = new ImVec4[2][1]{{{%s}}};",
            ):
                with self.subTest(element=element, declaration=declaration):
                    self.assertEqual(len(check_text(declaration % element, "Panel.cpp")), count)
        source = """
            auto size = sizeof(ImVec4[1]);
            auto sizeWithData = sizeof(ImVec4[1]) + sizeof(std::array<int, 4>{1, 2, 3, 4});
            auto data = new std::array<int, 4>[1]{{1, 2, 3, 4}};
            auto colors = new ImVec4[1]{sampleColor(std::array<int, 4>{1, 2, 3, 4})};
        """
        self.assertEqual(check_text(source, "Panel.cpp"), [])

    def test_cli_allows_array_elements_produced_from_noncolor_data(self) -> None:
        source = """
            ImVec4 sampleColor(std::array<int, 4> sample);
            std::array<ImVec4, 1> colors{{sampleColor(std::array<int, 4>{1, 2, 3, 4})}};
        """
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "Source/App/Panels/Probe.cpp"
            path.parent.mkdir(parents=True)
            path.write_text(source, encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(ROOT / "Tools/check_literal_colors.py"), "--root", str(root)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_wrapped_numeric_constructor_arguments_are_checked(self) -> None:
        for element, count in (("1, 0, 0, 1", 1), ("0, 0, 0, 0", 0), ("r, g, b, a", 0)):
            parenthesized = ", ".join("(" + channel.strip() + ")" for channel in element.split(","))
            for expression in (
                "ImVec4 ink(" + parenthesized + ");",
                "ImVec4 ink({" + element + "});",
                "ImVec4({" + element + "});",
            ):
                with self.subTest(expression=expression):
                    self.assertEqual(len(check_text(expression, "Panel.cpp")), count)
        source = "ImVec4 ink(sampleValue(std::array<int, 4>{1, 2, 3, 4}), g, b, a);"
        self.assertEqual(check_text(source, "Panel.cpp"), [])

    def test_diagnostic_names_file_line_and_construct(self) -> None:
        errors = check_text("// ignored\nImVec4(1, 0, 0, 1);", "Panels/Test.cpp")
        self.assertEqual(len(errors), 1)
        self.assertIn("Panels/Test.cpp:2:", errors[0])
        self.assertIn("ImVec4", errors[0])

    def test_cli_checks_only_panel_and_shell_sources(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in (
                "Source/App/Panels/Test.cpp",
                "Source/App/Shell/Test.mm",
                "Source/App/Model/Test.cpp",
                "Source/Render/Test.cpp",
            ):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("ImColor(data);\n", encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(ROOT / "Tools/check_literal_colors.py"), "--root", str(root)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 1)
            self.assertIn("Source/App/Panels/Test.cpp:1:", result.stderr)
            self.assertIn("Source/App/Shell/Test.mm:1:", result.stderr)
            self.assertNotIn("Source/App/Model", result.stderr)
            self.assertNotIn("Source/Render", result.stderr)


if __name__ == "__main__":
    unittest.main()
