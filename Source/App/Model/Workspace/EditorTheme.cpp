//----------------------------------------------------------------------------------------------------------------------
/// @file EditorTheme.cpp
/// @brief Defines appearance resolution, density metrics and encoded palette transitions.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/Workspace/EditorTheme.h"

#include <algorithm>
#include <cmath>

namespace lmx::app {

//======================================================================================================================
ThemeKind resolveTheme(Appearance appearance, SystemTheme system) {
    return appearance == Appearance::Light ||
                   (appearance == Appearance::Auto && system == SystemTheme::Light)
               ? ThemeKind::Light
               : ThemeKind::Dark;
}

//======================================================================================================================
std::optional<ThemeKind> forcedWindowAppearance(Appearance appearance) {
    if (appearance == Appearance::Auto)
        return std::nullopt;
    return resolveTheme(appearance, SystemTheme::Unknown);
}

//======================================================================================================================
const ThemePalette& themePalette(ThemeKind kind) {
    return kind == ThemeKind::Light ? kLightPalette : kDarkPalette;
}

//======================================================================================================================
ThemeColor composite(ThemeColor top, ThemeColor bottom) {
    const float alpha = top.a + bottom.a * (1 - top.a);
    if (alpha == 0)
        return {0, 0, 0, 0};
    return {(top.r * top.a + bottom.r * bottom.a * (1 - top.a)) / alpha,
            (top.g * top.a + bottom.g * bottom.a * (1 - top.a)) / alpha,
            (top.b * top.a + bottom.b * bottom.a * (1 - top.a)) / alpha, alpha};
}

//======================================================================================================================
double contrastRatio(ThemeColor a, ThemeColor b) {
    const auto luminance = [](ThemeColor c) {
        const auto decode = [](double v) {
            return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * decode(c.r) + 0.7152 * decode(c.g) + 0.0722 * decode(c.b);
    };
    const auto x = luminance(a), y = luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}

//======================================================================================================================
std::optional<Appearance> parseAppearance(std::string_view name) {
    if (name == "auto")
        return Appearance::Auto;
    if (name == "light")
        return Appearance::Light;
    if (name == "dark")
        return Appearance::Dark;
    return std::nullopt;
}

//======================================================================================================================
std::string_view appearanceName(Appearance appearance) {
    switch (appearance) {
    case Appearance::Auto:
        return "auto";
    case Appearance::Light:
        return "light";
    case Appearance::Dark:
        return "dark";
    }
    return "auto";
}

//======================================================================================================================
std::optional<Density> parseDensity(std::string_view name) {
    if (name == "comfortable")
        return Density::Comfortable;
    if (name == "compact")
        return Density::Compact;
    return std::nullopt;
}

//======================================================================================================================
std::string_view densityName(Density density) {
    return density == Density::Compact ? "compact" : "comfortable";
}

//======================================================================================================================
DensityMetrics densityMetrics(Density density) {
    return density == Density::Compact ? DensityMetrics{6, 3, 6, 4, 8}
                                       : DensityMetrics{8, 5, 8, 8, 12};
}

//======================================================================================================================
void ThemeTransition::start(const ThemePalette& from, const ThemePalette& to, double now,
                            bool reduceMotion) {
    m_from = from;
    m_to = to;
    m_start = now;
    m_end = reduceMotion ? now : now + 0.160;
}

//======================================================================================================================
ThemePalette ThemeTransition::sample(double now) const {
    if (now >= m_end)
        return m_to;
    if (now <= m_start)
        return m_from;
    const float t = static_cast<float>((now - m_start) / (m_end - m_start));
    ThemePalette result;
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto a = m_from[i], b = m_to[i];
        result[i] = {std::lerp(a.r, b.r, t), std::lerp(a.g, b.g, t), std::lerp(a.b, b.b, t),
                     std::lerp(a.a, b.a, t)};
    }
    return result;
}

//======================================================================================================================
bool ThemeTransition::active(double now) const {
    return m_end > m_start && now < m_end;
}

//======================================================================================================================
Appearance AppearanceState::effective() const {
    return override.value_or(persisted);
}

//======================================================================================================================
void AppearanceState::choose(Appearance appearance) {
    persisted = appearance;
    override.reset();
}

} // namespace lmx::app
