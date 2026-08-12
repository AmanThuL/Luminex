//----------------------------------------------------------------------------------------------------------------------
/// @file StressCases.cpp
/// @brief Defines the frozen bounded stress workloads.
//----------------------------------------------------------------------------------------------------------------------

#include "Workload/StressCases.h"
#include "Workload/Splitmix64.h"

namespace lmx::noapi::workload {

//======================================================================================================================
uint32_t bindTextureIndexForDraw(uint32_t drawIndex) {
    return drawIndex % kBindTextureCount;
}

namespace {
using enum HazardOpKind;
using enum HazardKind;

// Every per-mip case in H14-H24 addresses these two mips of the shared 4-mip texture; the spec
// requires only that the two be distinct, not which two, so one fixed pair is the manifest's own
// freeze (spec section 7: "per-mip cases address distinct mips").
constexpr uint32_t kPerMipProducer = 1;
constexpr uint32_t kPerMipConsumer = 2;

//======================================================================================================================
HazardCase whole(std::string id, HazardKind kind, HazardOpKind producer, HazardOpKind consumer) {
    return {.id = std::move(id), .hazard = kind, .producer = producer, .consumer = consumer};
}

//======================================================================================================================
HazardCase perMip(std::string id, HazardKind kind, HazardOpKind producer, HazardOpKind consumer) {
    return {.id = std::move(id),
            .hazard = kind,
            .producer = producer,
            .consumer = consumer,
            .perMip = true,
            .producerMip = kPerMipProducer,
            .consumerMip = kPerMipConsumer};
}
} // namespace

//======================================================================================================================
const std::vector<HazardCase>& hazardCases() {
    static const std::vector<HazardCase> cases = {
        // H01-H09: RAW, whole resource, all nine ordered pairs.
        whole("H01", ReadAfterWrite, Raster, Raster),
        whole("H02", ReadAfterWrite, Raster, Compute),
        whole("H03", ReadAfterWrite, Raster, Copy),
        whole("H04", ReadAfterWrite, Compute, Raster),
        whole("H05", ReadAfterWrite, Compute, Compute),
        whole("H06", ReadAfterWrite, Compute, Copy),
        whole("H07", ReadAfterWrite, Copy, Raster),
        whole("H08", ReadAfterWrite, Copy, Compute),
        whole("H09", ReadAfterWrite, Copy, Copy),
        // H10-H13: WAR and WAW, whole resource, for C<->C and R<->C.
        whole("H10", WriteAfterRead, Compute, Compute),
        whole("H11", WriteAfterWrite, Compute, Compute),
        whole("H12", WriteAfterRead, Raster, Compute),
        whole("H13", WriteAfterWrite, Raster, Compute),
        // H14-H18: RAW per-mip.
        perMip("H14", ReadAfterWrite, Compute, Compute),
        perMip("H15", ReadAfterWrite, Compute, Raster),
        perMip("H16", ReadAfterWrite, Raster, Compute),
        perMip("H17", ReadAfterWrite, Copy, Compute),
        perMip("H18", ReadAfterWrite, Compute, Copy),
        // H19-H22: WAR and WAW per-mip, for C<->C and R<->C.
        perMip("H19", WriteAfterRead, Compute, Compute),
        perMip("H20", WriteAfterWrite, Compute, Compute),
        perMip("H21", WriteAfterRead, Raster, Compute),
        perMip("H22", WriteAfterWrite, Raster, Compute),
        // H23-H24: RAW per-mip, distinct mips.
        perMip("H23", ReadAfterWrite, Raster, Raster),
        perMip("H24", ReadAfterWrite, Copy, Copy),
    };
    return cases;
}

//======================================================================================================================
uint8_t hazardExpectedTexel(const HazardCase& hazardCase, uint32_t texelIndex) {
    // The numeric suffix of "H<NN>" is the domain tag, offset clear of every other generator's
    // tags in this library, so each case's readback oracle is independent of every other's.
    const uint32_t caseNumber =
        static_cast<uint32_t>((hazardCase.id[1] - '0') * 10 + (hazardCase.id[2] - '0'));
    const uint64_t draw = splitmix64(kSeed, {uint64_t{2000} + caseNumber, texelIndex});
    return static_cast<uint8_t>(draw & 0xFFu);
}

//======================================================================================================================
const std::vector<LifetimeFrame>& lifetimeSchedule() {
    static const std::vector<LifetimeFrame> schedule = {
        {.frameIndex = 0, .extent = kExtentE1},
        {.frameIndex = 1, .extent = kExtentE1},
        {.frameIndex = 2, .extent = kExtentE1},
        {.frameIndex = 3,
         .extent = kExtentE2,
         .resizeIssuedThisFrame = true,
         .resizeTarget = kExtentE2},
        {.frameIndex = 4, .extent = kExtentE2, .midFlightUpload = true},
        {.frameIndex = 5, .extent = kExtentE2},
        {.frameIndex = 6,
         .extent = kExtentE3,
         .resizeIssuedThisFrame = true,
         .resizeTarget = kExtentE3},
        {.frameIndex = 7, .extent = kExtentE3},
        {.frameIndex = 8, .extent = kExtentE3},
        {.frameIndex = 9,
         .extent = kExtentE1,
         .resizeIssuedThisFrame = true,
         .resizeTarget = kExtentE1},
        {.frameIndex = 10, .extent = kExtentE1},
        {.frameIndex = 11, .extent = kExtentE1},
    };
    return schedule;
}

//======================================================================================================================
const std::vector<IndirectCase>& indirectCases() {
    static const std::vector<IndirectCase> cases = {
        {.id = "I1",
         .kind = IndirectKind::DrawIndirect,
         .argSource = IndirectArgSource::CpuWritten,
         .byteOffset = 256},
        {.id = "I2",
         .kind = IndirectKind::DrawIndexedIndirect,
         .argSource = IndirectArgSource::ComputeWritten,
         .byteOffset = 0},
        {.id = "I3",
         .kind = IndirectKind::DispatchIndirect,
         .argSource = IndirectArgSource::CpuWritten,
         .byteOffset = 64},
        {.id = "I4",
         .kind = IndirectKind::DispatchIndirect,
         .argSource = IndirectArgSource::ComputeWritten,
         .byteOffset = 128},
    };
    return cases;
}

//======================================================================================================================
const std::vector<MisuseCase>& misuseCases() {
    static const std::vector<MisuseCase> cases = {
        {.id = "M1",
         .kind = MisuseId::PassScopeViolation,
         .description = "dispatch recorded outside any compute pass"},
        {.id = "M2",
         .kind = MisuseId::UsageMismatch,
         .description = "a texture created without storage usage bound for storage write"},
        {.id = "M3",
         .kind = MisuseId::ViewRangeOverflow,
         .description = "a view whose mip range exceeds the texture's mip count"},
        {.id = "M4",
         .kind = MisuseId::AlignmentViolation,
         .description = "a binding offset violating the documented alignment contract"},
        {.id = "M5",
         .kind = MisuseId::InvalidHandle,
         .description = "use of a destroyed (or never-created) resource handle"},
        {.id = "M6",
         .kind = MisuseId::RetiredFrameSlotUse,
         .description =
             "reusing a per-frame allocation from a frame slot that has not been proven retired"},
    };
    return cases;
}

} // namespace lmx::noapi::workload
