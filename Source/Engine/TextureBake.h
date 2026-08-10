//----------------------------------------------------------------------------------------------------------------------
/// @file TextureBake.h
/// @brief Declares deterministic mip baking, manifests, and hashing helpers.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset.h"
#include "RHI/RHI.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace lmx::engine {

/// Selects both the color-space handling and the manifest's recorded filter name. sRGB decodes
/// each texel to linear before filtering and re-encodes every generated level back to sRGB bytes
/// (base-color images). Linear box-filters raw normalized bytes with no transform at all (data
/// textures -- metallic-roughness, occlusion -- not yet a Scene consumer, but already correct for
/// when one arrives). NormalMap decodes to a [-1,1] tangent-space vector, filters, and
/// renormalizes every generated level so a mip never drifts off the unit sphere.
enum class BakeMode {
    Srgb,      ///< Decode and re-encode color channels around filtering.
    Linear,    ///< Filter normalized channels without a transfer function.
    NormalMap, ///< Decode, filter, and renormalize tangent-space vectors.
};

/// The manifest's "filter" string for each mode -- written literally, not derived, so a manifest
/// reader never has to know this enum's numeric values.
std::string_view bakeFilterName(BakeMode mode);

/// A full mip chain baked in memory: one 2D image, RGBA8, tightly packed and mip-major (matches
/// Engine/DdsLoader.h's DdsImage payload layout for a single face). `mips` is createTexture-ready
/// -- MaterialLab hands it to rhi::Device::createTexture directly, without ever touching a file.
/// Move-only because each TextureMip points into `payload`; a default copy would leave the copied
/// descriptors pointing into the source object's storage.
struct BakedMipChain {
    uint32_t width = 0, height = 0, mipLevels = 1; ///< Base extent and full level count.
    std::vector<std::byte> payload;                ///< Owned, tightly packed RGBA8 mip bytes.
    std::vector<rhi::TextureMip> mips;             ///< Upload views pointing into `payload`.

    /// Creates an empty chain ready to receive owned payload and views.
    BakedMipChain() = default;
    /// Copying is disabled because mip views point into the chain's payload.
    BakedMipChain(const BakedMipChain&) = delete;
    /// Copying is disabled because mip views point into the chain's payload.
    BakedMipChain& operator=(const BakedMipChain&) = delete;
    /// Transfers payload ownership together with its mip views.
    BakedMipChain(BakedMipChain&&) noexcept = default;
    /// Transfers payload ownership together with its mip views.
    BakedMipChain& operator=(BakedMipChain&&) noexcept = default;
};

/// Deterministic offline mip generation. rgba8 is level 0, tightly packed (width*height*4 bytes,
/// R,G,B,A per texel -- stb_image's req_comp=4 layout, the same one GltfLoader.h's GltfImage
/// carries). Level 0 of the output is copied verbatim: filtering starts at level 1, always derived
/// from the immediately preceding level's *already-rounded* output, not recomputed from level 0 --
/// so the chain matches what a human re-deriving level N from a saved level N-1 file would get.
///
/// Level dimensions follow Engine/DdsLoader.h's contract: level L is max(1, base >> L) on each
/// axis independently (not a recursive ceiling-halve), which is exactly a recursive floor-halve of
/// the previous level because right-shift is associative ((n>>a)>>b == n>>(a+b)). Each step from
/// one level to the next is therefore a 2x2 box filter EXCEPT when the source extent on an axis is
/// 1 (that axis stops halving, kernel width 1, a direct copy) or odd (destination extent is
/// floor(source/2); the trailing unpaired source texel -- index sourceExtent-1 -- folds into the
/// LAST destination texel with equal area weight alongside its normal pair, e.g. a source width of
/// 5 makes destination texel 0 average source columns {0,1} at weight 1/2 each, and destination
/// texel 1 (the last) average columns {2,3,4} at weight 1/3 each). The two axes combine
/// separably, so a corner destination texel with both axes odd-and-last can average up to 9 source
/// texels at weight 1/9 each. Every accumulation iterates source rows top-to-bottom, and within a
/// row left-to-right (scanline order) -- fully deterministic, no parallel reduction, no ordering
/// that depends on hardware thread count.
///
/// Alpha is never colour-space transformed in any mode (Engine/Color.h's rule: alpha is coverage,
/// not colour) -- it box-filters as a raw normalized value in every mode, round-to-nearest on
/// write-back like every other channel.
BakedMipChain bakeMips(std::span<const uint8_t> rgba8, uint32_t width, uint32_t height,
                       BakeMode mode);

/// Writes `image` as an uncompressed A8R8G8B8 DDS with a full mip chain, Tex2D only (no cubemap
/// bake path exists) -- the exact legacy layout Engine/DdsLoader.h's loadDds parses, byte-swapped
/// to the file's B,G,R,A order on write the same way loadDds swaps it back to R,G,B,A on read.
AssetResult<void> writeDds(std::string_view path, const BakedMipChain& image);

/// Writes `<path>` (the caller passes the full "<out>.dds.json" name) with deterministic key
/// order: {"source", "sourceSha256", "filter", "toolVersion"}. `source` is recorded exactly as
/// given -- callers pass a repo-relative path or a basename, never an absolute one, so the
/// manifest never encodes the machine it was baked on.
AssetResult<void> writeManifest(std::string_view path, std::string_view source,
                                std::string_view sourceSha256, BakeMode mode,
                                std::string_view toolVersion);

/// Lowercase hex SHA-256 of `bytes`. Self-contained (no external crypto dependency) so the
/// manifest's source hash needs nothing beyond what Engine already links; used identically by the
/// bake tool (hashing the source file) and by Tools/bake_gltf_textures.py's own hashlib-based
/// staleness check -- both compute the same standard digest, just in different languages.
std::string sha256Hex(std::span<const std::byte> bytes);

} // namespace lmx::engine
