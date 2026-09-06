//----------------------------------------------------------------------------------------------------------------------
/// @file Workload.cpp
/// @brief Generates deterministic inputs, independent visibility, and resolved manifests.
//----------------------------------------------------------------------------------------------------------------------
#include "Workload.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <numeric>
#include <sstream>

namespace lmx::experimental::submission {
namespace {

constexpr float kNear = 1;
constexpr float kFar = 64;
constexpr double kGridWidth = 1.6;
constexpr double kQuadCellFraction = 0.2;
constexpr double kOutsideShift = 3;
constexpr double kPlaneNormTolerance = 1e-6;

//======================================================================================================================
uint32_t rotationStep(uint32_t count) {
    return std::max(1U, count / kPhaseCount);
}

//======================================================================================================================
uint32_t gridSide(uint32_t count) {
    return std::max(1U, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(count)))));
}

//======================================================================================================================
bool validPlanes(const Params& params) {
    if (!std::isfinite(params.epsilon) || params.epsilon < 0 ||
        !std::ranges::all_of(
            params.viewProjection, [](float value) { return std::isfinite(value); })) {
        return false;
    }
    for (const auto& plane : params.planes) {
        if (!std::ranges::all_of(plane, [](float value) { return std::isfinite(value); })) {
            return false;
        }
        const double normSquared =
            double{plane[0]} * plane[0] + double{plane[1]} * plane[1] + double{plane[2]} * plane[2];
        if (std::abs(normSquared - 1) > kPlaneNormTolerance) {
            return false;
        }
    }
    return true;
}

//======================================================================================================================
bool validBounds(const Instance& instance) {
    return std::isfinite(instance.x) && std::isfinite(instance.y) && std::isfinite(instance.z) &&
           std::isfinite(instance.halfExtent) && instance.halfExtent > 0 &&
           std::isfinite(instance.radius) &&
           double{instance.radius} >= std::sqrt(2.0) * instance.halfExtent;
}

//======================================================================================================================
double planeDistance(const Instance& instance, const std::array<float, 4>& plane) {
    return double{plane[0]} * instance.x + double{plane[1]} * instance.y +
           double{plane[2]} * instance.z + double{plane[3]};
}

//======================================================================================================================
bool visibleBounds(const Instance& instance, const Params& params) {
    for (const auto& plane : params.planes) {
        if (planeDistance(instance, plane) < -double{instance.radius} - params.epsilon) {
            return false;
        }
    }
    return true;
}

//======================================================================================================================
std::string quote(std::string_view value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << '"';
    for (unsigned char byte : value) {
        if (byte == '"' || byte == '\\') {
            out << '\\' << byte;
        } else if (byte < 32 || byte >= 127) {
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned{byte};
        } else {
            out << byte;
        }
    }
    out << '"';
    return out.str();
}

//======================================================================================================================
template <typename Range>
void writeNumbers(std::ostream& out, const Range& values) {
    out << '[';
    bool first = true;
    for (const auto& value : values) {
        if (!first) {
            out << ',';
        }
        out << value;
        first = false;
    }
    out << ']';
}

//======================================================================================================================
void hashWord(uint64_t& hash, uint64_t value, unsigned bytes) {
    for (unsigned index = 0; index < bytes; ++index) {
        hash ^= (value >> (8 * index)) & 0xff;
        hash *= 1099511628211ULL;
    }
}

//======================================================================================================================
void hashFloat(uint64_t& hash, float value) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    hashWord(hash, std::bit_cast<uint32_t>(value), 4);
}

} // namespace

//======================================================================================================================
std::vector<Case> caseMatrix() {
    std::vector<Case> result;
    result.reserve(20);
    for (uint32_t count : {1024U, 16384U, 65536U}) {
        for (uint32_t triangles : {2U, 32U}) {
            for (uint32_t percent : {10U, 50U, 100U}) {
                result.push_back({"n" + std::to_string(count) + "-t" + std::to_string(triangles) +
                                      "-v" + std::to_string(percent) + "-b1",
                                  count, triangles, 1, percent / 100.0});
            }
        }
    }
    for (uint32_t bins : {16U, 64U}) {
        result.push_back({"n16384-t2-v50-b" + std::to_string(bins), 16384, 2, bins, 0.5});
    }
    return result;
}

//======================================================================================================================
Result<Case> findCase(std::string_view id) {
    if (id == "empty") {
        return Case{"empty", 0, 2, 1, 0};
    }
    if (id == "single") {
        return Case{"single", 1, 2, 1, 1};
    }
    if (id == "tail") {
        return Case{"tail", 257, 2, 1, 0.5};
    }
    for (const auto& spec : caseMatrix()) {
        if (spec.id == id) {
            return spec;
        }
    }
    return std::unexpected("Unknown workload case: " + std::string(id));
}

//======================================================================================================================
Result<void> validateCase(const Case& spec) {
    if (spec.id.empty()) {
        return std::unexpected("Workload case identity must be nonempty");
    }
    if (!std::isfinite(spec.visibleFraction) || spec.visibleFraction < 0 ||
        spec.visibleFraction > 1) {
        return std::unexpected("Visible fraction must be finite and in [0,1]");
    }
    if (spec.triangles != 2 && spec.triangles != 32) {
        return std::unexpected("Quad triangle count must be 2 or 32");
    }
    if (spec.bins == 0 || spec.bins > kMaxBins) {
        return std::unexpected("Material bin count must be in [1,64]");
    }
    const uint64_t vertices = uint64_t{spec.count} * 3 * spec.triangles;
    if (vertices > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected("Virtual vertex range exceeds uint32 indexing capacity");
    }
    if (spec.count > kMaxInstances) {
        return std::unexpected("Candidate count exceeds generator capacity");
    }
    return {};
}

//======================================================================================================================
Result<FrameInput> makeFrame(const Case& spec, uint32_t logicalFrame) {
    if (auto valid = validateCase(spec); !valid) {
        return std::unexpected(valid.error());
    }
    FrameInput frame;
    frame.params.count = spec.count;
    frame.params.triangles = spec.triangles;
    frame.params.bins = spec.bins;
    // RH, vertical FOV 90 degrees, unit aspect, finite reversed-Z perspective; eye is the origin.
    frame.params.viewProjection = {1,
                                   0,
                                   0,
                                   0,
                                   0,
                                   1,
                                   0,
                                   0,
                                   0,
                                   0,
                                   kNear / (kFar - kNear),
                                   -1,
                                   0,
                                   0,
                                   kNear * kFar / (kFar - kNear),
                                   0};
    const float sideNormal = static_cast<float>(1 / std::sqrt(2.0));
    frame.params.planes = {{{sideNormal, 0, -sideNormal, 0},
                            {-sideNormal, 0, -sideNormal, 0},
                            {0, sideNormal, -sideNormal, 0},
                            {0, -sideNormal, -sideNormal, 0},
                            {0, 0, -1, -kNear},
                            {0, 0, 1, kFar}}};
    frame.instances.resize(spec.count);
    frame.bitmap.resize(spec.count);
    frame.binOffsets.resize(spec.bins + 1);
    const auto visibleCount = static_cast<uint32_t>(std::floor(spec.count * spec.visibleFraction));
    const uint32_t phase = (logicalFrame / kFramesPerPhase) % kPhaseCount;
    const uint32_t side = gridSide(spec.count);
    const double cellWidth = kGridWidth / side;
    for (uint32_t bin = 0; bin < spec.bins; ++bin) {
        const auto begin = static_cast<uint32_t>(uint64_t{bin} * spec.count / spec.bins);
        const auto end = static_cast<uint32_t>(uint64_t{bin + 1} * spec.count / spec.bins);
        for (uint32_t id = begin; id < end; ++id) {
            const auto cell = static_cast<uint32_t>(
                (uint64_t{id} + kSeed % spec.count + uint64_t{phase} * rotationStep(spec.count)) %
                spec.count);
            const bool inside = cell < visibleCount;
            // Each cell has a distinct exactly representable float depth at the capacity limit.
            const float depth = static_cast<float>(8.0 + 8.0 * cell / spec.count);
            const double x =
                -kGridWidth / 2 + (cell % side + 0.5) * cellWidth + (inside ? 0 : kOutsideShift);
            const double y = -kGridWidth / 2 + (cell / side + 0.5) * cellWidth;
            const float halfExtent = static_cast<float>(kQuadCellFraction * cellWidth * depth);
            const float radius = std::nextafter(static_cast<float>(std::sqrt(2.0) * halfExtent),
                                                std::numeric_limits<float>::infinity());
            const Instance instance{static_cast<float>(x * depth),
                                    static_cast<float>(y * depth),
                                    -depth,
                                    halfExtent,
                                    radius,
                                    bin};
            if (!validBounds(instance)) {
                return std::unexpected("Generated nonfinite or nonconservative sphere bounds");
            }
            for (const auto& plane : frame.params.planes) {
                if (std::abs(planeDistance(instance, plane) + instance.radius) <
                    kScoredBoundaryMargin) {
                    return std::unexpected("Generated sphere violates the scored boundary margin");
                }
            }
            frame.instances[id] = instance;
            frame.bitmap[id] = inside ? 1 : 0;
        }
    }
    frame.visibleIds = classify(frame);
    if (frame.visibleIds.size() != visibleCount) {
        return std::unexpected("Independent visibility count disagrees with generated cells");
    }
    for (uint32_t id : frame.visibleIds) {
        if (frame.bitmap[id] != 1) {
            return std::unexpected("Independent visibility IDs disagree with generated cells");
        }
        ++frame.binOffsets[frame.instances[id].bin + 1];
    }
    std::partial_sum(frame.binOffsets.begin(), frame.binOffsets.end(), frame.binOffsets.begin());
    return frame;
}

//======================================================================================================================
bool sphereVisible(const Instance& instance, const Params& params) {
    return validPlanes(params) && validBounds(instance) && visibleBounds(instance, params);
}

//======================================================================================================================
std::vector<uint32_t> classify(const FrameInput& frame) {
    if (frame.instances.size() != frame.params.count || frame.params.count > kMaxInstances ||
        frame.params.bins == 0 || frame.params.bins > kMaxBins || !validPlanes(frame.params)) {
        return {};
    }
    std::vector<uint32_t> ids;
    ids.reserve(frame.params.count);
    std::array<uint32_t, kMaxBins + 1> offsets{};
    for (uint32_t id = 0; id < frame.params.count; ++id) {
        const auto& instance = frame.instances[id];
        if (instance.bin >= frame.params.bins || !validBounds(instance)) {
            return {};
        }
        if (visibleBounds(instance, frame.params)) {
            ids.push_back(id);
            ++offsets[instance.bin + 1];
        }
    }
    std::partial_sum(offsets.begin(), offsets.end(), offsets.begin());
    std::vector<uint32_t> packed(ids.size());
    for (uint32_t id : ids) {
        packed[offsets[frame.instances[id].bin]++] = id;
    }
    return packed;
}

//======================================================================================================================
std::string frameHash(const FrameInput& frame) {
    uint64_t hash = 14695981039346656037ULL;
    for (float value : frame.params.viewProjection) {
        hashFloat(hash, value);
    }
    for (const auto& plane : frame.params.planes) {
        for (float value : plane) {
            hashFloat(hash, value);
        }
    }
    for (uint32_t value :
         {frame.params.count, frame.params.triangles, frame.params.bins, frame.params.suite,
          frame.params.batched, frame.params.binOffset, frame.params.debug}) {
        hashWord(hash, value, 4);
    }
    hashFloat(hash, frame.params.epsilon);
    hashWord(hash, frame.instances.size(), 8);
    for (const auto& instance : frame.instances) {
        for (float value :
             {instance.x, instance.y, instance.z, instance.halfExtent, instance.radius}) {
            hashFloat(hash, value);
        }
        for (uint32_t value : {instance.bin, instance.padding0, instance.padding1}) {
            hashWord(hash, value, 4);
        }
    }
    for (const auto* values : {&frame.bitmap, &frame.visibleIds, &frame.binOffsets}) {
        hashWord(hash, values->size(), 8);
        for (uint32_t value : *values) {
            hashWord(hash, value, 4);
        }
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

//======================================================================================================================
std::string manifestJson(const Case& spec) {
    auto first = makeFrame(spec, 0);
    if (!first) {
        return "{\"schemaVersion\":1,\"valid\":false,\"id\":" + quote(spec.id) +
               ",\"error\":" + quote(first.error()) + "}";
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    const bool scoredMatrixCase = std::ranges::any_of(caseMatrix(), [&](const Case& candidate) {
        return candidate.id == spec.id && candidate.count == spec.count &&
               candidate.triangles == spec.triangles && candidate.bins == spec.bins &&
               candidate.visibleFraction == spec.visibleFraction;
    });
    out << "{\"schemaVersion\":1,\"valid\":true,\"generatorVersion\":" << quote(kGeneratorVersion)
        << ",\"scoredMatrixCase\":" << (scoredMatrixCase ? "true" : "false")
        << ",\"seed\":" << kSeed << ",\"id\":" << quote(spec.id) << ",\"count\":" << spec.count
        << ",\"triangles\":" << spec.triangles << ",\"bins\":" << spec.bins
        << ",\"visibleFraction\":" << spec.visibleFraction
        << ",\"requestedVisibleCount\":" << std::floor(spec.count * spec.visibleFraction)
        << ",\"actualVisibleCount\":" << first->visibleIds.size()
        << ",\"target\":{\"width\":" << kExtent << ",\"height\":" << kExtent
        << ",\"color\":\"RGBA8Unorm\",\"colorSpace\":\"scene-linear\",\"depth\":\"D32Float\","
           "\"depthClear\":0,\"depthCompare\":\"greater\",\"samples\":1,\"blending\":false,"
           "\"exposure\":1,\"postProcessing\":false},\"camera\":{\"handedness\":\"right\","
           "\"forward\":\"-Z\",\"up\":\"+Y\",\"worldUnits\":\"arbitrary\","
           "\"matrixLayout\":\"column-major\",\"ndcDepth\":\"reversed [0,1]\","
           "\"verticalFovDegrees\":90,\"aspect\":1,\"near\":"
        << kNear << ",\"far\":" << kFar << ",\"viewProjection\":";
    writeNumbers(out, first->params.viewProjection);
    out << ",\"planeOrder\":[\"left\",\"right\",\"bottom\",\"top\",\"near\",\"far\"],\"planes\":[";
    for (size_t index = 0; index < first->params.planes.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        writeNumbers(out, first->params.planes[index]);
    }
    out << "]},\"oracle\":{\"arithmetic\":\"float64 over uploaded float32 fields\","
           "\"visibleRule\":\"all(dot(plane.xyz,center)+plane.w >= -radius-epsilon)\","
           "\"epsilon\":0,\"boundaryFixtureEpsilon\":"
        << kBoundaryEpsilon << ",\"scoredBoundaryMargin\":" << kScoredBoundaryMargin
        << ",\"planeNormSquaredTolerance\":" << kPlaneNormTolerance
        << "},\"geometry\":{\"plane\":\"XY\",\"gridCellsPerSide\":"
        << (spec.triangles == 32 ? 4 : 1)
        << ",\"cellCorners\":[[0,0],[1,0],[1,1],[0,0],[1,1],[0,1]],"
           "\"localCoordinates\":\"2*(cell+corner)/gridCellsPerSide-1\","
           "\"worldCoordinates\":\"(x+local.x*halfExtent,y+local.y*halfExtent,z)\","
           "\"ordinaryFirstVertex\":\"objectId*3*triangles\","
           "\"batchedObjectId\":\"visibleIds[binOffset+instanceId]\",\"firstInstance\":0},"
           "\"generator\":{\"gridSide\":"
        << gridSide(spec.count) << ",\"gridNdcWidth\":" << kGridWidth
        << ",\"quadHalfExtentCellFraction\":" << kQuadCellFraction
        << ",\"outsideNdcShiftX\":" << kOutsideShift
        << ",\"cellFormula\":\"(objectId+seed%count+phase*rotationStep)%count; empty if count=0\","
           "\"visibleCellRule\":\"cell<floor(count*visibleFraction)\","
           "\"depthFormula\":\"float32(8+8*cell/count)\",\"rotationStep\":"
        << rotationStep(spec.count)
        << ",\"radiusRule\":\"nextafter(float32(sqrt(2)*halfExtent),+infinity)\"},"
           "\"packingOrder\":\"ascending bin then objectId\",\"binCandidateRanges\":[";
    for (uint32_t bin = 0; bin < spec.bins; ++bin) {
        if (bin != 0) {
            out << ',';
        }
        out << '[' << uint64_t{bin} * spec.count / spec.bins << ','
            << uint64_t{bin + 1} * spec.count / spec.bins << ']';
    }
    out << "],\"linearBinColors\":[";
    for (uint32_t bin = 0; bin < spec.bins; ++bin) {
        if (bin != 0) {
            out << ',';
        }
        writeNumbers(out, std::array{(bin * 37 % 251 + 1) / 252.F, (bin * 73 % 251 + 1) / 252.F,
                                     (bin * 109 % 251 + 1) / 252.F, 1.F});
    }
    // Store the resolved transforms once: phases permute these exact float32 records, not math.
    out << "],\"phaseZeroTransformFields\":[\"x\",\"y\",\"z\",\"halfExtent\",\"radius\"],"
           "\"phaseZeroTransforms\":[";
    for (size_t id = 0; id < first->instances.size(); ++id) {
        if (id != 0) {
            out << ',';
        }
        const auto& instance = first->instances[id];
        writeNumbers(out, std::array{instance.x, instance.y, instance.z, instance.halfExtent,
                                     instance.radius});
    }
    out << "],\"phaseZeroBitmap\":";
    writeNumbers(out, first->bitmap);
    out << ",\"phaseTransformIndex\":\"(objectId+phase*rotationStep)%count; bin remains tied to "
           "objectId\","
           "\"replayFrames\":"
        << kReplayFrames << ",\"framesPerPhase\":" << kFramesPerPhase
        << ",\"hashAlgorithm\":\"FNV-1a-64, canonical little-endian IEEE754 fields; vector lengths "
           "uint64\","
           "\"hashIsCryptographic\":false,\"phases\":[";
    for (uint32_t phase = 0; phase < kPhaseCount; ++phase) {
        const auto frame = makeFrame(spec, phase * kFramesPerPhase);
        if (!frame) {
            return "{\"schemaVersion\":1,\"valid\":false,\"id\":" + quote(spec.id) +
                   ",\"error\":" + quote(frame.error()) + "}";
        }
        if (phase != 0) {
            out << ',';
        }
        out << "{\"phase\":" << phase << ",\"firstFrame\":" << phase * kFramesPerPhase
            << ",\"endFrameExclusive\":" << (phase + 1) * kFramesPerPhase
            << ",\"actualVisibleCount\":" << frame->visibleIds.size()
            << ",\"frameHash\":" << quote(frameHash(*frame)) << '}';
    }
    out << "]}";
    return out.str();
}

//======================================================================================================================
std::string name(Suite value) {
    switch (value) {
    case Suite::S:
        return "S";
    case Suite::E:
        return "E";
    }
    return "unknown";
}

//======================================================================================================================
std::string name(Variant value) {
    switch (value) {
    case Variant::Direct:
        return "direct";
    case Variant::CpuIndirect:
        return "cpu-indirect";
    case Variant::GpuArgs:
        return "gpu-args";
    case Variant::GpuIcb:
        return "gpu-icb";
    case Variant::Batched:
        return "batched";
    }
    return "unknown";
}

//======================================================================================================================
std::string name(Lane value) {
    switch (value) {
    case Lane::Headline:
        return "headline";
    case Lane::GpuSpan:
        return "gpu-span";
    case Lane::Stages:
        return "stages";
    }
    return "unknown";
}

//======================================================================================================================
Result<Suite> parseSuite(std::string_view value) {
    for (Suite suite : {Suite::S, Suite::E}) {
        if (value == name(suite)) {
            return suite;
        }
    }
    return std::unexpected("Unknown suite: " + std::string(value));
}

//======================================================================================================================
Result<Variant> parseVariant(std::string_view value) {
    for (Variant variant : {Variant::Direct, Variant::CpuIndirect, Variant::GpuArgs,
                            Variant::GpuIcb, Variant::Batched}) {
        if (value == name(variant)) {
            return variant;
        }
    }
    return std::unexpected("Unknown variant: " + std::string(value));
}

//======================================================================================================================
Result<Lane> parseLane(std::string_view value) {
    for (Lane lane : {Lane::Headline, Lane::GpuSpan, Lane::Stages}) {
        if (value == name(lane)) {
            return lane;
        }
    }
    return std::unexpected("Unknown lane: " + std::string(value));
}

} // namespace lmx::experimental::submission
