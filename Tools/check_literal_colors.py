#!/usr/bin/env python3
"""Require semantic palette colors in editor panel and shell sources."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIRS = ("Source/App/Panels", "Source/App/Shell")
SOURCE_SUFFIXES = {".h", ".cpp", ".mm"}
NON_CODE = re.compile(
    r'R"([^ ()\\\t\r\n]{0,16})\([\s\S]*?\)\1"'
    r'|//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"'
    r"|(?<![\w])(?:u8|u|U|L)?'(?:\\.|[^'\\])*'",
)
FLOAT_SUFFIX = r"(?:[fF](?:16|32|64|128)?|[lL]|bf16|BF16)"
INTEGER_SUFFIX = r"[uUlLzZ]*"
NUMBER = re.compile(
    rf"[+-]?(?:0[xX](?:[0-9a-fA-F]+(?:\.[0-9a-fA-F]*)?|\.[0-9a-fA-F]+)"
    rf"[pP][+-]?[0-9]+(?:{FLOAT_SUFFIX})?|0[xX][0-9a-fA-F]+{INTEGER_SUFFIX}|"
    rf"0[bB][01]+{INTEGER_SUFFIX}|"
    rf"(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?"
    rf"(?:{FLOAT_SUFFIX}|{INTEGER_SUFFIX}))\Z"
)
QUALIFIERS = r"(?:\s+(?:const|volatile))*"
VECTOR = re.compile(
    rf"\bImVec4{QUALIFIERS}\s*" r"([({])"
)
CONTROL_PREFIXES = {"if", "else", "for", "while", "switch", "catch", "do", "try"}
NON_FUNCTION_NAMES = CONTROL_PREFIXES | {
    "constexpr", "consteval", "noexcept", "requires", "decltype", "sizeof", "alignof",
    "alignas", "static_cast", "dynamic_cast", "reinterpret_cast", "const_cast", "typeid",
    "__attribute__", "__declspec",
}
TOKENS = re.compile(r"[A-Za-z_]\w*|::|->|[^\s]")


@dataclass(frozen=True)
class Token:
    text: str
    offset: int


class ColorType(IntEnum):
    NONE = 0
    VECTOR = 1
    ARRAY = 2


def code_only(source: str) -> str:
    """Blank comments and quoted text while retaining diagnostic line numbers."""
    return NON_CODE.sub(lambda match: re.sub(r"[^\n]", " ", match.group()), source)


def closing_delimiter(source: str, start: int) -> int | None:
    pairs = {"(": ")", "{": "}"}
    stack = [pairs[source[start]]]
    for index in range(start + 1, len(source)):
        token = source[index]
        if token in pairs:
            stack.append(pairs[token])
        elif token in ")}":
            if token != stack.pop():
                return None
            if not stack:
                return index
    return None


def unwrapped_initializer(expression: str) -> str:
    """Remove whole-expression list/parenthesis wrappers without evaluating expressions."""
    expression = expression.strip()
    while expression and expression[0] in "({":
        closing = closing_delimiter(expression, 0)
        if closing != len(expression) - 1:
            break
        expression = expression[1:-1].strip()
    return expression


def nonzero_numeric_color(arguments: str) -> bool:
    arguments = unwrapped_initializer(arguments)
    channels = [unwrapped_initializer(channel).replace("'", "") for channel in arguments.split(",")]
    if channels and not channels[-1]:
        channels.pop()
    if len(channels) != 4 or not all(NUMBER.fullmatch(channel) for channel in channels):
        return False
    for channel in channels:
        if re.match(r"[+-]?0[xX]", channel):
            if re.search(r"[pP]", channel):
                value = float.fromhex(re.sub(FLOAT_SUFFIX + r"$", "", channel))
            else:
                value = int(re.sub(r"[uUlLzZ]+$", "", channel), 16)
        elif re.match(r"[+-]?0[bB]", channel):
            value = int(re.sub(r"[uUlLzZ]+$", "", channel), 2)
        else:
            value = float(re.sub(r"[uUlLzZ]+$", "", re.sub(FLOAT_SUFFIX + r"$", "", channel)))
        if value != 0:
            return True
    return False


def numeric_array_elements(code: str, opening: int, closing: int) -> list[int]:
    """Check direct anonymous aggregate clauses, keeping named/data expressions opaque."""
    tokens = [Token(match.group(), opening + 1 + match.start())
              for match in TOKENS.finditer(code[opening + 1 : closing])]
    pairs = token_pairs(tokens)
    elements: list[int] = []

    def visit(start: int, end: int) -> None:
        clause = start
        index = start
        while index <= end:
            if index == end or tokens[index].text == ",":
                # Anonymous list clauses are vector elements or aggregate wrappers.
                # A call or named construction owns every delimiter inside its clause.
                if clause < index and tokens[clause].text == "{" and pairs.get(clause) == index - 1:
                    brace = tokens[clause].offset
                    close = tokens[index - 1].offset
                    if nonzero_numeric_color(code[brace + 1 : close]):
                        elements.append(brace)
                    else:
                        visit(clause + 1, index - 1)
                clause = index + 1
            elif tokens[index].text in {"(", "[", "{"} and index in pairs:
                index = pairs[index] + 1
                continue
            elif (tokens[index].text == "<" and index > clause
                  and re.fullmatch(r"[A-Za-z_]\w*", tokens[index - 1].text)):
                template = template_end(tokens, pairs, index)
                if template is not None and template < end:
                    index = template + 1
                    continue
            index += 1

    visit(0, len(tokens))
    return elements


def token_pairs(tokens: list[Token]) -> dict[int, int]:
    """Match balanced code delimiters, including captures with nested initializers."""
    matching = {"(": ")", "[": "]", "{": "}"}
    stack: list[int] = []
    pairs: dict[int, int] = {}
    for index, token in enumerate(tokens):
        if token.text in matching:
            stack.append(index)
        elif token.text in matching.values() and stack:
            opening = stack[-1]
            if matching[tokens[opening].text] == token.text:
                stack.pop()
                pairs[opening] = index
                pairs[index] = opening
    # Template defaults are declaration syntax, not assignment expressions. Register
    # explicit template lists so both forward and backward header walks skip them.
    for index, token in enumerate(tokens[:-1]):
        if token.text == "template" and tokens[index + 1].text == "<":
            closing = template_end(tokens, pairs, index + 1)
            if closing is not None:
                pairs[index + 1] = closing
                pairs[closing] = index + 1
    return pairs


def template_end(tokens: list[Token], pairs: dict[int, int], opening: int) -> int | None:
    """Read a lambda template list without interpreting comparisons inside parentheses."""
    depth = 0
    index = opening
    while index < len(tokens):
        text = tokens[index].text
        if text in {"(", "[", "{"} and index in pairs:
            index = pairs[index] + 1
            continue
        if text == "<":
            depth += 1
        elif text == ">":
            depth -= 1
            if depth == 0:
                return index
        elif text == ";":
            return None
        index += 1
    return None


def signature_tokens(tokens: list[Token], pairs: dict[int, int], start: int, end: int) -> list[int]:
    """Return top-level header tokens, keeping nested arguments out of scope decisions."""
    result: list[int] = []
    index = start
    while index < end:
        result.append(index)
        if (tokens[index].text == "<" and index > start
                and re.fullmatch(r"[A-Za-z_]\w*", tokens[index - 1].text)
                and tokens[index - 1].text != "operator"):
            closing = template_end(tokens, pairs, index)
            if closing is not None and closing < end:
                pairs[index] = closing
                pairs[closing] = index
        if tokens[index].text in {"(", "[", "{", "<"} and index in pairs:
            index = pairs[index] + 1
        else:
            index += 1
    return result


def explicit_color_type(tokens: list[Token], pairs: dict[int, int], start: int, end: int) -> ColorType:
    """Recognize an explicit vector or array element type, including cv/global qualifiers."""
    while start < end and tokens[start].text in {"const", "volatile", "::"}:
        start += 1
    while start < end and tokens[end - 1].text in {"const", "volatile", "&", "*"}:
        end -= 1
    if start >= end:
        return ColorType.NONE
    if tokens[start].text == "ImVec4" and start + 1 == end:
        return ColorType.VECTOR
    if (tokens[start].text == "std" and start + 1 < end
            and tokens[start + 1].text == "::"):
        start += 2
    if start + 2 >= end or tokens[start].text != "array" or tokens[start + 1].text != "<":
        return ColorType.NONE
    closing = template_end(tokens, pairs, start + 1)
    if closing != end - 1:
        return ColorType.NONE
    comma = next((index for index in range(start + 2, closing)
                  if tokens[index].text == ","), None)
    if comma is not None and explicit_color_type(tokens, pairs, start + 2, comma) == ColorType.VECTOR:
        return ColorType.ARRAY
    return ColorType.NONE


def trailing_color_type(tokens: list[Token], pairs: dict[int, int], indices: list[int]) -> ColorType:
    for position, index in enumerate(indices):
        if tokens[index].text != "->":
            continue
        if index > 0 and tokens[index - 1].text == "operator":
            continue
        tail = indices[position + 1 :]
        if not tail:
            return ColorType.NONE
        end = pairs.get(tail[-1], tail[-1]) + 1
        end = next((item for item in tail if tokens[item].text in {"try", "requires"}), end)
        return explicit_color_type(tokens, pairs, index + 1, end)
    return ColorType.NONE


def lambda_body(tokens: list[Token], pairs: dict[int, int], capture: int) -> int | None:
    closing = pairs.get(capture)
    if closing is None or (capture > 0 and tokens[capture - 1].text == "["):
        return None
    if capture > 0:
        previous = tokens[capture - 1].text
        if previous in {")", "]", "}"} or (
            re.fullmatch(r"\w+", previous) and previous not in {"return", "co_return", "co_await", "throw"}
        ):
            return None
    index = closing + 1
    if index >= len(tokens) or tokens[index].text not in {
        "<", "(", "[", "mutable", "constexpr", "consteval", "static", "noexcept", "->", "requires", "{"
    }:
        return None
    while index < len(tokens):
        text = tokens[index].text
        if text in {"(", "["} and index in pairs:
            index = pairs[index] + 1
            continue
        if text == "<":
            end = template_end(tokens, pairs, index)
            if end is None:
                return None
            index = end + 1
            continue
        if text == "{":
            return index
        if text in {";", "=", ")", "]", "}"}:
            return None
        index += 1
    return None


def header_start(tokens: list[Token], pairs: dict[int, int], body: int) -> int:
    index = body - 1
    while index >= 0:
        text = tokens[index].text
        if text in {")", "]", ">"} and index in pairs:
            index = pairs[index] - 1
        elif text in {";", "{", "}"}:
            break
        else:
            index -= 1
    return index + 1


def declaration_initializers(code: str) -> list[tuple[int, int, bool]]:
    """Keep an explicit vector type through declarators, skipping nested initializer commas."""
    tokens = [Token(match.group(), match.start()) for match in TOKENS.finditer(code)]
    pairs = token_pairs(tokens)
    initializers: dict[int, tuple[int, bool]] = {}
    for type_index, token in enumerate(tokens):
        if token.text not in {"ImVec4", "array"}:
            continue
        index = type_index + 1
        aggregate_array = False
        if token.text == "array":
            if index >= len(tokens) or tokens[index].text != "<":
                continue
            end = template_end(tokens, pairs, index)
            if end is None or explicit_color_type(tokens, pairs, type_index, end + 1) != ColorType.ARRAY:
                continue
            type_start = type_index
            while type_start > 0:
                if (type_start >= 2 and tokens[type_start - 1].text == "::"
                        and tokens[type_start - 2].text == "std"):
                    type_start -= 2
                elif tokens[type_start - 1].text in {"const", "volatile", "::"}:
                    type_start -= 1
                else:
                    break
            # A trailing return type's following brace is the callable body.
            if type_start > 0 and tokens[type_start - 1].text == "->":
                continue
            index = end + 1
            aggregate_array = True
        while index < len(tokens) and tokens[index].text in {"const", "volatile"}:
            index += 1
        if token.text == "ImVec4" and index in pairs and tokens[index].text == "[":
            type_start = type_index
            while type_start > 0 and tokens[type_start - 1].text in {"const", "volatile", "::"}:
                type_start -= 1
            if type_start > 0 and tokens[type_start - 1].text == "new":
                while index in pairs and tokens[index].text == "[":
                    index = pairs[index] + 1
                if index in pairs and tokens[index].text == "{":
                    initializers[tokens[index].offset] = (tokens[pairs[index]].offset, True)
            continue
        if aggregate_array and index in pairs and tokens[index].text == "{":
            initializers[tokens[index].offset] = (tokens[pairs[index]].offset, True)
            continue
        while index < len(tokens):
            while index < len(tokens) and tokens[index].text in {"*", "&", "const", "volatile"}:
                index += 1
            if (index >= len(tokens) or not re.fullmatch(r"[A-Za-z_]\w*", tokens[index].text)
                    or tokens[index].text in {"operator", "return", "requires"}):
                break
            index += 1
            is_array = aggregate_array
            while index in pairs and tokens[index].text == "[":
                if index + 1 >= len(tokens) or tokens[index + 1].text != "[":
                    is_array = True
                index = pairs[index] + 1
            assigned = index < len(tokens) and tokens[index].text == "="
            if assigned:
                index += 1
            if index in pairs and tokens[index].text in {"{", "("}:
                opening = index
                index = pairs[index] + 1
                initializers[tokens[opening].offset] = (tokens[pairs[opening]].offset, is_array)
                # A function definition owns a body, not another variable initializer.
                if (not assigned and tokens[opening].text == "(" and index < len(tokens)
                        and tokens[index].text not in {",", ";", ")", "]"}):
                    break
            while index < len(tokens) and tokens[index].text not in {",", ";", ")", "]", "}"}:
                if tokens[index].text in {"(", "[", "{"} and index in pairs:
                    index = pairs[index] + 1
                    continue
                if (tokens[index].text == "<" and index > 0
                        and re.fullmatch(r"[A-Za-z_]\w*", tokens[index - 1].text)):
                    end = template_end(tokens, pairs, index)
                    if end is not None:
                        index = end + 1
                        continue
                index += 1
            if index >= len(tokens) or tokens[index].text != ",":
                break
            index += 1
    return [(opening, closing, array) for opening, (closing, array) in sorted(initializers.items())]


def callable_end(tokens: list[Token], pairs: dict[int, int], body: int, function_try: bool) -> int:
    """Include every function-try handler in its callable's return scope."""
    end = pairs[body]
    if not function_try:
        return end
    while end + 1 < len(tokens) and tokens[end + 1].text == "catch":
        parameter = end + 2
        if parameter not in pairs or tokens[parameter].text != "(":
            break
        handler = pairs[parameter] + 1
        if handler not in pairs or tokens[handler].text != "{":
            break
        end = pairs[handler]
    return end


def function_declarator(tokens: list[Token], pairs: dict[int, int], indices: list[int]) -> tuple[int, ColorType] | None:
    """Find the name and parameters, separating operator names from their argument lists."""
    operator = next((position for position, index in enumerate(indices)
                     if tokens[index].text == "operator"), None)
    if operator is not None:
        # operator() has a name delimiter followed by its actual parameter list;
        # operator[] and symbolic operators have no competing parenthesis.
        after_name = operator + 1
        if after_name < len(indices) and tokens[indices[after_name]].text == "(":
            after_name += 1
        parameter = next((index for index in indices[after_name:]
                          if tokens[index].text == "("), None)
        if parameter is None or any(tokens[index].text == "=" for index in indices[:operator]):
            return None
        name = indices[operator]
        conversion_color = explicit_color_type(tokens, pairs, name + 1, parameter)
        return name, conversion_color
    if any(tokens[index].text == "=" for index in indices):
        return None
    for position, index in enumerate(indices):
        if tokens[index].text != "(" or position == 0:
            continue
        name_position = position - 1
        if tokens[indices[name_position]].text == "<":
            name_position -= 1
        if name_position < 0:
            continue
        name = indices[name_position]
        if re.fullmatch(r"[A-Za-z_]\w*", tokens[name].text) and tokens[name].text not in NON_FUNCTION_NAMES:
            return name, ColorType.NONE
    return None


def callable_bodies(code: str) -> list[tuple[int, int, ColorType]]:
    """Classify callable scopes from full lexical headers, independently of control blocks."""
    tokens = [Token(match.group(), match.start()) for match in TOKENS.finditer(code)]
    pairs = token_pairs(tokens)
    bodies: dict[int, ColorType] = {}
    ends: dict[int, int] = {}
    for index, token in enumerate(tokens):
        if token.text != "[":
            continue
        body = lambda_body(tokens, pairs, index)
        if body is not None and body in pairs:
            bodies[body] = trailing_color_type(tokens, pairs, signature_tokens(tokens, pairs, index, body))
    for body, token in enumerate(tokens):
        if token.text != "{" or body not in pairs or body in bodies:
            continue
        start = header_start(tokens, pairs, body)
        indices = signature_tokens(tokens, pairs, start, body)
        # Attributes belong to the statement they prefix, including control-flow statements.
        while indices and tokens[indices[0]].text == "[":
            indices.pop(0)
        if not indices or tokens[indices[0]].text in CONTROL_PREFIXES | {"return", "co_return"}:
            continue
        declarator = function_declarator(tokens, pairs, indices)
        if declarator is None:
            continue
        name, conversion_color = declarator
        while name >= start + 2 and tokens[name - 1].text == "::":
            qualifier = name - 2
            if tokens[qualifier].text == ">" and qualifier in pairs:
                qualifier = pairs[qualifier] - 1
            name = qualifier
        result_type = name - 1
        while result_type >= start and tokens[result_type].text in {"const", "volatile", "&", "*"}:
            result_type -= 1
        explicit_color = ColorType.NONE
        if result_type >= start and tokens[result_type].text == "ImVec4":
            explicit_color = ColorType.VECTOR
        elif result_type in pairs and tokens[result_type].text == ">":
            explicit_color = explicit_color_type(tokens, pairs, pairs[result_type] - 1, result_type + 1)
        bodies[body] = explicit_color or conversion_color or trailing_color_type(tokens, pairs, indices)
        ends[body] = callable_end(tokens, pairs, body, any(tokens[index].text == "try" for index in indices))
    return [(tokens[body].offset, tokens[ends.get(body, pairs[body])].offset, color)
            for body, color in sorted(bodies.items())]


def check_text(source: str, path: str) -> list[str]:
    """Check literal colors and require the shared neutral helper for framed headers."""
    code = code_only(source)
    bodies = callable_bodies(code)
    findings: dict[int, str] = {}
    for match in re.finditer(r"\b(IM_COL32|ImColor)\s*[({]", code):
        findings[match.start()] = match.group(1)
    for match in VECTOR.finditer(code):
        opening = match.end() - 1
        closing = closing_delimiter(code, opening)
        if closing is None:
            continue
        if nonzero_numeric_color(code[opening + 1 : closing]):
            findings[opening] = "ImVec4"
    for opening, closing, is_array in declaration_initializers(code):
        if is_array:
            for brace in numeric_array_elements(code, opening, closing):
                findings[brace] = "ImVec4 array element"
        elif nonzero_numeric_color(code[opening + 1 : closing]):
            findings[opening] = "ImVec4"
    for opening, closing, color_type in bodies:
        if not color_type:
            continue
        nested = [(start, end) for start, end, _ in bodies if opening < start < end < closing]
        for match in re.finditer(r"\breturn\s*\{", code[opening + 1 : closing]):
            start = opening + 1 + match.start()
            if any(begin < start < end for begin, end in nested):
                continue
            brace = opening + match.end()
            end = closing_delimiter(code, brace)
            if end is not None:
                if color_type == ColorType.ARRAY:
                    for element in numeric_array_elements(code, brace, end):
                        findings[element] = "ImVec4 array return element"
                elif nonzero_numeric_color(code[brace + 1 : end]):
                    findings[start] = "ImVec4 return"
    errors = []
    if Path(path).as_posix() != "Source/App/Panels/Shared/EditorStyle.cpp":
        for match in re.finditer(r"\bImGui\s*::\s*CollapsingHeader\s*\(", code):
            line = code.count("\n", 0, match.start()) + 1
            errors.append(f"{path}:{line}: use editor_style::collapsingHeader for neutral headers")
        for match in re.finditer(r"\bImGuiTreeNodeFlags_Framed\b", code):
            line = code.count("\n", 0, match.start()) + 1
            errors.append(
                f"{path}:{line}: ImGuiTreeNodeFlags_Framed fills with the selection color; "
                "use editor_style::collapsingHeader for neutral headers"
            )
    return errors + [
        f"{path}:{code.count(chr(10), 0, offset) + 1}: {construct} must use an editor theme role"
        for offset, construct in sorted(findings.items())
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="Repository root to check")
    args = parser.parse_args()
    errors: list[str] = []
    for directory in SOURCE_DIRS:
        for path in sorted((args.root / directory).rglob("*")):
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                errors.extend(check_text(path.read_text(encoding="utf-8"), str(path.relative_to(args.root))))
    if errors:
        print("\n".join(errors), file=sys.stderr)
        print(f"Literal color check failed ({len(errors)} violations)", file=sys.stderr)
        return 1
    print("Literal color check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
