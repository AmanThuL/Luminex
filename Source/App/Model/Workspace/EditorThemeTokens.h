//----------------------------------------------------------------------------------------------------------------------
/// @file EditorThemeTokens.h
/// @brief Declares encoded editor palettes and UI slot contracts.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace lmx::app {

/// Stable semantic indices shared by both themes; generated from Tools/Theme/generate_tokens.py.
enum class ThemeRole : uint16_t {
    SurfaceCanvas,          ///< surface/canvas semantic color.
    SurfacePanel,           ///< surface/panel semantic color.
    SurfaceRaised,          ///< surface/raised semantic color.
    SurfaceSunken,          ///< surface/sunken semantic color.
    SurfaceHover,           ///< surface/hover semantic color.
    SurfaceActive,          ///< surface/active semantic color.
    SurfaceOverlay,         ///< surface/overlay semantic color.
    SurfaceViewport,        ///< surface/viewport semantic color.
    TextPrimary,            ///< text/primary semantic color.
    TextSecondary,          ///< text/secondary semantic color.
    TextDisabled,           ///< text/disabled semantic color.
    TextOnAccent,           ///< text/on-accent semantic color.
    BorderSubtle,           ///< border/subtle semantic color.
    BorderStrong,           ///< border/strong semantic color.
    BorderFocus,            ///< border/focus semantic color.
    AccentOperator,         ///< accent/operator semantic color.
    AccentOperatorHover,    ///< accent/operator-hover semantic color.
    AccentOperatorActive,   ///< accent/operator-active semantic color.
    AccentOperatorText,     ///< accent/operator-text semantic color.
    AccentOperatorSubtle,   ///< accent/operator-subtle semantic color.
    AccentAgent,            ///< AccentAgent semantic color.
    AccentAgentHover,       ///< AccentAgentHover semantic color.
    AccentAgentActive,      ///< AccentAgentActive semantic color.
    AccentAgentText,        ///< AccentAgentText semantic color.
    AccentAgentSubtle,      ///< AccentAgentSubtle semantic color.
    StatusSuccess,          ///< status/success semantic color.
    StatusWarning,          ///< status/warning semantic color.
    StatusError,            ///< status/error semantic color.
    StatusInfo,             ///< status/info semantic color.
    ActorOperator,          ///< actor/operator semantic color.
    ActorAgent,             ///< ActorAgent semantic color.
    ActorSystem,            ///< actor/system semantic color.
    ProvSession,            ///< prov/session semantic color.
    SelectionBg,            ///< selection/bg semantic color.
    SelectionText,          ///< selection/text semantic color.
    GraphRaster,            ///< graph/raster semantic color.
    GraphCompute,           ///< graph/compute semantic color.
    GraphCopy,              ///< graph/copy semantic color.
    GraphExternal,          ///< graph/external semantic color.
    GraphGroup,             ///< graph/group semantic color.
    GraphSink,              ///< graph/sink semantic color.
    GraphCulled,            ///< graph/culled semantic color.
    GraphLink0,             ///< graph/link-0 semantic color.
    GraphLink1,             ///< graph/link-1 semantic color.
    GraphLink2,             ///< graph/link-2 semantic color.
    GraphLink3,             ///< graph/link-3 semantic color.
    GraphLink4,             ///< graph/link-4 semantic color.
    GraphLink5,             ///< graph/link-5 semantic color.
    GraphLink6,             ///< graph/link-6 semantic color.
    GraphLink7,             ///< graph/link-7 semantic color.
    PlotLine,               ///< plot/line semantic color.
    PlotLimit,              ///< plot/limit semantic color.
    OverlayBoundsCandidate, ///< overlay/bounds-candidate semantic color.
    OverlayBoundsSelected,  ///< overlay/bounds-selected semantic color.
    OverlayOutline,         ///< overlay/outline semantic color.
    OverlayLabel,           ///< overlay/label semantic color.
    ConsoleTrace,           ///< console/trace semantic color.
    ConsoleDebug,           ///< console/debug semantic color.
    ConsoleInfo,            ///< console/info semantic color.
    ConsoleWarn,            ///< console/warn semantic color.
    ConsoleError,           ///< console/error semantic color.
    ConsoleCritical,        ///< console/critical semantic color.
    OverlayText,            ///< overlay/text semantic color.
};

/// Number of semantic colors in each palette.
inline constexpr std::size_t kThemeRoleCount = 63;

/// Encoded sRGB channels in [0, 1]; RGB uses exact byte / 255.0f, alpha is straight coverage.
struct ThemeColor {
    float r; ///< Encoded red channel.
    float g; ///< Encoded green channel.
    float b; ///< Encoded blue channel.
    float a; ///< Straight alpha coverage.
};

/// Owned immutable values indexed by ThemeRole, with no graphics-library dependency.
using ThemePalette = std::array<ThemeColor, kThemeRoleCount>;

/// Dark appearance palette; immutable for process lifetime.
extern const ThemePalette kDarkPalette;
/// Light appearance palette; immutable for process lifetime.
extern const ThemePalette kLightPalette;

/// Maps one upstream UI color slot to a semantic role without importing the UI library.
struct SlotRole {
    std::string_view name; ///< Static name returned by the upstream GetStyleColorName API.
    ThemeRole role;        ///< Palette entry supplying encoded RGB and straight alpha.
    float alphaScale;      ///< Multiplier in [0, 1] applied to the palette entry's alpha only.
};

/// Complete pinned ImGuiCol_ order; names and views have process lifetime.
extern const std::array<SlotRole, 63> kImGuiSlots;
/// Complete pinned node-editor StyleColor order; names and views have process lifetime.
extern const std::array<SlotRole, 19> kNodeEditorSlots;

/// WCAG luminance ratio contract over encoded palette colors decoded to linear light.
struct ContrastPair {
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
};

/// Required dark-theme contrast pairs, including the dark island floor.
extern const std::array<ContrastPair, 24> kDarkContrastPairs;
/// Required light-theme contrast pairs, including the light island floor.
extern const std::array<ContrastPair, 24> kLightContrastPairs;

} // namespace lmx::app
