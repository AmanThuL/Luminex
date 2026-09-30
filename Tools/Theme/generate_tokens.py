#!/usr/bin/env python3
"""UX4 token generator: OKLCH primitives -> sRGB, semantic roles per theme, WCAG contrast audit.

Generates EditorThemeTokens.h/.cpp and figma-variables.json without runtime dependencies.
The owner's OKLCH primitive math is preserved. Palette RGB channels are rounded to bytes;
selection blending uses encoded sRGB, matching the editor's SDR ImGui target.
Run --check for read-only freshness validation and --audit for both contrast tables.
"""
import argparse
import json
import math
from pathlib import Path
import re
import sys

# ---------- color math (Björn Ottosson OKLab) ----------
def oklch_to_linear_srgb(L, C, h):
    a = C * math.cos(math.radians(h)); b = C * math.sin(math.radians(h))
    l_ = L + 0.3963377774 * a + 0.2158037573 * b
    m_ = L - 0.1055613458 * a - 0.0638541728 * b
    s_ = L - 0.0894841775 * a - 1.2914855480 * b
    l, m, s = l_ ** 3, m_ ** 3, s_ ** 3
    r = +4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s
    g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s
    bb = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s
    return r, g, bb

def encode(c):
    c = min(max(c, 0.0), 1.0)
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055

def decode(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

def oklch(L, C, h):
    """Return encoded sRGB floats, gamut-clamped by chroma reduction."""
    for _ in range(40):
        r, g, b = oklch_to_linear_srgb(L, C, h)
        if all(-0.0005 <= x <= 1.0005 for x in (r, g, b)):
            break
        C *= 0.94
    return tuple(encode(x) for x in (r, g, b))

def luminance(rgb):
    r, g, b = (decode(x) for x in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b

def contrast(a, b):
    la, lb = luminance(a), luminance(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)

def over(fg, alpha, bg):
    return tuple(f * alpha + b * (1 - alpha) for f, b in zip(fg, bg))

def hexs(rgb):
    return "#%02X%02X%02X" % tuple(round(x * 255) for x in rgb)

# ---------- primitives ----------
HUE_NEUTRAL, HUE_OPERATOR, HUE_AGENT = 255, 248, 302
HUE_SUCCESS, HUE_WARNING, HUE_ERROR = 152, 78, 24

prim = {}
# Neutral "graphite" ramp: cool-tinted grays. 0 = white, 1000 = black end.
neutral_steps = {0: 1.00, 50: 0.985, 100: 0.965, 150: 0.94, 200: 0.905, 300: 0.83, 400: 0.72,
                 500: 0.62, 600: 0.52, 700: 0.42, 750: 0.36, 800: 0.30, 850: 0.26, 900: 0.225,
                 940: 0.20, 950: 0.19, 975: 0.155, 1000: 0.12}
# Near-achromatic (chroma 0.004): a renderer's chrome must not bias color judgment.
for k, L in neutral_steps.items():
    prim[f"neutral/{k}"] = oklch(L, 0.004, HUE_NEUTRAL)

def ramp(name, hue, chroma):
    for k, L in {100: 0.93, 200: 0.87, 300: 0.80, 400: 0.72, 500: 0.62, 600: 0.53, 700: 0.46,
                 800: 0.38, 900: 0.30}.items():
        c = chroma * (0.45 if k <= 100 else 0.7 if k <= 200 else 1.0 if k <= 700 else 0.8)
        prim[f"{name}/{k}"] = oklch(L, c, hue)

ramp("blue", HUE_OPERATOR, 0.15)
ramp("violet", HUE_AGENT, 0.16)
ramp("green", HUE_SUCCESS, 0.13)
ramp("amber", HUE_WARNING, 0.14)
ramp("red", HUE_ERROR, 0.16)

# ---------- semantic roles ----------
def sem(theme):
    d = theme == "dark"
    P = prim
    s = {}
    # surfaces
    s["surface/canvas"]   = P["neutral/1000"] if d else P["neutral/200"]
    s["surface/panel"]    = P["neutral/940"]  if d else P["neutral/0"]
    s["surface/raised"]   = P["neutral/900"]  if d else P["neutral/0"]
    s["surface/sunken"]   = P["neutral/975"]  if d else P["neutral/100"]
    s["surface/hover"]    = P["neutral/850"]  if d else P["neutral/150"]
    s["surface/active"]   = P["neutral/800"]  if d else P["neutral/200"]
    s["surface/overlay"]  = P["neutral/1000"] if d else P["neutral/0"]    # + alpha 0.86
    s["surface/viewport"] = P["neutral/975"]                               # both themes
    # text
    s["text/primary"]     = P["neutral/50"]   if d else P["neutral/950"]
    s["text/secondary"]   = P["neutral/400"]  if d else P["neutral/600"]
    s["text/disabled"]    = P["neutral/600"]  if d else P["neutral/500"]
    s["text/on-accent"]   = P["neutral/1000"] if d else P["neutral/0"]
    # borders
    s["border/subtle"]    = P["neutral/850"]  if d else P["neutral/200"]
    s["border/strong"]    = P["neutral/600"]  if d else P["neutral/500"]
    s["border/focus"]     = P["blue/400"]     if d else P["blue/600"]
    # operator accent (blue)
    s["accent/operator"]        = P["blue/400"] if d else P["blue/600"]
    s["accent/operator-hover"]  = P["blue/300"] if d else P["blue/700"]
    s["accent/operator-active"] = P["blue/200"] if d else P["blue/800"]
    s["accent/operator-text"]   = P["blue/300"] if d else P["blue/700"]
    s["accent/operator-subtle"] = P["blue/400"] if d else P["blue/600"]   # + alpha 0.18
    # agent accent (violet)
    s["accent/agent"]        = P["violet/400"] if d else P["violet/600"]
    s["accent/agent-hover"]  = P["violet/300"] if d else P["violet/700"]
    s["accent/agent-active"] = P["violet/200"] if d else P["violet/800"]
    s["accent/agent-text"]   = P["violet/300"] if d else P["violet/700"]
    s["accent/agent-subtle"] = P["violet/400"] if d else P["violet/600"]  # + alpha 0.18
    # status
    s["status/success"] = P["green/400"] if d else P["green/700"]
    s["status/warning"] = P["amber/400"] if d else P["amber/700"]
    s["status/error"]   = P["red/400"]   if d else P["red/700"]
    s["status/info"]    = s["text/secondary"]
    # provenance / actors
    s["actor/operator"] = s["accent/operator"]
    s["actor/agent"]    = s["accent/agent"]
    s["actor/system"]   = P["neutral/500"] if d else P["neutral/600"]
    s["prov/session"]   = P["amber/400"]   if d else P["amber/700"]
    # selection
    s["selection/bg"]   = s["accent/operator"]          # + alpha 0.28 dark / 0.20 light
    s["selection/text"] = s["text/primary"]
    # editor colors: graph kind title bands (dark: deep bands, light: tints with dark text)
    kinds = {"raster": (248, 0.10), "compute": (302, 0.10), "copy": (165, 0.09), "external": (55, 0.10),
             "group": (85, 0.09), "sink": (75, 0.11)}
    for k, (h, c) in kinds.items():
        s[f"graph/{k}"] = oklch(0.40, c, h) if d else oklch(0.90, c * 0.55, h)
    s["graph/culled"] = oklch(0.32, 0.0, 0) if d else oklch(0.86, 0.0, 0)
    for i, h in enumerate([250, 55, 150, 95, 195, 20, 290, 100]):
        s[f"graph/link-{i}"] = oklch(0.74, 0.10, h) if d else oklch(0.52, 0.12, h)
    s["plot/line"]  = s["accent/operator"]
    s["plot/limit"] = s["status/warning"]
    # viewport overlays sit on the rendered image in both themes
    s["overlay/bounds-candidate"] = (1.0, 0.41, 0.31)
    s["overlay/bounds-selected"]  = (1.0, 0.86, 0.31)
    s["overlay/outline"]          = (76 / 255, 171 / 255, 253 / 255)
    s["overlay/label"]            = (1.0, 0.94, 0.71)
    # Console severity follows status semantics; overlay text stays bright in both themes.
    for severity, role in {"trace": "text/disabled", "debug": "text/secondary",
                           "info": "text/primary", "warn": "status/warning",
                           "error": "status/error", "critical": "status/error"}.items():
        s[f"console/{severity}"] = s[role]
    s["overlay/text"] = prim["neutral/50"]
    return s

ALPHA = {"surface/overlay": 0.86, "accent/operator-subtle": 0.18, "accent/agent-subtle": 0.18,
         "selection/bg": None, "overlay/bounds-candidate": 0.6, "overlay/bounds-selected": 0.9}

# These lists follow the pinned upstream enum order without importing either UI library.
IMGUI_SLOTS = [
    ("Text", "text/primary", 1),
    ("TextDisabled", "text/disabled", 1),
    ("WindowBg", "surface/panel", 1),
    ("ChildBg", "surface/panel", 0),
    ("PopupBg", "surface/raised", 1),
    ("Border", "border/subtle", 1),
    ("BorderShadow", "border/subtle", 0),
    ("FrameBg", "surface/sunken", 1),
    ("FrameBgHovered", "surface/hover", 1),
    ("FrameBgActive", "surface/active", 1),
    ("TitleBg", "surface/canvas", 1),
    ("TitleBgActive", "surface/panel", 1),
    ("TitleBgCollapsed", "surface/canvas", 1),
    ("MenuBarBg", "surface/panel", 1),
    ("ScrollbarBg", "surface/canvas", 1),
    ("ScrollbarGrab", "border/strong", 1),
    ("ScrollbarGrabHovered", "text/disabled", 1),
    ("ScrollbarGrabActive", "text/secondary", 1),
    ("CheckMark", "accent/operator", 1),
    ("CheckboxSelectedBg", "accent/operator-subtle", 1),
    ("SliderGrab", "accent/operator", 1),
    ("SliderGrabActive", "accent/operator-active", 1),
    ("Button", "surface/hover", 1),
    ("ButtonHovered", "surface/active", 1),
    ("ButtonActive", "accent/operator-subtle", 1),
    ("Header", "selection/bg", 1),
    ("HeaderHovered", "surface/hover", 1),
    ("HeaderActive", "surface/active", 1),
    ("Separator", "border/subtle", 1),
    ("SeparatorHovered", "border/strong", 1),
    ("SeparatorActive", "accent/operator", 1),
    ("ResizeGrip", "border/strong", 1),
    ("ResizeGripHovered", "accent/operator-hover", 1),
    ("ResizeGripActive", "accent/operator", 1),
    ("InputTextCursor", "text/primary", 1),
    ("TabHovered", "surface/hover", 1),
    ("Tab", "surface/canvas", 1),
    ("TabSelected", "surface/panel", 1),
    ("TabSelectedOverline", "accent/operator", 1),
    ("TabDimmed", "surface/canvas", 1),
    ("TabDimmedSelected", "surface/panel", 1),
    ("TabDimmedSelectedOverline", "border/strong", 1),
    ("DockingPreview", "accent/operator-subtle", 1),
    ("DockingEmptyBg", "surface/canvas", 1),
    ("PlotLines", "accent/operator", 1),
    ("PlotLinesHovered", "accent/operator-hover", 1),
    ("PlotHistogram", "accent/operator", 1),
    ("PlotHistogramHovered", "accent/operator-hover", 1),
    ("TableHeaderBg", "surface/canvas", 1),
    ("TableBorderStrong", "border/strong", 1),
    ("TableBorderLight", "border/subtle", 1),
    ("TableRowBg", "surface/panel", 0),
    ("TableRowBgAlt", "surface/hover", 0.35),
    ("TextLink", "accent/operator-text", 1),
    ("TextSelectedBg", "selection/bg", 1),
    ("TreeLines", "border/subtle", 1),
    ("DragDropTarget", "accent/operator", 1),
    ("DragDropTargetBg", "accent/operator-subtle", 1),
    ("UnsavedMarker", "actor/operator", 1),
    ("NavCursor", "accent/operator", 1),
    ("NavWindowingHighlight", "accent/operator", 1),
    ("NavWindowingDimBg", "surface/canvas", 0.6),
    ("ModalWindowDimBg", "surface/canvas", 0.6),
]

NODE_EDITOR_SLOTS = [
    ("Bg", "surface/canvas", 1),
    ("Grid", "border/subtle", 1),
    ("NodeBg", "surface/raised", 1),
    ("NodeBorder", "border/subtle", 1),
    ("HovNodeBorder", "accent/operator-hover", 1),
    ("SelNodeBorder", "accent/operator", 1),
    ("NodeSelRect", "accent/operator-subtle", 1),
    ("NodeSelRectBorder", "accent/operator", 1),
    ("HovLinkBorder", "accent/operator-hover", 1),
    ("SelLinkBorder", "accent/operator", 1),
    ("HighlightLinkBorder", "accent/operator-active", 1),
    ("LinkSelRect", "accent/operator-subtle", 1),
    ("LinkSelRectBorder", "accent/operator", 1),
    ("PinRect", "accent/operator-subtle", 1),
    ("PinRectBorder", "accent/operator", 1),
    ("Flow", "accent/operator", 1),
    ("FlowMarker", "accent/operator", 1),
    ("GroupBg", "surface/panel", 0.5),
    ("GroupBorder", "border/subtle", 1),
]


def palette(theme):
    """The byte-quantized encoded RGB values consumed by both C++ and the audit."""
    return {key: tuple(round(channel * 255) / 255 for channel in rgb)
            for key, rgb in sem(theme).items()}


def alpha(role, theme):
    value = ALPHA.get(role, 1.0)
    return (0.28 if theme == "dark" else 0.20) if value is None else value


def contrast_pairs(theme):
    pairs = []

    def pair(fg, bg, minimum, label=None, underlay=None):
        pairs.append((label or f"{fg} on {bg}", fg, bg, underlay, minimum))

    for surface in ("surface/panel", "surface/raised", "surface/sunken", "surface/hover"):
        pair("text/primary", surface, 4.5)
        pair("text/secondary", surface, 4.5)
    pair("text/disabled", "surface/panel", 3.0)
    pair("accent/operator-text", "surface/panel", 4.5)
    pair("accent/agent-text", "surface/panel", 4.5)
    for status in ("status/success", "status/warning", "status/error"):
        pair(status, "surface/panel", 4.5)
    pair("text/on-accent", "accent/operator", 4.5)
    pair("text/on-accent", "accent/agent", 4.5)
    pair("accent/operator", "surface/panel", 3.0)
    pair("accent/agent", "surface/panel", 3.0)
    pair("border/strong", "surface/panel", 3.0)
    pair("surface/panel", "surface/canvas", 1.1 if theme == "dark" else 1.2,
         "panel/canvas island floor")
    pair("border/subtle", "surface/panel", 1.1, "subtle/panel hairline")
    pair("overlay/text", "surface/viewport", 4.5)
    pair("text/primary", "selection/bg", 4.5, "text/primary on selection (composited)",
         "surface/panel")
    pair("prov/session", "surface/panel", 4.5)
    return pairs


def audit(theme, colors):
    rows = []
    for label, fg, bg, underlay, minimum in contrast_pairs(theme):
        background = colors[bg]
        if underlay is not None:
            background = over(background, alpha(bg, theme), colors[underlay])
        ratio = contrast(colors[fg], background)
        rows.append((label, ratio, minimum, ratio >= minimum))
    return rows


def cpp_label(label):
    """Use C++ semantic identifiers where the source prose policy reserves a standalone word."""
    return re.sub(r"(?:accent|actor)/agent(?:-[a-z]+)?", lambda match: role_name(match[0]), label)


def role_name(role):
    return "".join(part.title() for part in re.split(r"[/\-]", role))


def envelope(name, brief):
    ruler = "//" + "-" * 118
    return f"{ruler}\n/// @file {name}\n/// @brief {brief}\n{ruler}\n"


def float_literal(value):
    return f"{float(value)}f"


def outputs():
    roles = list(sem("dark"))
    header = envelope("EditorThemeTokens.h", "Declares encoded editor palettes and UI slot contracts.")
    header += '''#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace lmx::app {

/// Stable semantic indices shared by both themes; generated from Tools/Theme/generate_tokens.py.
enum class ThemeRole : uint16_t {
'''
    for role in roles:
        header += f"    {role_name(role) + chr(44):<{max(len(role_name(r)) for r in roles) + 2}}///< {cpp_label(role)} semantic color.\n"
    header += f'''}};

/// Number of semantic colors in each palette.
inline constexpr std::size_t kThemeRoleCount = {len(roles)};

/// Encoded sRGB channels in [0, 1]; RGB uses exact byte / 255.0f, alpha is straight coverage.
struct ThemeColor {{
    float r; ///< Encoded red channel.
    float g; ///< Encoded green channel.
    float b; ///< Encoded blue channel.
    float a; ///< Straight alpha coverage.
}};

/// Owned immutable values indexed by ThemeRole, with no graphics-library dependency.
using ThemePalette = std::array<ThemeColor, kThemeRoleCount>;

/// Dark appearance palette; immutable for process lifetime.
extern const ThemePalette kDarkPalette;
/// Light appearance palette; immutable for process lifetime.
extern const ThemePalette kLightPalette;

/// Maps one upstream UI color slot to a semantic role without importing the UI library.
struct SlotRole {{
    std::string_view name; ///< Static upstream enumerator suffix, excluding the library prefix.
    ThemeRole role;        ///< Palette entry supplying encoded RGB and straight alpha.
    float alphaScale;      ///< Multiplier in [0, 1] applied to the palette entry's alpha only.
}};

/// Complete pinned ImGuiCol_ order; names and views have process lifetime.
extern const std::array<SlotRole, 63> kImGuiSlots;
/// Complete pinned node-editor StyleColor order; names and views have process lifetime.
extern const std::array<SlotRole, 19> kNodeEditorSlots;

/// WCAG luminance ratio contract over encoded palette colors decoded to linear light.
struct ContrastPair {{
    /// Static audit label with process lifetime.
    std::string_view label;
    /// Foreground RGB; all audited foregrounds are opaque.
    ThemeRole fg;
    /// Background RGB and, when underlay is present, straight alpha.
    ThemeRole bg;
    /// Opaque base for encoded-sRGB background compositing.
    std::optional<ThemeRole> underlay;
    /// Inclusive minimum WCAG contrast ratio.
    double minimum;
}};

/// Required dark-theme contrast pairs, including the dark island floor.
extern const std::array<ContrastPair, 24> kDarkContrastPairs;
/// Required light-theme contrast pairs, including the light island floor.
extern const std::array<ContrastPair, 24> kLightContrastPairs;

}} // namespace lmx::app
'''
    cpp = envelope("EditorThemeTokens.cpp", "Defines generated byte-exact editor colors and slot mappings.")
    cpp += '#include "EditorThemeTokens.h"\n\nnamespace lmx::app {\n\n'
    # Fixed one-entry-per-line data is intentionally immune to formatter version changes.
    cpp += '// clang-format off\n'
    document = {"primitives": {key: {"hex": hexs(rgb), "rgb": [round(v, 4) for v in rgb]}
                               for key, rgb in prim.items()}, "themes": {}}
    for theme in ("dark", "light"):
        colors = palette(theme)
        document["themes"][theme] = {
            role: {"hex": hexs(rgb), "rgb": list(rgb), "alpha": alpha(role, theme)}
            for role, rgb in colors.items()}
        cpp += f"const ThemePalette k{theme.title()}Palette = {{{{\n"
        for role, rgb in colors.items():
            channels = ", ".join(f"{round(v * 255)}.0f / 255.0f" for v in rgb)
            cpp += f"    {{{channels}, {float_literal(alpha(role, theme))}}}, // {cpp_label(role)}\n"
        cpp += "}};\n\n"
    for name, slots in (("ImGui", IMGUI_SLOTS), ("NodeEditor", NODE_EDITOR_SLOTS)):
        cpp += f"const std::array<SlotRole, {len(slots)}> k{name}Slots = {{{{\n"
        for slot, role, scale in slots:
            cpp += f'    {{"{slot}", ThemeRole::{role_name(role)}, {float_literal(scale)}}},\n'
        cpp += "}};\n\n"
    for theme in ("dark", "light"):
        cpp += f"const std::array<ContrastPair, 24> k{theme.title()}ContrastPairs = {{{{\n"
        for label, fg, bg, underlay, minimum in contrast_pairs(theme):
            under = "std::nullopt" if underlay is None else "ThemeRole::" + role_name(underlay)
            # Break rows explicitly to keep generated code within the source column budget.
            cpp += f'    {{"{cpp_label(label)}",\n'
            cpp += f'     ThemeRole::{role_name(fg)}, ThemeRole::{role_name(bg)}, {under}, {minimum}}},\n'
        cpp += "}};\n\n"
    cpp += '// clang-format on\n\n} // namespace lmx::app\n'
    return {"Source/App/Model/Workspace/EditorThemeTokens.h": header,
            "Source/App/Model/Workspace/EditorThemeTokens.cpp": cpp,
            "Tools/Theme/figma-variables.json": json.dumps(document, indent=2) + "\n"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Report stale outputs without writing")
    parser.add_argument("--audit", action="store_true", help="Print both contrast tables without writing")
    args = parser.parse_args()
    if args.audit:
        passed = 0
        for theme in ("dark", "light"):
            print(f"{theme.title()} contrast audit")
            print("Pair | Ratio | Minimum | Result")
            for label, ratio, minimum, ok in audit(theme, palette(theme)):
                print(f"{label} | {ratio:.6f} | {minimum} | {'PASS' if ok else 'FAIL'}")
                passed += ok
        print(f"{passed}/48 contrast pairs pass")
        if passed != 48:
            return 1
    if args.audit and not args.check:
        return 0
    root = Path(__file__).resolve().parents[2]
    stale = []
    for relative, content in outputs().items():
        path = root / relative
        if args.check:
            if not path.exists() or path.read_bytes() != content.encode("utf-8"):
                stale.append(relative)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content.encode("utf-8"))
            print(f"Generated {relative}")
    if stale:
        for relative in stale:
            print(f"Stale generated output: {relative}", file=sys.stderr)
        return 1
    if args.check:
        print("Theme token outputs are current")
    return 0


if __name__ == "__main__":
    sys.exit(main())
