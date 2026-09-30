//----------------------------------------------------------------------------------------------------------------------
/// @file EditorTheme.h
/// @brief Declares appearance resolution, density metrics and encoded palette transitions.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "App/Model/Workspace/EditorThemeTokens.h"

namespace lmx::app {
/// Font families loaded by the shell; fallbacks are resolved there.
enum class TypeFace {
    Sans,       ///< Geist Sans Regular.
    SansMedium, ///< Geist Sans Medium.
    Mono        ///< Geist Mono Regular.
};

/// Semantic typography in unscaled logical points, shared by both themes and densities.
enum class TypeRole {
    Caption,     ///< Secondary captions and legend notes.
    Body,        ///< Controls and readings.
    BodyStrong,  ///< Subject/topic headers, selected editor tabs and dialog actions.
    Display,     ///< Dialog titles.
    MonoCaption, ///< Compact timestamps, paths and evidence.
    MonoBody     ///< Numeric tables, costs, identifiers and ranges.
};

/// Face and base logical size; UI scale is applied by ImGui once.
struct TypeSpec {
    TypeFace face; ///< Requested face; the shell supplies a fallback if unavailable.
    float size;    ///< Unscaled logical points.
};

/// Returns the immutable typography specification for a semantic role.
TypeSpec typeSpec(TypeRole role);

/// Fixed Sans digit advance in em; Mono retains its native glyph advances.
inline constexpr float kDigitAdvanceEm = 0.6f;

/// Stored appearance preference.
enum class Appearance {
    Auto,  ///< Follow the system, defaulting to Dark when unknown.
    Light, ///< Force light appearance.
    Dark   ///< Force dark appearance.
};

/// Stored spacing preference; does not change fonts.
enum class Density {
    Comfortable, ///< Default spacing.
    Compact      ///< Reduced spacing.
};

/// System appearance observation.
enum class SystemTheme {
    Unknown, ///< No system observation is available.
    Light,   ///< System uses light appearance.
    Dark     ///< System uses dark appearance.
};

/// Resolved palette identity.
enum class ThemeKind {
    Dark, ///< Dark palette.
    Light ///< Light palette.
};

/// Resolves a preference; Auto falls back to Dark when the system is unknown.
ThemeKind resolveTheme(Appearance appearance, SystemTheme system);

/// Returns the forced native window appearance; Auto returns nullopt to inherit the system.
std::optional<ThemeKind> forcedWindowAppearance(Appearance appearance);

/// Returns an immutable palette with process lifetime.
const ThemePalette& themePalette(ThemeKind kind);

/// Composites straight-alpha colors with channels in [0, 1] in encoded sRGB; zero resulting alpha
/// returns transparent black.
ThemeColor composite(ThemeColor top, ThemeColor bottom);

/// Returns WCAG 2.2 contrast for opaque encoded sRGB colors; alpha is ignored.
double contrastRatio(ThemeColor a, ThemeColor b);

/// Parses exact lowercase storage names; unknown values return nullopt.
std::optional<Appearance> parseAppearance(std::string_view name);

/// Returns a lowercase storage name with process lifetime.
std::string_view appearanceName(Appearance appearance);

/// Parses exact lowercase storage names; unknown values return nullopt.
std::optional<Density> parseDensity(std::string_view name);

/// Returns a lowercase storage name with process lifetime.
std::string_view densityName(Density density);

/// Unscaled spacing in UI points.
struct DensityMetrics {
    float framePaddingX; ///< Horizontal frame padding.
    float framePaddingY; ///< Vertical frame padding.
    float itemSpacingX;  ///< Horizontal item spacing.
    float itemSpacingY;  ///< Vertical item spacing.
    float windowPadding; ///< Padding on each window edge.
};

/// Returns unscaled metrics for the selected density.
DensityMetrics densityMetrics(Density density);

/// Owned palette crossfade; caller serializes access and supplies monotonic seconds.
class ThemeTransition {
public:
    /// Starts a 160 ms encoded-sRGB fade from an owned copy of from to to; reduceMotion snaps.
    /// To restart continuously, pass sample(now) as from. Sizes and spacing are outside this model.
    void start(const ThemePalette& from, const ThemePalette& to, double now, bool reduceMotion);
    /// Returns the interpolated palette; before start returns from, at/after the deadline returns
    /// exactly to.
    ThemePalette sample(double now) const;
    /// Returns true before the deadline of an animated transition, including its start instant.
    bool active(double now) const;

private:
    ThemePalette m_from = kDarkPalette;
    ThemePalette m_to = kDarkPalette;
    double m_start = 0;
    double m_end = 0;
};

/// Persisted preference plus an optional session override; caller serializes access.
struct AppearanceState {
    Appearance persisted = Appearance::Auto; ///< Preference saved to workspace storage.
    std::optional<Appearance> override;      ///< Session-only preference, when present.
    /// Returns the override if present, otherwise persisted.
    Appearance effective() const;
    /// Saves the chosen preference and clears the session override.
    void choose(Appearance appearance);
};

} // namespace lmx::app
