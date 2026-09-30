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
    SurfaceCanvas,          ///< surface/canvas semantic colour.
    SurfacePanel,           ///< surface/panel semantic colour.
    SurfaceRaised,          ///< surface/raised semantic colour.
    SurfaceSunken,          ///< surface/sunken semantic colour.
    SurfaceHover,           ///< surface/hover semantic colour.
    SurfaceActive,          ///< surface/active semantic colour.
    SurfaceOverlay,         ///< surface/overlay semantic colour.
    SurfaceViewport,        ///< surface/viewport semantic colour.
    TextPrimary,            ///< text/primary semantic colour.
    TextSecondary,          ///< text/secondary semantic colour.
    TextDisabled,           ///< text/disabled semantic colour.
    TextOnAccent,           ///< text/on-accent semantic colour.
    BorderSubtle,           ///< border/subtle semantic colour.
    BorderStrong,           ///< border/strong semantic colour.
    BorderFocus,            ///< border/focus semantic colour.
    AccentOperator,         ///< accent/operator semantic colour.
    AccentOperatorHover,    ///< accent/operator-hover semantic colour.
    AccentOperatorActive,   ///< accent/operator-active semantic colour.
    AccentOperatorText,     ///< accent/operator-text semantic colour.
    AccentOperatorSubtle,   ///< accent/operator-subtle semantic colour.
    AccentAgent,            ///< AccentAgent semantic colour.
    AccentAgentHover,       ///< AccentAgentHover semantic colour.
    AccentAgentActive,      ///< AccentAgentActive semantic colour.
    AccentAgentText,        ///< AccentAgentText semantic colour.
    AccentAgentSubtle,      ///< AccentAgentSubtle semantic colour.
    StatusSuccess,          ///< status/success semantic colour.
    StatusWarning,          ///< status/warning semantic colour.
    StatusError,            ///< status/error semantic colour.
    StatusInfo,             ///< status/info semantic colour.
    ActorOperator,          ///< actor/operator semantic colour.
    ActorAgent,             ///< ActorAgent semantic colour.
    ActorSystem,            ///< actor/system semantic colour.
    ProvSession,            ///< prov/session semantic colour.
    SelectionBg,            ///< selection/bg semantic colour.
    SelectionText,          ///< selection/text semantic colour.
    GraphRaster,            ///< graph/raster semantic colour.
    GraphCompute,           ///< graph/compute semantic colour.
    GraphCopy,              ///< graph/copy semantic colour.
    GraphExternal,          ///< graph/external semantic colour.
    GraphGroup,             ///< graph/group semantic colour.
    GraphSink,              ///< graph/sink semantic colour.
    GraphCulled,            ///< graph/culled semantic colour.
    GraphLink0,             ///< graph/link-0 semantic colour.
    GraphLink1,             ///< graph/link-1 semantic colour.
    GraphLink2,             ///< graph/link-2 semantic colour.
    GraphLink3,             ///< graph/link-3 semantic colour.
    GraphLink4,             ///< graph/link-4 semantic colour.
    GraphLink5,             ///< graph/link-5 semantic colour.
    GraphLink6,             ///< graph/link-6 semantic colour.
    GraphLink7,             ///< graph/link-7 semantic colour.
    PlotLine,               ///< plot/line semantic colour.
    PlotLimit,              ///< plot/limit semantic colour.
    OverlayBoundsCandidate, ///< overlay/bounds-candidate semantic colour.
    OverlayBoundsSelected,  ///< overlay/bounds-selected semantic colour.
    OverlayOutline,         ///< overlay/outline semantic colour.
    OverlayLabel,           ///< overlay/label semantic colour.
    ConsoleTrace,           ///< console/trace semantic colour.
    ConsoleDebug,           ///< console/debug semantic colour.
    ConsoleInfo,            ///< console/info semantic colour.
    ConsoleWarn,            ///< console/warn semantic colour.
    ConsoleError,           ///< console/error semantic colour.
    ConsoleCritical,        ///< console/critical semantic colour.
    OverlayText,            ///< overlay/text semantic colour.
};

/// Number of semantic colours in each palette.
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

/// Maps one upstream UI colour slot to a semantic role without importing the UI library.
struct SlotRole {
    std::string_view name; ///< Static upstream enumerator suffix, excluding the library prefix.
    ThemeRole role;        ///< Palette entry supplying encoded RGB and straight alpha.
    float alphaScale;      ///< Multiplier in [0, 1] applied to the palette entry's alpha only.
};

/// Complete pinned ImGuiCol_ order; names and views have process lifetime.
extern const std::array<SlotRole, 63> kImGuiSlots;
/// Complete pinned node-editor StyleColor order; names and views have process lifetime.
extern const std::array<SlotRole, 19> kNodeEditorSlots;

/// WCAG luminance ratio contract over encoded palette colours decoded to linear light.
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
