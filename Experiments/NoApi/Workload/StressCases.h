//----------------------------------------------------------------------------------------------------------------------
/// @file StressCases.h
/// @brief Declares StressCases for the NoApi experiment.
//----------------------------------------------------------------------------------------------------------------------

/// @details Declares the frozen bounded stress workloads: S-BIND, the H01-H24 hazard matrix,
/// S-LIFE,
///        I1-I4, and M1-M6 (spec section 7).

#pragma once
#include "Workload/Types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lmx::noapi::workload {

// ---------------------------------------------------------------------------------------------
// S-BIND: binding scale.
// ---------------------------------------------------------------------------------------------

inline constexpr uint32_t kBindTextureCount = 256;
inline constexpr uint32_t kBindTextureSize = 64;
inline constexpr Format kBindTextureFormat = Format::RGBA8Unorm;
inline constexpr uint32_t kBindTargetWidth = 512;
inline constexpr uint32_t kBindTargetHeight = 512;
inline constexpr Format kBindTargetFormat = Format::RGBA8Unorm;
/// The two frozen S-BIND draw counts (spec: "exactly 1,024 and 4,096 draws").
inline constexpr std::array<uint32_t, 2> kBindDrawCounts = {1024, 4096};

/// Texture index draw `drawIndex` samples (spec: "texture index = draw index mod 256").
uint32_t bindTextureIndexForDraw(uint32_t drawIndex);

// ---------------------------------------------------------------------------------------------
// H01-H24: the hazard matrix (spec section 7's table, restated one row per case).
// ---------------------------------------------------------------------------------------------

/// Producer/consumer pass kind, on the spec table's own R/C/T notation.
enum class HazardOpKind { Raster, Compute, Copy };

/// Which hazard the case is checking (spec: "RAW", "WAR and WAW").
enum class HazardKind { ReadAfterWrite, WriteAfterRead, WriteAfterWrite };

/// One hazard-matrix case: a producer op, a consumer op, and (for a per-mip case) the two distinct
/// mips the two ops address on a shared 4-mip 256x256 texture. `expectedTexel` is the deterministic
/// value (spec: "deterministic expected readback values you define now") both encoders' readback
/// must match: the producer writes splitmix64(kSeed, {id-derived tag, texel}) truncated to one
/// byte per channel, and a correct hazard ordering is what lets the consumer observe it rather than
/// whatever the resource held before.
struct HazardCase {
    std::string id; ///< "H01".."H24".
    HazardKind hazard = HazardKind::ReadAfterWrite;
    HazardOpKind producer = HazardOpKind::Raster;
    HazardOpKind consumer = HazardOpKind::Raster;
    bool perMip = false;      ///< Whole-resource (single-mip texture) when false.
    uint32_t producerMip = 0; ///< Meaningful only when perMip.
    uint32_t consumerMip = 0; ///< Meaningful only when perMip; always != producerMip.
};

/// A 4-mip 256x256 RGBA8Unorm texture, shared by every per-mip hazard case (spec: "per-mip cases
/// address distinct mips of a 4-mip 256x256 texture").
inline constexpr uint32_t kHazardPerMipTextureSize = 256;
inline constexpr uint32_t kHazardPerMipTextureMipCount = 4;
/// A single-mip texture, shared by every whole-resource hazard case.
inline constexpr uint32_t kHazardWholeResourceTextureSize = 64;

/// Exactly the 24 cases the spec table lists, in H01..H24 order.
const std::vector<HazardCase>& hazardCases();

/// The deterministic byte value a hazard case's producer writes and its consumer must read back,
/// per texel index within the mip it touches.
uint8_t hazardExpectedTexel(const HazardCase& hazardCase, uint32_t texelIndex);

// ---------------------------------------------------------------------------------------------
// S-LIFE: upload and resize lifetime.
// ---------------------------------------------------------------------------------------------

struct Extent {
    uint32_t width = 0;
    uint32_t height = 0;
};
inline constexpr Extent kExtentE1{1024, 1024};
inline constexpr Extent kExtentE2{1536, 864};
inline constexpr Extent kExtentE3{512, 512};

/// One frame of the frozen 12-frame S-LIFE schedule (spec section 7).
struct LifetimeFrame {
    uint32_t frameIndex = 0;
    Extent extent; ///< Extent this frame renders at.
    bool resizeIssuedThisFrame =
        false; ///< Whether a resize to `resizeTarget` is issued this frame.
    Extent resizeTarget;
    /// Frame 4 uploads new content to a texture consumed by frame 4's own graph (spec section 7).
    bool midFlightUpload = false;
};

/// The frozen 12-frame schedule (frames 0-11), exactly as spec section 7 states it.
const std::vector<LifetimeFrame>& lifetimeSchedule();

// ---------------------------------------------------------------------------------------------
// I1-I4: indirect.
// ---------------------------------------------------------------------------------------------

enum class IndirectKind { DrawIndirect, DrawIndexedIndirect, DispatchIndirect };
enum class IndirectArgSource { CpuWritten, ComputeWritten };

struct IndirectCase {
    std::string id; ///< "I1".."I4".
    IndirectKind kind = IndirectKind::DrawIndirect;
    IndirectArgSource argSource = IndirectArgSource::CpuWritten;
    uint64_t byteOffset = 0; ///< Frozen byte offset of the arguments within the argument buffer.
};

/// I1-I4, exactly as spec section 7 states them.
const std::vector<IndirectCase>& indirectCases();

// ---------------------------------------------------------------------------------------------
// M1-M6: misuse.
// ---------------------------------------------------------------------------------------------

enum class MisuseId {
    PassScopeViolation,  ///< M1
    UsageMismatch,       ///< M2
    ViewRangeOverflow,   ///< M3
    AlignmentViolation,  ///< M4
    InvalidHandle,       ///< M5
    RetiredFrameSlotUse, ///< M6
};

struct MisuseCase {
    std::string id; ///< "M1".."M6".
    MisuseId kind = MisuseId::PassScopeViolation;
    std::string description; ///< The contract violated, quoting spec section 7's own wording.
};

/// M1-M6, exactly as spec section 7 states them.
const std::vector<MisuseCase>& misuseCases();

} // namespace lmx::noapi::workload
