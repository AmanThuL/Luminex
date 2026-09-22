//----------------------------------------------------------------------------------------------------------------------
/// @file DisplayDomain.h
/// @brief Names the colour domain stored by the final display transform.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace lmx::render {

/// Which display-referred output view a frame produced.
enum class DisplayView : uint8_t {
    Sdr, ///< Standard dynamic range, bounded by reference white.
};

/// Transfer applied to linear display light before storage.
enum class TransferFunction : uint8_t {
    Srgb, ///< IEC 61966-2-1 piecewise sRGB encoding.
};

/// Chromaticities and white point of the encoded RGB channels.
enum class ColorPrimaries : uint8_t {
    Bt709, ///< sRGB / Rec. 709 primaries with D65 white.
};

/// Scene-to-display tone mapping applied before transfer encoding.
enum class ToneMap : uint8_t {
    PbrNeutral, ///< Khronos PBR Neutral, as implemented by Tonemap.slang.
};

/// What the display target holds after lmx.pass.display. White values are relative to the
/// display's SDR reference white, not absolute luminance. Alpha carries no coverage when opaque.
struct DisplayDomain {
    DisplayView view = DisplayView::Sdr;                ///< Display-referred view.
    TransferFunction transfer = TransferFunction::Srgb; ///< Storage transfer function.
    ColorPrimaries primaries = ColorPrimaries::Bt709;   ///< RGB primaries and white point.
    ToneMap toneMap = ToneMap::PbrNeutral;              ///< Tone map preceding the transfer.
    float referenceWhite = 1.0f; ///< SDR reference white in relative display-linear units.
    float peakWhite = 1.0f;      ///< Tone-map peak in the same units as referenceWhite.
    uint8_t bitsPerChannel = 8;  ///< Storage precision of each colour channel.
    bool opaque = true;          ///< True when stored alpha is always 1.0 and carries no coverage.
};

/// The one domain the production display transform emits.
inline constexpr DisplayDomain kSdrDisplayDomain{};

/// Stable machine-readable name of the output view.
constexpr std::string_view name(DisplayView) {
    return "sdr";
}

/// Stable machine-readable name of the transfer function.
constexpr std::string_view name(TransferFunction) {
    return "srgb";
}

/// Stable machine-readable name of the RGB primaries.
constexpr std::string_view name(ColorPrimaries) {
    return "bt709";
}

/// Stable machine-readable name of the tone map.
constexpr std::string_view name(ToneMap) {
    return "pbr-neutral";
}

/// One human-readable line describing storage and relative reference/peak white.
inline std::string describe(const DisplayDomain& domain) {
    return std::format("{} / {} / {} / {} / {}-bit / white {} / peak {} / {}", name(domain.view),
                       name(domain.transfer), name(domain.primaries), name(domain.toneMap),
                       domain.bitsPerChannel, domain.referenceWhite, domain.peakWhite,
                       domain.opaque ? "opaque" : "alpha");
}

/// Deterministic JSON object shared by capture manifests and PNG display metadata.
inline std::string toJson(const DisplayDomain& domain) {
    return std::format(
        R"({{"view":"{}","transfer":"{}","primaries":"{}","toneMap":"{}","referenceWhite":{},"peakWhite":{},"bitsPerChannel":{},"opaque":{}}})",
        name(domain.view), name(domain.transfer), name(domain.primaries), name(domain.toneMap),
        domain.referenceWhite, domain.peakWhite, domain.bitsPerChannel, domain.opaque);
}

} // namespace lmx::render
