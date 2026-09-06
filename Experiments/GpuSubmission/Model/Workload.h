//----------------------------------------------------------------------------------------------------------------------
/// @file Workload.h
/// @brief Declares the API-neutral submission workload and shader layout.
//----------------------------------------------------------------------------------------------------------------------
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace lmx::experimental::submission {
/// Owning result with a diagnostic for invalid workload inputs or unavailable execution.
template <class T>
using Result = std::expected<T, std::string>;
/// Fixed seed used by the versioned input generator.
inline constexpr uint32_t kSeed = 0x4C4D5836;
/// Width and height, in pixels, of the offscreen targets.
inline constexpr uint32_t kExtent = 1024;
/// Number of constant-input frames in a phase.
inline constexpr uint32_t kFramesPerPhase = 32;
/// Number of phases before the deterministic sequence repeats.
inline constexpr uint32_t kPhaseCount = 8;
/// Total length of the deterministic input sequence.
inline constexpr uint32_t kReplayFrames = kFramesPerPhase * kPhaseCount;
/// Generator capacity; preserves distinct float32 depths and bounds model allocations.
inline constexpr uint32_t kMaxInstances = 1U << 20;
/// Maximum number of contiguous material bins supported by this workload.
inline constexpr uint32_t kMaxBins = 64;
/// Conservative world-space tolerance for explicitly constructed boundary fixtures only.
inline constexpr float kBoundaryEpsilon = 1e-5F;
/// Minimum world-space separation from a sphere/plane classification boundary in generated inputs.
inline constexpr double kScoredBoundaryMargin = 1e-3;
/// Recipe identity; changes to generated inputs require a new version and evidence freeze.
inline constexpr std::string_view kGeneratorVersion = "grid-rotation-v1";

/// Selects whether classification is excluded from or included in measured preparation.
enum class Suite {
    S, ///< Submission: consume the precomputed bitmap.
    E  ///< End-to-end: classify bounds using the camera planes.
};
/// Submission mechanism, with instancing retained as a repeated-geometry control.
enum class Variant {
    Direct,      ///< CPU-visible direct draws.
    CpuIndirect, ///< CPU-packed indirect arguments.
    GpuArgs,     ///< GPU-generated arguments for every candidate.
    GpuIcb,      ///< GPU-encoded native commands, subject to a separate feasibility gate.
    Batched      ///< CPU-packed visible IDs and one instanced draw per nonempty bin.
};
/// Distinct measurement lanes whose samples must not be pooled.
enum class Lane {
    Headline, ///< No GPU timestamp markers.
    GpuSpan,  ///< Only common workload-boundary markers.
    Stages    ///< Diagnostic preparation/raster markers; unscored.
};
/// Resolved generator inputs; custom correctness cases may use any valid count and fraction.
struct Case {
    std::string id;         ///< Nonempty identity; matrix IDs are n<N>-t<T>-v<percent>-b<B>.
    uint32_t count = 1024;  ///< Candidate count, including zero, up to kMaxInstances.
    uint32_t triangles = 2; ///< Exactly 2 (1x1 cells) or 32 (4x4 cells) per planar quad.
    uint32_t bins = 1;      ///< In [1,kMaxBins]; empty bins are valid, even when count is zero.
    /// Finite [0,1]; actual visible count is floor(count * fraction).
    double visibleFraction = 0.5;
};
/// Shader buffer 1 record: an axis-aligned world-XY quad and its conservative sphere.
struct alignas(16) Instance {
    float x;               ///< World-space sphere/quad center X; camera is right-handed.
    float y;               ///< World-space center Y; up is +Y.
    float z;               ///< World-space center Z; the camera looks along -Z.
    float halfExtent;      ///< Positive quad half-size in world X and Y.
    float radius;          ///< Positive world-space sphere radius, at least sqrt(2) * halfExtent.
    uint32_t bin;          ///< Bin owning this object ID; candidate ranges are contiguous.
    uint32_t padding0 = 0; ///< Reserved shader ABI word, generated as zero.
    uint32_t padding1 = 0; ///< Reserved shader ABI word, generated as zero.
};
static_assert(sizeof(Instance) == 32);
static_assert(alignof(Instance) == 16 && std::is_trivially_copyable_v<Instance>);
static_assert(std::is_standard_layout_v<Instance>);
static_assert(offsetof(Instance, x) == 0 && offsetof(Instance, y) == 4);
static_assert(offsetof(Instance, z) == 8 && offsetof(Instance, halfExtent) == 12);
static_assert(offsetof(Instance, radius) == 16 && offsetof(Instance, bin) == 20);
static_assert(offsetof(Instance, padding0) == 24 && offsetof(Instance, padding1) == 28);

/// Shader buffer 0, shared without conversion by native and RHI adapters.
struct alignas(16) Params {
    std::array<float, 16> viewProjection{};       ///< Column-major RH matrix, reversed Z in [0,1].
    std::array<std::array<float, 4>, 6> planes{}; ///< Inward normalized L/R/B/T/near/far planes.
    uint32_t count = 0;     ///< Candidate count and instance/bitmap buffer length.
    uint32_t triangles = 2; ///< Triangles per quad, 2 or 32.
    uint32_t bins = 1;      ///< Number of material bins.
    uint32_t suite = 0;     ///< Shader suite selector: S=0, E=1.
    uint32_t batched = 0;   ///< Nonzero selects visibleIds[binOffset + instanceId].
    /// Offset into packed visible IDs, applied once; firstInstance is zero.
    uint32_t binOffset = 0;
    uint32_t debug = 0; ///< Enables untimed diagnostic writes when the adapter supports them.
    float epsilon = 0;  ///< Nonnegative world-space culling tolerance; generated cases use zero.
};
static_assert(sizeof(Params) == 192);
static_assert(alignof(Params) == 16 && std::is_trivially_copyable_v<Params>);
static_assert(std::is_standard_layout_v<Params>);
static_assert(offsetof(Params, viewProjection) == 0 && offsetof(Params, planes) == 64);
static_assert(offsetof(Params, count) == 160 && offsetof(Params, triangles) == 164);
static_assert(offsetof(Params, bins) == 168 && offsetof(Params, suite) == 172);
static_assert(offsetof(Params, batched) == 176 && offsetof(Params, binOffset) == 180);
static_assert(offsetof(Params, debug) == 184 && offsetof(Params, epsilon) == 188);

/// Owning immutable input/oracle data for one logical frame; no GPU objects or borrowed storage.
struct FrameInput {
    Params params;                    ///< Default S, ordinary addressing, no debug or epsilon.
    std::vector<Instance> instances;  ///< Object ID equals index; bin b is [b*N/B,(b+1)*N/B).
    std::vector<uint32_t> bitmap;     ///< One uint32 0/1 per object; not bit-packed.
    std::vector<uint32_t> visibleIds; ///< Oracle result in ascending bin, then object-ID order.
    std::vector<uint32_t> binOffsets; ///< B+1 offsets into visibleIds, including its terminal size.
};
/// Retired correctness output; adapters own GPU synchronization before constructing this value.
struct FrameImage {
    std::vector<uint8_t> rgba; ///< Row-major RGBA8Unorm diagnostic linear color, no sRGB encode.
    std::vector<uint32_t> visibleIds; ///< IDs observed through correctness bookkeeping.
    std::string graphDump;            ///< Reference compiled graph text, or empty when unavailable.
};
/// Returns exactly 18 core points followed by the two bin points, in stable N/T/fraction order.
std::vector<Case> caseMatrix();
/// Resolves a case-sensitive matrix ID or correctness-only empty/single/tail; unknown IDs fail.
Result<Case> findCase(std::string_view id);
/// Checks identity, finite fraction, geometry, bin limits, vertex indexing and generator capacity.
Result<void> validateCase(const Case& spec);
/// Builds phase (logicalFrame / 32) % 8; invalid cases or broken geometry guarantees return errors.
/// Work is untimed. Both adapters must use these same bytes and only change their Params selectors.
Result<FrameInput> makeFrame(const Case& spec, uint32_t logicalFrame);
/// Double-precision oracle over uploaded float32 inputs: visible iff every d >= -radius-epsilon.
/// Malformed bounds, epsilon or non-normalized/nonfinite planes return false, never visible NaNs.
bool sphereVisible(const Instance& instance, const Params& params);
/// Independently classifies instances and sorts IDs by bin/object, ignoring cached bitmap/IDs.
std::vector<uint32_t> classify(const FrameInput& frame);
/// FNV-1a-64 lowercase hex digest of canonical field bytes, for deterministic replay checks.
/// This is not a cryptographic content hash; freeze evidence with an external content hash too.
std::string frameHash(const FrameInput& frame);
/// Serializes a locale-independent resolved recipe, camera, geometry and all eight phase hashes.
/// Invalid cases produce valid JSON with valid=false and an error; no NaN/Infinity is emitted.
std::string manifestJson(const Case& spec);
/// Returns the canonical CLI spelling, or "unknown" for an invalid enumeration value.
std::string name(Suite value);
/// Returns the canonical CLI spelling, or "unknown" for an invalid enumeration value.
std::string name(Variant value);
/// Returns the canonical CLI spelling, or "unknown" for an invalid enumeration value.
std::string name(Lane value);
/// Parses exactly S or E; CLI aggregate selectors such as all are handled by the caller.
Result<Suite> parseSuite(std::string_view value);
/// Parses a canonical, case-sensitive variant spelling; invalid input returns a diagnostic.
Result<Variant> parseVariant(std::string_view value);
/// Parses a canonical, case-sensitive lane spelling; invalid input returns a diagnostic.
Result<Lane> parseLane(std::string_view value);
} // namespace lmx::experimental::submission
